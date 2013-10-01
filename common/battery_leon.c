/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery_pack.h"
#include "gpio.h"
#include "host_command.h"
#include "smart_battery.h"
#include "extpower.h"
#include "hooks.h"

#define SB_SHIP_MODE_ADDR	0x35
#define SB_SHIP_MODE_DATA	0x0004
#define SB_SHIP_RESET_DATA	0x0003

/* FIXME: We need REAL values for all this stuff */
const struct battery_temperature_ranges bat_temp_ranges = {
	.start_charging_min_c = 0,
	.start_charging_max_c = 50,
	.charging_min_c       = 0,
	.charging_max_c       = 60,
	.discharging_min_c    = -10,
	.discharging_max_c    = 40,
};

static const struct battery_info info = {

	.voltage_max    = 16800,
	.voltage_normal = 14800,
	.voltage_min    = 12000,

	/* Pre-charge values. */
	.precharge_current  = 256,	/* mA */
};

const struct battery_info *battery_get_info(void)
{
	return &info;
}

static void battery_leon_ac_change(void)
{
	if (extpower_is_present())
		sb_write(SB_SHIP_MODE_ADDR, SB_SHIP_RESET_DATA);
}
DECLARE_HOOK(HOOK_AC_CHANGE, battery_leon_ac_change, HOOK_PRIO_DEFAULT);

int battery_command_cut_off(struct host_cmd_handler_args *args)
{
	return sb_write(SB_SHIP_MODE_ADDR, SB_SHIP_MODE_DATA);
}
DECLARE_HOST_COMMAND(EC_CMD_BATTERY_CUT_OFF, battery_command_cut_off,
		     EC_VER_MASK(0));

/**
 * Physical detection of battery connection.
 */
int battery_is_connected(void)
{
	return (gpio_get_level(GPIO_BAT_DETECT_L) == 0);
}
