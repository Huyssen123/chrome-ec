/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "builtin/endian.h"
#include "console.h"
#include "dcrypto.h"
#include "ec_commands.h"
#include "extension.h"
#include "fips.h"
#include "fips_rand.h"
#include "flash.h"
#include "flash_info.h"
#include "flash_log.h"
#include "hooks.h"
#include "new_nvmem.h"
#include "nvmem.h"
#include "nvmem_vars.h"
#include "registers.h"
#include "scratch_reg1.h"
#include "shared_mem.h"
#include "system.h"
#include "tpm_nvmem_ops.h"
#include "u2f_impl.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

/**
 * Combined FIPS status & global FIPS error.
 * default value is  = FIPS_UNINITIALIZED
 */
static enum fips_status _fips_status;

/* Return current FIPS status, but prevent direct modification of state. */
enum fips_status fips_status(void)
{
	return _fips_status;
}

#ifdef CRYPTO_TEST_SETUP
/* Flag to simulate specific error condition in power-up tests. */
uint8_t fips_break_cmd;
#else
/* For production set it to zero, so check is eliminated. */
#define fips_break_cmd 0
#endif

/**
 * Return true if no blocking crypto errors detected.
 * Until self-integrity works properly (b/138578318), ignore it.
 * TODO(b/138578318): remove ignoring of FIPS_FATAL_SELF_INTEGRITY.
 */
static inline bool fips_is_no_crypto_error(void)
{
	return (_fips_status &
		(FIPS_ERROR_MASK & (~FIPS_FATAL_SELF_INTEGRITY))) == 0;
}

/* Return true if crypto can be used (no failures detected). */
bool fips_crypto_allowed(void)
{
	/**
	 * We never allow crypto if there were errors, no matter
	 * if we are in FIPS approved or not-approved mode.
	 */
	return ((_fips_status & FIPS_POWER_UP_TEST_DONE) &&
		fips_is_no_crypto_error() && DCRYPTO_ladder_is_enabled());
}

int crypto_enabled(void)
{
	return fips_crypto_allowed();
}

void fips_throw_err(enum fips_status err)
{
	/* if not a new error, don't write it in the flash log */
	if ((_fips_status & err) == err)
		return;
	fips_set_status(err);
	if (!fips_is_no_crypto_error()) {
#ifdef CONFIG_FLASH_LOG
		fips_vtable->flash_log_add_event(FE_LOG_FIPS_FAILURE,
						 sizeof(_fips_status),
						 &_fips_status);
#endif
		/* Revoke access to secrets in HW Key ladder. */
		DCRYPTO_ladder_revoke();
	}
}

/**
 * Set status of FIPS power-up tests on wake from sleep. We don't want to
 * run lengthy KAT & power-up tests on every wake-up, so need to 'cache'
 * result in long life register which content persists during sleep mode.
 *
 * @param asserted: false power-up tests should run on resume, otherwise
 * can be skipped.
 */
static void fips_set_power_up(bool asserted)
{
	/* Enable writing to the long life register */
	if (asserted)
		GREG32(PMU, PWRDN_SCRATCH22) = BOARD_FIPS_POWERUP_DONE;
	else
		GREG32(PMU, PWRDN_SCRATCH22) = 0;
}

/**
 * Return true if FIPS KAT tests completed successfully after waking up
 * from sleep mode which clears RAM.
 */
static bool fips_is_power_up_done(void)
{
	return !!(GREG32(PMU, PWRDN_SCRATCH22) == BOARD_FIPS_POWERUP_DONE);
}

void fips_set_status(enum fips_status status)
{
	/**
	 * if FIPS error took place, drop indication of FIPS approved mode.
	 * Next cycle of sleep will power-cycle HW crypto components, so any
	 * soft-errors will be recovered. In case of hard errors it
	 * will be detected again.
	 */
	/* Accumulate status (errors). */
	_fips_status |= status;

	status = _fips_status;
	/* if we have error, require power up tests on resume. */
	if (status & FIPS_ERROR_MASK)
		fips_set_power_up(false);
}

/**
 * Test vectors for Known-Answer Tests (KATs) and driving functions.
 */

/* KAT for SHA256, test values from OpenSSL. */
static bool fips_sha256_kat(void)
{
	struct sha256_ctx ctx;

	static const uint8_t in[] = /* "etaonrishd" */ { 0x65, 0x74, 0x61, 0x6f,
							 0x6e, 0x72, 0x69, 0x73,
							 0x68, 0x64 };
	static const uint8_t ans[] = { 0xf5, 0x53, 0xcd, 0xb8, 0xcf, 0x1,  0xee,
				       0x17, 0x9b, 0x93, 0xc9, 0x68, 0xc0, 0xea,
				       0x40, 0x91, 0x6,	 0xec, 0x8e, 0x11, 0x96,
				       0xc8, 0x5d, 0x1c, 0xaf, 0x64, 0x22, 0xe6,
				       0x50, 0x4f, 0x47, 0x57 };

	SHA256_hw_init(&ctx);
	SHA256_update(&ctx, in, sizeof(in));
	return !(fips_break_cmd == FIPS_BREAK_SHA256) &&
	       (memcmp(SHA256_final(&ctx), ans, SHA256_DIGEST_SIZE) == 0);
}

