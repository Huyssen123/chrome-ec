/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 */
#ifndef __CR50_INCLUDE_AP_RO_INTEGRITY_CHECK_H
#define __CR50_INCLUDE_AP_RO_INTEGRITY_CHECK_H

#include "flash_log.h"

enum ap_ro_status {
	AP_RO_NOT_RUN = 0,
	AP_RO_PASS_UNVERIFIED_GBB,
	AP_RO_FAIL,
	AP_RO_UNSUPPORTED_UNKNOWN, /* Deprecated */
	AP_RO_UNSUPPORTED_NOT_TRIGGERED,
	AP_RO_UNSUPPORTED_TRIGGERED,
	AP_RO_PASS,
	AP_RO_FAIL_CLEARED,
	AP_RO_IN_PROGRESS,
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

#endif /* ! __CR50_INCLUDE_AP_RO_INTEGRITY_CHECK_H */
