/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Code supporting AP RO verification.
 */

#include "ap_ro_integrity_check.h"
#include "board_id.h"
#include "console.h"
#include "crypto_api.h"
#include "extension.h"
#include "flash.h"
#include "flash_info.h"
#include "cryptoc/sha256.h"
#include "stddef.h"
#include "stdint.h"
#include "timer.h"
#include "usb_spi.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

/* A flash range included in hash calculations. */
struct flash_range {
	uint32_t flash_offset;
	uint32_t range_size;
} __packed;

/*
 * A somewhat arbitrary maximum number of AP RO hash ranges to save. There are
 * 27 regions in a FMAP layout. The AP RO ranges should only be from the RO
 * region. It's unlikely anyone will need more than 32 ranges.
 * If there are AP RO hash issues, the team will likely need to look at the
 * value of each range what part of the FMAP it corresponds to. Enforce a limit
 * to the number of ranges, so it's easier to debug and to make people consider
 * why they would need more than 32 ranges.
 */
#define APRO_MAX_NUM_RANGES 32
/* Values used for sanity check of the flash_range structure fields. */
#define MAX_SUPPORTED_FLASH_SIZE (32 * 1024 * 1024)
#define MAX_SUPPORTED_RANGE_SIZE (4 * 1024 * 1024)

/* Page offset for H1 flash operations. */
static const uint32_t h1_flash_offset_ =
	AP_RO_DATA_SPACE_ADDR - CONFIG_PROGRAM_MEMORY_BASE;

/*
 * Payload of the vendor command communicating a variable number of flash
 * ranges to be checked and the total sha256.
 *
 * The actual number of ranges is determined based on the actual payload size.
 */
struct ap_ro_check_payload {
	uint8_t digest[SHA256_DIGEST_SIZE];
	struct flash_range ranges[0];
} __packed;

/* Version of the AP RO check information saved in the H1 flash page. */
#define AP_RO_HASH_LAYOUT_VERSION 0

/*
 * Header added for storing of the AP RO check information in the H1 flash
 * page. The checksum is a 4 byte truncated sha256 of the saved payload, just
 * a sanity check.
 */
struct ap_ro_check_header {
	uint16_t version;
	uint16_t num_ranges;
	uint32_t checksum;
};

/* Format of the AP RO check information saved in the H1 flash page. */
struct ap_ro_check {
	struct ap_ro_check_header header;
	struct ap_ro_check_payload payload;
};

/* Fixed pointer at the H1 flash page storing the AP RO check information. */
static const struct ap_ro_check *p_chk =
	(const struct ap_ro_check *)AP_RO_DATA_SPACE_ADDR;

/*
 * Track if the AP RO hash was validated this boot. Must be cleared every AP
 * reset.
 */
static uint8_t validated_ap_ro_boot;

void ap_ro_device_reset(void)
{
	if (validated_ap_ro_boot)
		CPRINTS("%s: clear validated state", __func__);
	validated_ap_ro_boot = 0;
}

static int ap_ro_erase_hash(void)
{
	int rv;

	/*
	 * TODO(vbendeb): Make this a partial erase, use refactored
	 * Board ID space partial erase.
	 */
	flash_open_ro_window(h1_flash_offset_, AP_RO_DATA_SPACE_SIZE);
	rv = flash_physical_erase(h1_flash_offset_, AP_RO_DATA_SPACE_SIZE);
	flash_close_ro_window();

	return rv;
}

