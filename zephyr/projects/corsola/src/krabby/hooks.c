/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>

#include <ap_power/ap_power.h>
#include "charger.h"
#include "driver/charger/rt9490.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"

static void board_i2c3_ctrl(bool enable)
{
	if (DEVICE_DT_GET(
		    DT_GPIO_CTLR_BY_IDX(DT_NODELABEL(i2c3), scl_gpios, 0)) ==
	    DEVICE_DT_GET(DT_NODELABEL(gpiof))) {
		/*
		 * TODO(b/226296649):
		 * Use pinctrl APIs to enable/disable an interface.
		 */
		struct gctrl_it8xxx2_regs *const gctrl_base =
			(struct gctrl_it8xxx2_regs *)DT_REG_ADDR(
				DT_NODELABEL(gctrl));

		if (enable) {
			gctrl_base->GCTRL_PMER3 |= IT8XXX2_GCTRL_SMB3PSEL;
		} else {
			gctrl_base->GCTRL_PMER3 &= ~IT8XXX2_GCTRL_SMB3PSEL;
		}
	}
}

static void board_enable_i2c3(void)
{
	board_i2c3_ctrl(1);
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, board_enable_i2c3, HOOK_PRIO_FIRST);

static void board_disable_i2c3(void)
{
	board_i2c3_ctrl(0);
}
DECLARE_HOOK(HOOK_CHIPSET_HARD_OFF, board_disable_i2c3, HOOK_PRIO_LAST);

static void board_suspend_handler(struct ap_power_ev_callback *cb,
				  struct ap_power_ev_data data)
{
	int value;

	switch (data.event) {
	default:
		return;

	case AP_POWER_RESUME:
		value = 1;
		break;

	case AP_POWER_SUSPEND:
		value = 0;
		break;
	}
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_5v_usm), value);
}

static int install_suspend_handler(const struct device *unused)
{
	static struct ap_power_ev_callback cb;

	/*
	 * Add a callback for suspend/resume.
	 */
	ap_power_ev_init_callback(&cb, board_suspend_handler,
				  AP_POWER_RESUME | AP_POWER_SUSPEND);
	ap_power_ev_add_callback(&cb);
	return 0;
}

SYS_INIT(install_suspend_handler, APPLICATION, 1);

static void board_hook_ac_change(void)
{
	if (system_get_board_version() >= 1) {
		rt9490_enable_adc(CHARGER_SOLO, extpower_is_present());
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, board_hook_ac_change, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_INIT, board_hook_ac_change, HOOK_PRIO_LAST);