/* KAT for HMAC-SHA256, test values from OpenSSL. */
static bool fips_hmac_sha256_kat(void)
{
	struct hmac_sha256_ctx ctx;

	static const uint8_t k[SHA256_DIGEST_SIZE] =
		/* "etaonrishd" */ { 0x65, 0x74, 0x61, 0x6f, 0x6e, 0x72, 0x69,
				     0x73, 0x68, 0x64, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00 };
	static const uint8_t in[] =
		/* "Sample text" */ { 0x53, 0x61, 0x6d, 0x70, 0x6c, 0x65,
				      0x20, 0x74, 0x65, 0x78, 0x74 };
	static const uint8_t ans[] = { 0xe9, 0x17, 0xc1, 0x7b, 0x4c, 0x6b, 0x77,
				       0xda, 0xd2, 0x30, 0x36, 0x02, 0xf5, 0x72,
				       0x33, 0x87, 0x9f, 0xc6, 0x6e, 0x7b, 0x7e,
				       0xa8, 0xea, 0xaa, 0x9f, 0xba, 0xee, 0x51,
				       0xff, 0xda, 0x24, 0xf4 };

	HMAC_SHA256_hw_init(&ctx, k, sizeof(k));
	HMAC_SHA256_update(&ctx, in, sizeof(in));
	return !(fips_break_cmd == FIPS_BREAK_HMAC_SHA256) &&
	       (memcmp(HMAC_SHA256_hw_final(&ctx), ans, SHA256_DIGEST_SIZE) ==
		0);
}

/**
 * DRBG test vector source recorded 6/1/17 from
 * http://shortn/_eNfI4wD6j8 -> https://csrc.nist.gov/projects/
 * cryptographic-algorithm-validation-program/random-number-generators
 * http://shortn/_9hsazxHKn7 -> https://csrc.nist.gov/CSRC/media/Projects/
 * Cryptographic-Algorithm-Validation-Program/documents/drbg/drbgtestvectors.zip
 * Input values:
 * [SHA-256]
 * [PredictionResistance = True]
 * [EntropyInputLen = 256]
 * [NonceLen = 128]
 * [PersonalizationStringLen = 256]
 * [AdditionalInputLen = 256]
 * [ReturnedBitsLen = 1024]
 * COUNT = 0
 * EntropyInput =
 * 4294671d493dc085b5184607d7de2ff2b6aceb734a1b026f6cfee7c5a90f03da
 * Nonce = d071544e599235d5eb38b64b551d2a6e
 * PersonalizationString =
 * 63bc769ae1d95a98bde870e4db7776297041d37c8a5c688d4e024b78d83f4d78
 * AdditionalInput =
 * 28848becd3f47696f124f4b14853a456156f69be583a7d4682cff8d44b39e1d3
 * EntropyInputPR =
 * db9b4790b62336fbb9a684b82947065393eeef8f57bd2477141ad17e776dac34
 * AdditionalInput =
 * 8bfce0b7132661c3cd78175d83926f643e36f7608eec2c5dac3ddcbacc8c2182
 * EntropyInputPR =
 * 4a9abe80f6f522f29878bedf8245b27940a76471006fb4a4110beb4decb6c341
 * ReturnedBits =
 * e580dc969194b2b18a97478aef9d1a72390aff14562747bf080d741527a6655
 * ce7fc135325b457483a9f9c70f91165a811cf4524b50d51199a0df3bd60d12abac27d0bf6618
 * e6b114e05420352e23f3603dfe8a225dc19b3d1fff1dc245dc6b1df24c741744bec3f9437dbb
 * f222df84881a457a589e7815ef132f686b760f012
 * DRBG KAT generation sequence:
 * hmac_drbg_init(entropy0, nonce0, perso0)
 * hmac_drbg_reseed(entropy1, addtl_input1)
 * hmac_drbg_generate()
 * hmac_drbg_reseed(entropy2, addtl_input2)
 * hmac_drbg_generate()
 */
static const uint8_t drbg_entropy0[] = {
	0x42, 0x94, 0x67, 0x1d, 0x49, 0x3d, 0xc0, 0x85, 0xb5, 0x18, 0x46,
	0x07, 0xd7, 0xde, 0x2f, 0xf2, 0xb6, 0xac, 0xeb, 0x73, 0x4a, 0x1b,
	0x02, 0x6f, 0x6c, 0xfe, 0xe7, 0xc5, 0xa9, 0x0f, 0x03, 0xda
};
static const uint8_t drbg_nonce0[] = { 0xd0, 0x71, 0x54, 0x4e, 0x59, 0x92,
				       0x35, 0xd5, 0xeb, 0x38, 0xb6, 0x4b,
				       0x55, 0x1d, 0x2a, 0x6e };
static const uint8_t drbg_perso0[] = { 0x63, 0xbc, 0x76, 0x9a, 0xe1, 0xd9, 0x5a,
				       0x98, 0xbd, 0xe8, 0x70, 0xe4, 0xdb, 0x77,
				       0x76, 0x29, 0x70, 0x41, 0xd3, 0x7c, 0x8a,
				       0x5c, 0x68, 0x8d, 0x4e, 0x02, 0x4b, 0x78,
				       0xd8, 0x3f, 0x4d, 0x78 };

