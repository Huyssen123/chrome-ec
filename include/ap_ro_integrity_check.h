/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 */
#ifndef __CR50_INCLUDE_AP_RO_INTEGRITY_CHECK_H
#define __CR50_INCLUDE_AP_RO_INTEGRITY_CHECK_H

#include "flash_log.h"

enum ap_ro_status {
	AP_RO_NOT_RUN = 0,
	AP_RO_PASS_UNVERIFIED_GBB = 1,
	AP_RO_FAIL = 2,
	AP_RO_UNSUPPORTED_UNKNOWN = 3, /* Deprecated */
	AP_RO_UNSUPPORTED_NOT_TRIGGERED = 4,
	AP_RO_UNSUPPORTED_TRIGGERED = 5,
	AP_RO_PASS = 6,
	AP_RO_IN_PROGRESS = 7,
};
/*
 * validate_ap_ro: based on information saved in an H1 RO flash page verify
 * contents of the AP flash. Hold the EC in reset if verification fails.
 */
void validate_ap_ro(void);

/*
 * ap_ro_add_flash_event: add a flash log event to keep track of AP RO
 *       verification attempt progress.
 */
void ap_ro_add_flash_event(enum ap_ro_verification_ev event);

/*
 * ap_ro_board_id_blocked: Returns True if AP RO verification is disabled for
 *       the board's RLZ.
 */
int ap_ro_board_id_blocked(void);

/*
 * ap_ro_device_reset: Clear AP RO verification state on a new boot.
 */
void ap_ro_device_reset(void);

/*
 * Clear the AP RO result and release the EC from reset. This should only be
 * done through a key combo.
 */
void ap_ro_clear_ec_rst_override(void);

#endif /* ! __CR50_INCLUDE_AP_RO_INTEGRITY_CHECK_H */
