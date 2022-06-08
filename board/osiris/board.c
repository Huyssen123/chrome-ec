/* Copyright 2022 The Chromium OS Authors. All rights reserved.
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
#include "tablet_mode.h"
#include "throttle_ap.h"
#include "usbc_config.h"

#include "gpio_list.h" /* Must come after other header files. */

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
	gpio_set_level(GPIO_AMP_PWR_EN, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	gpio_set_level(GPIO_AMP_PWR_EN, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

__override void board_kblight_shutdown(void)
{
	/* turn off keyboard backlight power */
	gpio_set_level(GPIO_KYBL_EN, 0);
}

__override void board_kblight_init(void)
{
	/* turn on keyboard backlight power */
	gpio_set_level(GPIO_KYBL_EN, 1);
	gpio_set_level(GPIO_EC_KB_BL_EN_L, 1);
	msleep(10);
}