static const uint8_t drbg_entropy1[] = {
	0xdb, 0x9b, 0x47, 0x90, 0xb6, 0x23, 0x36, 0xfb, 0xb9, 0xa6, 0x84,
	0xb8, 0x29, 0x47, 0x06, 0x53, 0x93, 0xee, 0xef, 0x8f, 0x57, 0xbd,
	0x24, 0x77, 0x14, 0x1a, 0xd1, 0x7e, 0x77, 0x6d, 0xac, 0x34
};
static const uint8_t drbg_addtl_input1[] = {
	0x28, 0x84, 0x8b, 0xec, 0xd3, 0xf4, 0x76, 0x96, 0xf1, 0x24, 0xf4,
	0xb1, 0x48, 0x53, 0xa4, 0x56, 0x15, 0x6f, 0x69, 0xbe, 0x58, 0x3a,
	0x7d, 0x46, 0x82, 0xcf, 0xf8, 0xd4, 0x4b, 0x39, 0xe1, 0xd3
};

static const uint8_t drbg_entropy2[] = {
	0x4a, 0x9a, 0xbe, 0x80, 0xf6, 0xf5, 0x22, 0xf2, 0x98, 0x78, 0xbe,
	0xdf, 0x82, 0x45, 0xb2, 0x79, 0x40, 0xa7, 0x64, 0x71, 0x00, 0x6f,
	0xb4, 0xa4, 0x11, 0x0b, 0xeb, 0x4d, 0xec, 0xb6, 0xc3, 0x41
};
static const uint8_t drbg_addtl_input2[] = {
	0x8b, 0xfc, 0xe0, 0xb7, 0x13, 0x26, 0x61, 0xc3, 0xcd, 0x78, 0x17,
	0x5d, 0x83, 0x92, 0x6f, 0x64, 0x3e, 0x36, 0xf7, 0x60, 0x8e, 0xec,
	0x2c, 0x5d, 0xac, 0x3d, 0xdc, 0xba, 0xcc, 0x8c, 0x21, 0x82
};

/* Known-answer test for HMAC_DRBG SHA256 instantiate. */
static bool fips_hmac_drbg_instantiate_kat(struct drbg_ctx *ctx)
{
	/* Expected internal drbg state */
	static const uint32_t K0[] = { 0x7fe2b43a, 0x94f11b33, 0x2b76c5ce,
				       0xfbb784af, 0x81cfe716, 0xc43596d6,
				       0xbdfe968b, 0x189c80fb };
	static const uint32_t V0[] = { 0xc42b237a, 0x929cdd0b, 0xe7fbafdd,
				       0xba22a36a, 0x4d23471a, 0xd8607022,
				       0x687e18ac, 0x2ac08007 };

	hmac_drbg_init(ctx, drbg_entropy0, sizeof(drbg_entropy0),
		       drbg_nonce0, sizeof(drbg_nonce0), drbg_perso0,
		       sizeof(drbg_perso0));

	return (memcmp(ctx->v, V0, sizeof(V0)) == 0) &&
	       (memcmp(ctx->k, K0, sizeof(K0)) == 0);
}

/* Known-answer test for HMAC_DRBG SHA256 reseed. */
static bool fips_hmac_drbg_reseed_kat(struct drbg_ctx *ctx)
{
	/* Expected internal drbg state */
	static const uint32_t K1[] = { 0x3118D36E, 0x05DEEC48, 0x7EFB6363,
				       0x3D575CDE, 0xCFCD14C1, 0x8D4F937D,
				       0x896B811E, 0x0EF038EB };
	static const uint32_t V1[] = { 0xC8ED8EEC, 0x24DD7B66, 0x09C635CD,
				       0x6AC74196, 0xC70067D7, 0xC2E71FEF,
				       0x918D9EB7, 0xAE0CD544 };

	hmac_drbg_reseed(ctx, drbg_entropy1, sizeof(drbg_entropy1),
			 drbg_addtl_input1, sizeof(drbg_addtl_input1), NULL, 0);

	return (memcmp(ctx->v, V1, sizeof(V1)) == 0) &&
	       (memcmp(ctx->k, K1, sizeof(K1)) == 0);
}