static enum vendor_cmd_rc vc_seed_ap_ro_check(enum vendor_cmd_cc code,
					      void *buf, size_t input_size,
					      size_t *response_size)
{
	struct ap_ro_check_header check_header;
	const struct ap_ro_check_payload *vc_payload = buf;
	uint32_t vc_num_of_ranges;
	uint32_t i;
	uint8_t *response = buf;
	size_t prog_size;
	int rv;

	*response_size = 1; /* Just in case there is an error. */

	/* Neither write nor erase are allowed once Board ID is programmed. */
#ifndef CR50_DEV
	if (!board_id_is_erased()) {
		*response = ARCVE_BID_PROGRAMMED;
		return VENDOR_RC_NOT_ALLOWED;
	}
#endif

	if (input_size == 0) {
		/* Empty payload is a request to erase the hash. */
		if (ap_ro_erase_hash() != EC_SUCCESS) {
			*response = ARCVE_FLASH_ERASE_FAILED;
			return VENDOR_RC_INTERNAL_ERROR;
		}

		*response_size = 0;
		return EC_SUCCESS;
	}

	/* There should be at least one range and the hash. */
	if (input_size < (SHA256_DIGEST_SIZE + sizeof(struct flash_range))) {
		*response = ARCVE_TOO_SHORT;
		return VENDOR_RC_BOGUS_ARGS;
	}

	/* There should be an integer number of ranges. */
	if (((input_size - SHA256_DIGEST_SIZE) % sizeof(struct flash_range)) !=
	    0) {
		*response = ARCVE_BAD_PAYLOAD_SIZE;
		return VENDOR_RC_BOGUS_ARGS;
	}

	vc_num_of_ranges =
		(input_size - SHA256_DIGEST_SIZE) / sizeof(struct flash_range);

	if (vc_num_of_ranges > APRO_MAX_NUM_RANGES) {
		*response = ARCVE_TOO_MANY_RANGES;
		return VENDOR_RC_BOGUS_ARGS;
	}
	for (i = 0; i < vc_num_of_ranges; i++) {
		if (vc_payload->ranges[i].range_size >
		    MAX_SUPPORTED_RANGE_SIZE) {
			*response = ARCVE_BAD_RANGE_SIZE;
			return VENDOR_RC_BOGUS_ARGS;
		}
		if ((vc_payload->ranges[i].flash_offset +
		     vc_payload->ranges[i].range_size) >
		    MAX_SUPPORTED_FLASH_SIZE) {
			*response = ARCVE_BAD_OFFSET;
			return VENDOR_RC_BOGUS_ARGS;
		}
	}

	prog_size = sizeof(struct ap_ro_check_header) + input_size;
	for (i = 0; i < (prog_size / sizeof(uint32_t)); i++)
		if (((uint32_t *)p_chk)[i] != ~0) {
			*response = ARCVE_ALREADY_PROGRAMMED;
			return VENDOR_RC_NOT_ALLOWED;
		}

	check_header.version = AP_RO_HASH_LAYOUT_VERSION;
	check_header.num_ranges = vc_num_of_ranges;
	app_compute_hash(buf, input_size, &check_header.checksum,
			 sizeof(check_header.checksum));

	flash_open_ro_window(h1_flash_offset_, prog_size);
	rv = flash_physical_write(h1_flash_offset_, sizeof(check_header),
				  (char *)&check_header);
	if (rv == EC_SUCCESS)
		rv = flash_physical_write(h1_flash_offset_ +
						  sizeof(check_header),
					  input_size, buf);
	flash_close_ro_window();

	if (rv != EC_SUCCESS) {
		*response = ARCVE_FLASH_WRITE_FAILED;
		return VENDOR_RC_WRITE_FLASH_FAIL;
	}

	*response_size = 0;
	return VENDOR_RC_SUCCESS;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_SEED_AP_RO_CHECK, vc_seed_ap_ro_check);

static int verify_ap_ro_check_space(void)
{
	uint32_t checksum;
	size_t data_size;

	data_size = p_chk->header.num_ranges * sizeof(struct flash_range) +
		    sizeof(struct ap_ro_check_payload);
	if (data_size > CONFIG_FLASH_BANK_SIZE) {
		CPRINTS("%s: bogus number of ranges %d", __func__,
			p_chk->header.num_ranges);
		return EC_ERROR_CRC;
	}

	app_compute_hash(&p_chk->payload, data_size, &checksum,
			 sizeof(checksum));

	if (memcmp(&checksum, &p_chk->header.checksum, sizeof(checksum))) {
		CPRINTS("%s: AP RO Checksum corrupted", __func__);
		return EC_ERROR_CRC;
	}

	return EC_SUCCESS;
}

/*
 * ap_ro_check_unsupported: Returns non-zero value if AP RO verification is
 *                          unsupported.
 *
 * Returns:
 *
 *  ARCVE_OK if AP RO verification is supported.
 *  ARCVE_NOT_PROGRAMMED if the hash is not programmed.
 *  ARCVE_FLASH_READ_FAILED if there was an error reading the hash.
 *  ARCVE_BOARD_ID_BLOCKED if ap ro verification is disabled for the board's rlz
 */
static enum ap_ro_check_vc_errors ap_ro_check_unsupported(int add_flash_event)
{

	if (ap_ro_board_id_blocked()) {
		CPRINTS("%s: BID blocked", __func__);
		return ARCVE_BOARD_ID_BLOCKED;
	}

	if (p_chk->header.num_ranges == (uint16_t)~0) {
		CPRINTS("%s: RO verification not programmed", __func__);
		if (add_flash_event)
			ap_ro_add_flash_event(APROF_SPACE_NOT_PROGRAMMED);
		return ARCVE_NOT_PROGRAMMED;
	}

	/* Is the contents intact? */
	if (verify_ap_ro_check_space() != EC_SUCCESS) {
		CPRINTS("%s: unable to read ap ro space", __func__);
		if (add_flash_event)
			ap_ro_add_flash_event(APROF_SPACE_INVALID);
		return ARCVE_FLASH_READ_FAILED; /* No verification possible. */
	}
	return ARCVE_OK;
}

