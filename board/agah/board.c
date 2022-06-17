/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "button.h"
#include "charge_ramp.h"
#include "charger.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "fw_config.h"
#include "hooks.h"
#include "lid_switch.h"
#include "power_button.h"
#include "power.h"
#include "registers.h"
#include "switch.h"
#include "throttle_ap.h"
#include "usbc_config.h"

#include "gpio_list.h" /* Must come after other header files. */

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

__override void board_cbi_init(void)
{
}

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
	/* Allow keyboard backlight to be enabled */
	gpio_set_level(GPIO_EC_KB_BL_EN, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	/* Turn off the keyboard backlight if it's on. */
	gpio_set_level(GPIO_EC_KB_BL_EN, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

static void board_init(void)
{
	gpio_enable_interrupt(GPIO_PG_PP3300_S5_OD);
	gpio_enable_interrupt(GPIO_BJ_ADP_PRESENT_ODL);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/**
 * Deferred function to handle GPIO PG_PP3300_S5_OD change
 */
static void bypass_pp3300_s5_deferred(void)
{
	int pg_pp3300_s5 = gpio_get_level(GPIO_PG_PP3300_S5_OD);

	gpio_set_level(GPIO_PG_PP3300_S5_EC_SEQ_OD, pg_pp3300_s5);
}
DECLARE_DEFERRED(bypass_pp3300_s5_deferred);

void board_power_interrupt(enum gpio_signal signal)
{
	/* Trigger deferred notification of gpio PG_PP3300_S5_OD change */
	hook_call_deferred(&bypass_pp3300_s5_deferred_data, 0);
}