/* Known-answer test for HMAC_DRBG SHA256 generate. */
static bool fips_hmac_drbg_generate_kat(struct drbg_ctx *ctx)
{
	/* Expected internal drbg state */
	static const uint32_t K2[] = { 0x980ccd6a, 0x0b34f7e1, 0x594aabd7,
				       0x33b66049, 0xb919bd57, 0x8ecc7194,
				       0xaf1748a3, 0x80982577 };
	static const uint32_t V2[] = { 0xe4927cdb, 0xb3435cc5, 0x601ab870,
				       0x46e1f024, 0x966ca875, 0x102b4167,
				       0xa71e5dce, 0xe4c15962 };
	/* Expected output */
	static const uint8_t KA[] = {
		0xe5, 0x80, 0xdc, 0x96, 0x91, 0x94, 0xb2, 0xb1, 0x8a, 0x97,
		0x47, 0x8a, 0xef, 0x9d, 0x1a, 0x72, 0x39, 0x0a, 0xff, 0x14,
		0x56, 0x27, 0x47, 0xbf, 0x08, 0x0d, 0x74, 0x15, 0x27, 0xa6,
		0x65, 0x5c, 0xe7, 0xfc, 0x13, 0x53, 0x25, 0xb4, 0x57, 0x48,
		0x3a, 0x9f, 0x9c, 0x70, 0xf9, 0x11, 0x65, 0xa8, 0x11, 0xcf,
		0x45, 0x24, 0xb5, 0x0d, 0x51, 0x19, 0x9a, 0x0d, 0xf3, 0xbd,
		0x60, 0xd1, 0x2a, 0xba, 0xc2, 0x7d, 0x0b, 0xf6, 0x61, 0x8e,
		0x6b, 0x11, 0x4e, 0x05, 0x42, 0x03, 0x52, 0xe2, 0x3f, 0x36,
		0x03, 0xdf, 0xe8, 0xa2, 0x25, 0xdc, 0x19, 0xb3, 0xd1, 0xff,
		0xf1, 0xdc, 0x24, 0x5d, 0xc6, 0xb1, 0xdf, 0x24, 0xc7, 0x41,
		0x74, 0x4b, 0xec, 0x3f, 0x94, 0x37, 0xdb, 0xbf, 0x22, 0x2d,
		0xf8, 0x48, 0x81, 0xa4, 0x57, 0xa5, 0x89, 0xe7, 0x81, 0x5e,
		0xf1, 0x32, 0xf6, 0x86, 0xb7, 0x60, 0xf0, 0x12
	};
	uint8_t buf[128];

	hmac_drbg_generate(ctx, buf, sizeof(buf), NULL, 0);
	/* Verify internal drbg state */
	if (memcmp(ctx->v, V2, sizeof(V2)) ||
	    memcmp(ctx->k, K2, sizeof(K2))) {
		return false;
	}

	hmac_drbg_reseed(ctx, drbg_entropy2, sizeof(drbg_entropy2),
			 drbg_addtl_input2, sizeof(drbg_addtl_input2), NULL, 0);
	/**
	 * reuse entropy buffer to avoid allocating too much stack and memory
	 * it will be cleaned up in TRNG health test
	 */
	hmac_drbg_generate(ctx, buf, sizeof(buf), NULL, 0);
	return !(fips_break_cmd == FIPS_BREAK_HMAC_DRBG) &&
	       (memcmp(buf, KA, sizeof(KA)) == 0);
}

/* Known-answer test for HMAC_DRBG SHA256. */
static bool fips_hmac_drbg_kat(void)
{
	struct drbg_ctx ctx;

	return fips_hmac_drbg_instantiate_kat(&ctx) &&
	       fips_hmac_drbg_reseed_kat(&ctx) &&
	       fips_hmac_drbg_generate_kat(&ctx);
}

/* Known-answer test for ECDSA NIST P-256 verify. */
static bool fips_ecdsa_verify_kat(void)
{
	static const p256_int qx = { .a = { 0xf49abf3c, 0xf82e6e12, 0x7a67c074,
					    0x5134e16f, 0xf8957a0c, 0xef4344a7,
					    0xd4bb3cb7, 0xe424dc61 } };
	static const p256_int qy = { .a = { 0xdfaee927, 0x3d6f60e7, 0xac85d124,
					    0x127e5965, 0xe1dddaf0, 0x1545949d,
					    0xa2bc4865, 0x970eed7a } };
	static const p256_int r = { .a = { 0xd9347f4f, 0xb72f981f, 0x6349b9da,
					   0x2ff540c7, 0x42017c64, 0x910be331,
					   0xa49c705c, 0xbf96b99a } };
	static const p256_int s = { .a = { 0x57ec871c, 0x920b9e0f, 0x75d98f31,
					   0x444e3230, 0x15abdf12, 0xe03b9cd4,
					   0x819089c2, 0x17c55095 } };
	static const uint8_t msg[128] = {
		0xe1, 0x13, 0x0a, 0xf6, 0xa3, 0x8c, 0xcb, 0x41, 0x2a, 0x9c,
		0x8d, 0x13, 0xe1, 0x5d, 0xbf, 0xc9, 0xe6, 0x9a, 0x16, 0x38,
		0x5a, 0xf3, 0xc3, 0xf1, 0xe5, 0xda, 0x95, 0x4f, 0xd5, 0xe7,
		0xc4, 0x5f, 0xd7, 0x5e, 0x2b, 0x8c, 0x36, 0x69, 0x92, 0x28,
		0xe9, 0x28, 0x40, 0xc0, 0x56, 0x2f, 0xbf, 0x37, 0x72, 0xf0,
		0x7e, 0x17, 0xf1, 0xad, 0xd5, 0x65, 0x88, 0xdd, 0x45, 0xf7,
		0x45, 0x0e, 0x12, 0x17, 0xad, 0x23, 0x99, 0x22, 0xdd, 0x9c,
		0x32, 0x69, 0x5d, 0xc7, 0x1f, 0xf2, 0x42, 0x4c, 0xa0, 0xde,
		0xc1, 0x32, 0x1a, 0xa4, 0x70, 0x64, 0xa0, 0x44, 0xb7, 0xfe,
		0x3c, 0x2b, 0x97, 0xd0, 0x3c, 0xe4, 0x70, 0xa5, 0x92, 0x30,
		0x4c, 0x5e, 0xf2, 0x1e, 0xed, 0x9f, 0x93, 0xda, 0x56, 0xbb,
		0x23, 0x2d, 0x1e, 0xeb, 0x00, 0x35, 0xf9, 0xbf, 0x0d, 0xfa,
		0xfd, 0xcc, 0x46, 0x06, 0x27, 0x2b, 0x20, 0xa3
	};

	p256_int p256_digest;
	struct sha256_digest digest;
	uint8_t bad_msg[128];
	int passed;

	SHA256_hw_hash(msg, sizeof(msg), &digest);
	p256_from_bin(digest.b8, &p256_digest);
	passed = dcrypto_p256_ecdsa_verify(&qx, &qy, &p256_digest, &r, &s);
	if (!passed)
		return false;
	/**
	 * create bad_msg same as msg but has one bit flipped in byte 92 (0x0a
	 * vs 0x1a) this is to save space in flash vs. having bad message as
	 * constant
	 */
	memcpy(bad_msg, msg, sizeof(msg));
	bad_msg[92] ^= 0x10;
	SHA256_hw_hash(bad_msg, sizeof(bad_msg), &digest);
	p256_from_bin(digest.b8, &p256_digest);
	passed = dcrypto_p256_ecdsa_verify(&qx, &qy, &p256_digest, &r, &s);
	return !(fips_break_cmd == FIPS_BREAK_ECDSA) && (passed == 0);
}

