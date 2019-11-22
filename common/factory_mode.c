/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* CCD factory enable */

#include "ccd_config.h"
#include "console.h"
#include "extension.h"
#include "hooks.h"
#include "system.h"
#include "tpm_registers.h"
#include "tpm_vendor_cmds.h"

#define CPRINTS(format, args...) cprints(CC_CCD, format, ## args)

static uint8_t ccd_hook_active;

static void ccd_config_changed(void)
{
	if (!ccd_hook_active)
		return;

	CPRINTS("%s: saved, rebooting\n", __func__);
	cflush();
	system_reset(SYSTEM_RESET_HARD);
}
DECLARE_HOOK(HOOK_CCD_CHANGE, ccd_config_changed, HOOK_PRIO_LAST);

static void factory_enable_failed(void)
{
	ccd_hook_active = 0;
	CPRINTS("factory enable failed");
	deassert_ec_rst();
}
DECLARE_DEFERRED(factory_enable_failed);

/* The below time constants are way longer than should be required in practice:
 *
 * Time it takes to finish processing TPM command
 */
#define TPM_PROCESSING_TIME (1 * SECOND)

/*
 * Time it takse TPM reset function to wipe out the NVMEM and reboot the
 * device.
 */
#define TPM_RESET_TIME (10 * SECOND)

/* Total time deep sleep should not be allowed. */
#define DISABLE_SLEEP_TIME (TPM_PROCESSING_TIME + TPM_RESET_TIME)

static void factory_enable_deferred(void)
{
	int rv;

	CPRINTS("%s: reset TPM\n", __func__);

	/*
	 * Let's make sure the rest of the system is out of the way while TPM
	 * is being wiped out.
	 */
	assert_ec_rst();

	if (tpm_reset_request(1, 1) != EC_SUCCESS) {
		CPRINTS("%s: TPM reset failed\n", __func__);
		deassert_ec_rst();
		return;
	}

	tpm_reinstate_nvmem_commits();

	CPRINTS("%s: TPM reset done, enabling factory mode\n", __func__);

	ccd_hook_active = 1;
	rv = ccd_reset_config(CCD_RESET_FACTORY);
	if (rv != EC_SUCCESS)
		factory_enable_failed();

	/*
	 * Make sure we never end up with the EC held in reset, no matter what
	 * prevents the proper factory reset flow from succeeding.
	 */
	hook_call_deferred(&factory_enable_failed_data, TPM_RESET_TIME);
}
DECLARE_DEFERRED(factory_enable_deferred);

void enable_ccd_factory_mode(void)
{
	delay_sleep_by(DISABLE_SLEEP_TIME);
	hook_call_deferred(&factory_enable_deferred_data,
		TPM_PROCESSING_TIME);
}
