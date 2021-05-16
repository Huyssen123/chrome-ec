/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr.h>
#include <power/power.h>
#include <soc.h>

#include "console.h"
#include "system.h"

static const struct pm_state_info pm_min_residency[] =
	PM_STATE_INFO_DT_ITEMS_LIST(DT_NODELABEL(cpu0));

/* CROS PM policy handler */
struct pm_state_info pm_policy_next_state(int32_t ticks)
{
	/* Deep sleep is allowed and console is not in use. */
	if (DEEP_SLEEP_ALLOWED != 0 && !npcx_power_console_is_in_use()) {
		for (int i = ARRAY_SIZE(pm_min_residency) - 1; i >= 0; i--) {
			/* Find suitable power state by residency time */
			if (ticks == K_TICKS_FOREVER ||
			    ticks >= k_us_to_ticks_ceil32(
					     pm_min_residency[i]
						     .min_residency_us)) {
				return pm_min_residency[i];
			}
		}
	}

	return (struct pm_state_info){ PM_STATE_ACTIVE, 0, 0 };
}

/* CROS PM device policy handler */
bool pm_policy_low_power_devices(enum pm_state state)
{
	return pm_is_sleep_state(state);
}