#ifdef CONFIG_FIPS_AES_CBC_256
#define AES_BLOCK_LEN 16

/* Known-answer test for AES-256 encrypt/decrypt. */
static bool fips_aes256_kat(void)
{
	uint8_t enc[AES_BLOCK_LEN];
	uint8_t dec[AES_BLOCK_LEN];
	uint8_t iv[AES_BLOCK_LEN];

	static const uint8_t kat_aes128_k[AES256_BLOCK_CIPHER_KEY_SIZE] = {
		0x65, 0x74, 0x61, 0x6f, 0x6e, 0x72, 0x69, 0x73,
		0x68, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	static const uint8_t kat_aes128_msg[AES_BLOCK_LEN] = {
		0x00, 0xAA, 0x00, 0xAA, 0x00, 0xAA, 0x00, 0xAA,
		0x00, 0xAA, 0x00, 0xAA, 0x00, 0xAA, 0x00, 0xAA
	};

	static const uint8_t ans_aes128[AES_BLOCK_LEN] = {
		0x64, 0x62, 0x89, 0x41, 0x73, 0x63, 0x70, 0xe9,
		0x12, 0x7e, 0xa7, 0x1b, 0x1b, 0xc3, 0x57, 0x8d
	};

	memset(iv, 0, sizeof(iv));
	DCRYPTO_aes_init(kat_aes128_k, 256, iv, CIPHER_MODE_CBC, ENCRYPT_MODE);
	DCRYPTO_aes_block(kat_aes128_msg, enc);
	if (memcmp(enc, ans_aes128, AES_BLOCK_LEN))
		return false;

	DCRYPTO_aes_init(kat_aes128_k, 256, iv, CIPHER_MODE_CBC, DECRYPT_MODE);
	DCRYPTO_aes_block(enc, dec);

	return !(fips_break_cmd == FIPS_BREAK_AES256) &&
	       (memcmp(kat_aes128_msg, dec, AES_BLOCK_LEN) == 0);
}
#endif

#ifdef CONFIG_FIPS_RSA2048
/* Known-answer test for RSA 2048. */
static bool fips_rsa2048_verify_kat(void)
{
	struct sha256_digest digest;
	static const uint32_t pub[64] = {
		0xf8729219, 0x2b42fc45, 0xfe6f4397, 0xa6ba59df, 0x4ce45ab8,
		0x4be044ea, 0xdade58ec, 0xf871ada6, 0x3a6355a1, 0x43739940,
		0x2fbdff33, 0x3e6f8953, 0xd2f99a29, 0xb0835670, 0x4d9144e1,
		0x3518387f, 0x808bef09, 0x1f612714, 0xa109e770, 0xcf0f4123,
		0x1d74505e, 0xa9b7c557, 0x176fcc28, 0xe0e86a16, 0x699b54eb,
		0x2c3514b8, 0xf236634f, 0xf4f5b4ae, 0x12d180a4, 0x5e587a1a,
		0xd7b9bd27, 0x649965dc, 0x5097e8aa, 0xa42c8ae7, 0x1e252547,
		0x11ed1901, 0x898ed7c4, 0x05705388, 0x866ac091, 0x5769c900,
		0x05108735, 0xca60769e, 0x7ab9ae85, 0xce7440eb, 0xe60eb7c8,
		0xd8d80ee8, 0xa151febc, 0x93d49bbc, 0xc0a79b3f, 0x48dbad30,
		0x9ff65c53, 0x2db20805, 0x175d83de, 0xfffceebd, 0x203e209e,
		0xafee1f86, 0x39b46031, 0x36b0c302, 0x85222b79, 0x891b7941,
		0x69d37fab, 0xec6cca57, 0xc81e692b, 0xd5e1b4e8
	};
	static const uint8_t sig[256] = {
		0x02, 0xa7, 0x8c, 0x15, 0x44, 0x00, 0x44, 0x2f, 0x2e, 0x45,
		0xb2, 0xf6, 0x11, 0x01, 0xdf, 0xcf, 0x28, 0xfd, 0x50, 0xf2,
		0x89, 0x59, 0x7c, 0x93, 0x1f, 0xec, 0x7d, 0xf9, 0xf7, 0x66,
		0xf1, 0xf5, 0x9d, 0x81, 0xad, 0x7a, 0x05, 0xcd, 0x93, 0xea,
		0x93, 0x0a, 0x41, 0x60, 0x34, 0x3d, 0xeb, 0x2f, 0x87, 0x8f,
		0x25, 0x13, 0x07, 0x61, 0xd8, 0x86, 0x64, 0xca, 0x74, 0xd7,
		0xff, 0xbf, 0xc3, 0xdc, 0xef, 0x5a, 0xcf, 0xa0, 0xff, 0x3a,
		0xe5, 0x91, 0x4b, 0xd1, 0xa6, 0x01, 0xe5, 0xb0, 0x98, 0xf5,
		0x01, 0x65, 0xe6, 0x62, 0xf4, 0x51, 0x15, 0xc0, 0xba, 0xe6,
		0xee, 0x0a, 0xa5, 0x83, 0xfb, 0x25, 0x1d, 0x09, 0x95, 0x49,
		0xc0, 0xf7, 0x32, 0x2d, 0x44, 0x49, 0xa4, 0x51, 0xa7, 0x2c,
		0xa5, 0x79, 0xc9, 0x80, 0x90, 0xd8, 0x3c, 0xd5, 0x25, 0x37,
		0x31, 0x04, 0xb1, 0x9b, 0x3e, 0xed, 0x3e, 0x49, 0x2c, 0xc2,
		0x11, 0xf2, 0x58, 0x36, 0x6c, 0x63, 0x15, 0xef, 0x34, 0x81,
		0xb2, 0xb8, 0xa3, 0x6b, 0x4a, 0x87, 0x0f, 0xd8, 0x87, 0x27,
		0x76, 0x2c, 0x51, 0x7d, 0xa3, 0x8e, 0xc7, 0xa1, 0x08, 0x47,
		0x35, 0xa4, 0x63, 0xd2, 0xe6, 0x05, 0x70, 0x15, 0x12, 0xbe,
		0x38, 0x95, 0x15, 0x3c, 0xf7, 0xed, 0xb0, 0x1a, 0xba, 0x81,
		0x93, 0x08, 0xe6, 0xec, 0x08, 0xe9, 0x5f, 0x35, 0x9d, 0x12,
		0xc2, 0xf7, 0x0f, 0xfc, 0x67, 0x40, 0x69, 0x90, 0x6e, 0x0a,
		0x3d, 0x3b, 0x83, 0x66, 0x2e, 0xee, 0x3d, 0xad, 0xad, 0xdd,
		0x46, 0xfd, 0x3d, 0x9b, 0x00, 0xd8, 0x45, 0xa6, 0xb5, 0x20,
		0x29, 0x88, 0x5f, 0x92, 0xa0, 0x63, 0x5f, 0x51, 0x17, 0xfb,
		0xde, 0xb2, 0x05, 0xb6, 0xc8, 0x4e, 0x58, 0x2b, 0xfc, 0xc5,
		0x04, 0x7d, 0x17, 0x4c, 0xd6, 0x7c, 0x05, 0xed, 0x10, 0xf8,
		0x98, 0x1e, 0xb2, 0x3a, 0x6c, 0x6d
	};
	static const uint8_t msg[128] = {
		0x2d, 0xfc, 0x5d, 0xbd, 0x44, 0x2a, 0xb6, 0x48, 0x1d, 0x6c,
		0xc7, 0xce, 0xa4, 0xcd, 0x01, 0x47, 0xff, 0xae, 0xd2, 0xbe,
		0x1d, 0x0a, 0xd5, 0xb2, 0x92, 0xfe, 0x46, 0xbb, 0xa2, 0x88,
		0xb8, 0x71, 0x9b, 0x8f, 0x0a, 0x89, 0x69, 0x23, 0x97, 0x41,
		0x64, 0x07, 0xad, 0xff, 0x6c, 0x6c, 0x41, 0x34, 0x38, 0x00,
		0xe0, 0x87, 0xeb, 0x27, 0xe9, 0x30, 0xe8, 0x88, 0xfa, 0xa1,
		0xe8, 0xcc, 0xa8, 0x6c, 0x4a, 0xa2, 0x73, 0x61, 0xaa, 0x07,
		0xf8, 0xf6, 0xb4, 0xc4, 0x69, 0xed, 0x3a, 0x38, 0x3b, 0x30,
		0x85, 0x57, 0x1e, 0x00, 0xe9, 0xf3, 0x32, 0x4e, 0x9c, 0x3b,
		0x78, 0x69, 0xc9, 0x81, 0x87, 0xda, 0xdf, 0x40, 0x80, 0x8c,
		0x2f, 0x5d, 0x43, 0x31, 0xb6, 0xad, 0xe3, 0xe0, 0x37, 0xb8,
		0x58, 0x03, 0x8e, 0xbc, 0x74, 0x70, 0x40, 0xf5, 0x19, 0xd6,
		0x56, 0x1c, 0xa8, 0x5b, 0x6c, 0x2e, 0xbc, 0x83
	};
	/* same as msg but has one bit flipped */
	static const uint8_t bad_msg[128] = {
		0x2d, 0xfc, 0x5d, 0xbd, 0x44, 0x2a, 0xb6, 0x48, 0x1d, 0x6c,
		0xc7, 0xce, 0xa4, 0xcd, 0x01, 0x47, 0xff, 0xae, 0xd2, 0xbe,
		0x1d, 0x0a, 0xd5, 0xb2, 0x92, 0xfe, 0x46, 0xbb, 0xa2, 0x88,
		0xb8, 0x71, 0x9b, 0x8f, 0x0a, 0x89, 0x69, 0x23, 0x97, 0x41,
		0x64, 0x07, 0xad, 0xff, 0x6c, 0x6c, 0x41, 0x34, 0x38, 0x00,
		0xe0, 0x87, 0xeb, 0x27, 0xe9, 0x30, 0xe8, 0x88, 0xfa, 0xa1,
		0xe8, 0xcc, 0xa8, 0x6c, 0x4a, 0xa2, 0x73, 0x61, 0xaa, 0x07,
		0xf8, 0xf6, 0xb4, 0xc5, 0x69, 0xed, /**/
		0x3a, 0x38, 0x3b, 0x30, 0x85, 0x57, 0x1e, 0x00, 0xe9, 0xf3,
		0x32, 0x4e, 0x9c, 0x3b, 0x78, 0x69, 0xc9, 0x81, 0x87, 0xda,
		0xdf, 0x40, 0x80, 0x8c, 0x2f, 0x5d, 0x43, 0x31, 0xb6, 0xad,
		0xe3, 0xe0, 0x37, 0xb8, 0x58, 0x03, 0x8e, 0xbc, 0x74, 0x70,
		0x40, 0xf5, 0x19, 0xd6, 0x56, 0x1c, 0xa8, 0x5b, 0x6c, 0x2e,
		0xbc, 0x83
	};
	static const struct RSA rsa = {
		.e = 0x00010001,
		.N = { .dmax = sizeof(pub) / 4,
		       .d = (struct access_helper *)pub }
	};

	int passed;

	SHA256_hw_hash(msg, sizeof(msg), &digest);
	passed = DCRYPTO_rsa_verify(&rsa, digest.b8, sizeof(digest), sig,
				    sizeof(sig), PADDING_MODE_PKCS1,
				    HASH_SHA256);
	if (!passed)
		return false;
	SHA256_hw_hash(bad_msg, sizeof(bad_msg), &digest);

	/* now signature should fail */
	return !DCRYPTO_rsa_verify(&rsa, digest.b8, sizeof(digest), sig,
				   sizeof(sig), PADDING_MODE_PKCS1,
				   HASH_SHA256);
}
#endif

/* Call function using provided stack. */
static bool call_on_stack(void *new_stack, bool (*func)(void))
{
	bool result;
	/* Call whilst switching stacks */
	__asm__ volatile("mov r4, sp\n" /* save sp */
			 "mov sp, %[new_stack]\n"
			 "blx %[func]\n"
			 "mov sp, r4\n" /* restore sp */
			 "mov %[result], r0\n"
			 : [result] "=r"(result)
			 : [new_stack] "r"(new_stack),
			   [func] "r"(func)
			 : "r0", "r1", "r2", "r3", "r4",
			   "lr" /* clobbers */
	);
	return result;
}

/* Placeholder for SHA256 digest of module computed during build time. */
const struct sha256_digest fips_integrity
	__attribute__((section(".rodata.fips.checksum")));

static bool fips_self_integrity(void)
{
	struct sha256_digest digest;
	size_t module_length = &__fips_module_end - &__fips_module_start;

#ifdef CR50_DEV
	CPRINTS("FIPS self-integrity start %x, length %u",
		(uintptr_t)&__fips_module_start, module_length);
#endif
	SHA256_hw_hash(&__fips_module_start, module_length, &digest);

#ifdef CR50_DEV
	CPRINTS("Stored:   %ph",
		HEX_BUF(fips_integrity.b8, SHA256_DIGEST_SIZE));
	CPRINTS("Computed: %ph",
		HEX_BUF(digest.b8, SHA256_DIGEST_SIZE));
#endif

	return DCRYPTO_equals(fips_integrity.b8, digest.b8, sizeof(digest));
}

/* Duration of FIPS tests. */
uint64_t fips_last_kat_test_duration;

#define FIPS_KAT_STACK_SIZE 2048
void fips_power_up_tests(void)
{
	char *stack_buf;
	void *stack;
	uint64_t starttime;

	starttime = fips_vtable->get_time().val;

	if (!fips_self_integrity())
		_fips_status |= FIPS_FATAL_SELF_INTEGRITY;

	/* Make sure hardware is properly configured. */
	if (!DCRYPTO_ladder_is_enabled())
		_fips_status |= FIPS_FATAL_OTHER;

	/**
	 * Since we are very limited on stack and static RAM, acquire
	 * shared memory for KAT tests temporary larger stack.
	 */
	if (EC_SUCCESS ==
	    fips_vtable->shared_mem_acquire(FIPS_KAT_STACK_SIZE, &stack_buf)) {
		stack = stack_buf + FIPS_KAT_STACK_SIZE;
		if (!call_on_stack(stack, &fips_sha256_kat))
			_fips_status |= FIPS_FATAL_SHA256;
		if (!call_on_stack(stack, &fips_hmac_sha256_kat))
			_fips_status |= FIPS_FATAL_HMAC_SHA256;
		/**
		 * Since TRNG FIFO takes some time to fill in, we can mask
		 * latency by splitting TRNG tests in 2 halves, each
		 * 2048 bits. This saves 20 ms on start.
		 * first call to TRNG warm-up
		 */
		fips_trng_startup(0);
		if (!call_on_stack(stack, &fips_ecdsa_verify_kat))
			_fips_status |= FIPS_FATAL_ECDSA;

		if (!call_on_stack(stack, &fips_hmac_drbg_kat))
			_fips_status |= FIPS_FATAL_HMAC_DRBG;

#ifdef CONFIG_FIPS_AES_CBC_256
		if (!call_on_stack(stack, &fips_aes256_kat))
			_fips_status |= FIPS_FATAL_AES256;
#endif

#ifdef CONFIG_FIPS_RSA2048
		/* RSA KAT adds 30ms and not used for U2F */
		if (!call_on_stack(stack, &fips_rsa2048_verify_kat))
			_fips_status |= FIPS_FATAL_RSA2048;
#endif
		/**
		 * Grab the SHA hardware lock to force the following KATs to use
		 * the software implementation.
		 */
		if (!dcrypto_grab_sha_hw())
			_fips_status |= FIPS_FATAL_SHA256;

		if (!call_on_stack(stack, &fips_sha256_kat))
			_fips_status |= FIPS_FATAL_SHA256;
		if (!call_on_stack(stack, &fips_hmac_sha256_kat))
			_fips_status |= FIPS_FATAL_HMAC_SHA256;
#ifdef CONFIG_FIPS_SW_HMAC_DRBG
		/* SW HMAC DRBG adds 30ms and not used for U2F */
		if (!call_on_stack(stack, &fips_hmac_drbg_kat))
			_fips_status |= FIPS_FATAL_HMAC_DRBG;
#endif
		dcrypto_release_sha_hw();
		fips_vtable->shared_mem_release(stack_buf);

		/* Second call to TRNG warm-up. */
		fips_trng_startup(1);

		/* If no errors, set not to run tests on wake from sleep. */
		if (fips_is_no_crypto_error())
			fips_set_power_up(true);
#ifdef CONFIG_FLASH_LOG
		else /* write combined error to flash log */
			fips_vtable->flash_log_add_event(FE_LOG_FIPS_FAILURE,
							 sizeof(_fips_status),
							 &_fips_status);
#endif
		/* Set the bit that power-up tests completed, even if failed. */
		_fips_status |= FIPS_POWER_UP_TEST_DONE;
	} else
		_fips_status |= FIPS_FATAL_OTHER;

	fips_last_kat_test_duration = fips_vtable->get_time().val - starttime;
}

void fips_power_on(void)
{
	fips_last_kat_test_duration = -1ULL;
	/* make sure on power-on / resume it's cleared */
	_fips_status = FIPS_UNINITIALIZED;

	/**
	 * If this was a power-on or power-up tests weren't executed
	 * for some reason, run them now. Board FIPS KAT status will
	 * be updated by fips_power_up_tests() if all tests pass.
	 */
	if (!fips_is_power_up_done())
		fips_power_up_tests();
	else	/* tests were already completed before sleep */
		_fips_status |= FIPS_POWER_UP_TEST_DONE;

	/* Check if we can set FIPS-approved mode. */
	if (fips_crypto_allowed())
		fips_set_status(FIPS_MODE_ACTIVE);

}

const struct fips_vtable *fips_vtable;

/**
 * Check that given address is in same half of flash as FIPS code.
 * This rejects addresses in SRAM and provides additional security.
 */
static bool is_flash_address(const void *ptr)
{
	uintptr_t my_addr =
		(uintptr_t)is_flash_address - CONFIG_PROGRAM_MEMORY_BASE;
	uintptr_t offset = (uintptr_t)ptr - CONFIG_PROGRAM_MEMORY_BASE;

	if (my_addr >= CONFIG_RW_MEM_OFF &&
	    my_addr < CFG_TOP_A_OFF)
		return (offset >= CONFIG_RW_MEM_OFF) &&
		       (offset <= CFG_TOP_A_OFF);
	if (my_addr >= CONFIG_RW_B_MEM_OFF &&
		 my_addr < CFG_TOP_B_OFF)
		return (offset >= CONFIG_RW_B_MEM_OFF) &&
		       (offset <= CFG_TOP_B_OFF);

	/* Otherwise, we don't know what's going on, don't accept it. */
	return false;
}

void fips_set_callbacks(const struct fips_vtable *vtable)
{
	if (is_flash_address(vtable) &&
	    is_flash_address(vtable->shared_mem_acquire) &&
	    is_flash_address(vtable->shared_mem_release) &&
#ifdef CONFIG_FLASH_LOG
	    is_flash_address(vtable->flash_log_add_event) &&
#endif
#ifdef CONFIG_WATCHDOG
	    is_flash_address(vtable->watchdog_reload) &&
#endif
	    is_flash_address(vtable->get_time) &&
	    is_flash_address(vtable->task_enable_irq) &&
	    is_flash_address(vtable->task_wait_event_mask) &&
	    is_flash_address(vtable->task_set_event) &&
	    is_flash_address(vtable->task_get_current) &&
	    is_flash_address(vtable->task_start_irq_handler) &&
	    is_flash_address(vtable->task_resched_if_needed) &&
	    is_flash_address(vtable->mutex_lock) &&
	    is_flash_address(vtable->mutex_unlock))

		fips_vtable = vtable;
	else
		fips_vtable = NULL;
}