int validate_ap_ro(void)
{
	uint32_t i;
	HASH_CTX ctx;
	uint8_t digest[SHA256_DIGEST_SIZE];
	int rv;

	if (ap_ro_check_unsupported(true))
		return EC_ERROR_INVAL;

	enable_ap_spi_hash_shortcut();
	usb_spi_sha256_start(&ctx);
	for (i = 0; i < p_chk->header.num_ranges; i++) {
		CPRINTS("%s: %x:%x", __func__,
			p_chk->payload.ranges[i].flash_offset,
			p_chk->payload.ranges[i].range_size);
		/* Make sure the message gets out before verification starts. */
		cflush();
		usb_spi_sha256_update(&ctx,
				      p_chk->payload.ranges[i].flash_offset,
				      p_chk->payload.ranges[i].range_size);
	}

	usb_spi_sha256_final(&ctx, digest, sizeof(digest));
	if (memcmp(digest, p_chk->payload.digest, sizeof(digest))) {
		CPRINTS("AP RO verification FAILED!");
		CPRINTS("Calculated digest %ph",
			HEX_BUF(digest, sizeof(digest)));
		CPRINTS("Stored digest %ph",
			HEX_BUF(p_chk->payload.digest,
				sizeof(p_chk->payload.digest)));
		ap_ro_add_flash_event(APROF_CHECK_FAILED);
		rv = EC_ERROR_CRC;
	} else {
		ap_ro_add_flash_event(APROF_CHECK_SUCCEEDED);
		rv = EC_SUCCESS;
		validated_ap_ro_boot = 1;
		CPRINTS("AP RO verification SUCCEEDED!");
	}
	disable_ap_spi_hash_shortcut();

	return rv;
}

void ap_ro_add_flash_event(enum ap_ro_verification_ev event)
{
	struct ap_ro_entry_payload ev;

	ev.event = event;
	flash_log_add_event(FE_LOG_AP_RO_VERIFICATION, sizeof(ev), &ev);
}

static enum vendor_cmd_rc vc_get_ap_ro_hash(enum vendor_cmd_cc code,
					    void *buf, size_t input_size,
					    size_t *response_size)
{
	int rv;
	uint8_t *response = buf;

	*response_size = 0;
	if (input_size)
		return VENDOR_RC_BOGUS_ARGS;

	rv = ap_ro_check_unsupported(false);
	if (rv == ARCVE_FLASH_READ_FAILED)
		return VENDOR_RC_READ_FLASH_FAIL;
	if (rv) {
		*response_size = 1;
		*response = rv;
		return VENDOR_RC_INTERNAL_ERROR;
	}
	*response_size = SHA256_DIGEST_SIZE;
	memcpy(buf, p_chk->payload.digest, *response_size);

	return VENDOR_RC_SUCCESS;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_GET_AP_RO_HASH, vc_get_ap_ro_hash);

static int ap_ro_info_cmd(int argc, char **argv)
{
	int rv;
	int i;
#ifdef CR50_DEV
	int const max_args = 2;
#else
	int const max_args = 1;
#endif

	if (argc > max_args)
		return EC_ERROR_PARAM_COUNT;
#ifdef CR50_DEV
	if (argc == max_args) {
		if (strcasecmp(argv[1], "erase"))
			return EC_ERROR_PARAM1;
		ap_ro_erase_hash();
	}
#endif
	rv = ap_ro_check_unsupported(false);
	if (rv == ARCVE_FLASH_READ_FAILED)
		return EC_ERROR_CRC; /* No verification possible. */
	/* All other AP RO verificaiton unsupported reasons are fine */
	if (rv)
		return EC_SUCCESS;

	ccprintf("boot validated: %s\n", validated_ap_ro_boot ? "yes" : "no");
	ccprintf("sha256 hash %ph\n",
		 HEX_BUF(p_chk->payload.digest, sizeof(p_chk->payload.digest)));
	ccprintf("Covered ranges:\n");
	for (i = 0; i < p_chk->header.num_ranges; i++) {
		ccprintf("%08x...%08x\n", p_chk->payload.ranges[i].flash_offset,
			 p_chk->payload.ranges[i].flash_offset +
				 p_chk->payload.ranges[i].range_size - 1);
		cflush();
	}

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(ap_ro_info, ap_ro_info_cmd,
#ifdef CR50_DEV
			     "[erase]", "Display or erase AP RO check space"
#else
			     "", "Display AP RO check space"
#endif
);

static enum vendor_cmd_rc vc_get_ap_ro_status(enum vendor_cmd_cc code,
					      void *buf, size_t input_size,
					      size_t *response_size)
{
	uint8_t rv = AP_RO_NOT_RUN;
	uint8_t *response = buf;

	CPRINTS("Check AP RO status");

	*response_size = 0;
	if (input_size)
		return VENDOR_RC_BOGUS_ARGS;

	if (ap_ro_check_unsupported(false))
		rv = AP_RO_UNSUPPORTED;
	else if (ec_rst_override())
		rv = AP_RO_FAIL;
	else if (validated_ap_ro_boot)
		rv = AP_RO_PASS;

	*response_size = 1;
	response[0] = rv;
	return VENDOR_RC_SUCCESS;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_GET_AP_RO_STATUS, vc_get_ap_ro_status);
