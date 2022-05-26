/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "common.h"
#include "compile_time_macros.h"
#include "gpio.h"
#include "gpio_signal.h"

/*
 * Battery info for all Ghost battery types. Note that the fields
 * start_charging_min/max and charging_min/max are not used for the charger.
 * The effective temperature limits are given by discharging_min/max_c.
 *
 * Fuel Gauge (FG) parameters which are used for determining if the battery
 * is connected, the appropriate ship mode (battery cutoff) command, and the
 * charge/discharge FETs status.
 *
 * Ship mode (battery cutoff) requires 2 writes to the appropriate smart battery
 * register. For some batteries, the charge/discharge FET bits are set when
 * charging/discharging is active, in other types, these bits set mean that
 * charging/discharging is disabled. Therefore, in addition to the mask for
 * these bits, a disconnect value must be specified. Note that for TI fuel
 * gauge, the charge/discharge FET status is found in Operation Status (0x54),
 * but a read of Manufacturer Access (0x00) will return the lower 16 bits of
 * Operation status which contains the FET status bits.
 *
 * The assumption for battery types supported is that the charge/discharge FET
 * status can be read with a sb_read() command and therefore, only the register
 * address, mask, and disconnect value need to be provided.
 */
const struct board_batt_params board_battery_info[] = {
	/* POW-TECH GQA05 Battery Information */
	[BATTERY_POWER_TECH] = {
		/* BQ40Z50 Fuel Gauge */
		.fuel_gauge = {
			.manuf_name = "POW-TECH",
			.device_name = "BATGQA05L22",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.mfgacc_support = 1,
				.reg_addr = 0x00,
				.reg_mask = 0x2000,		/* XDSG */
				.disconnect_val = 0x2000,
			}
		},
		.batt_info = {
			.voltage_max		= TARGET_WITH_MARGIN(13050, 5),
			.voltage_normal		= 11400, /* mV */
			.voltage_min		= 9000, /* mV */
			.precharge_current	= 280,	/* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 45,
			.charging_min_c		= 0,
			.charging_max_c		= 45,
			.discharging_min_c	= -10,
			.discharging_max_c	= 60,
		},
	},
	/*
	 * TODO(b/233120385): verify these
	 */
	[BATTERY_SWD_ATL] = {
		/* BQ40Z50-R3 Fuel Gauge */
		.fuel_gauge = {
			.manuf_name = "SWD",
			.device_name = "1163985013",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.mfgacc_support = 1,
				.reg_addr = 0x00,
				.reg_mask = 0x2000,		/* XDSG */
				.disconnect_val = 0x2000,
			}
		},
		.batt_info = {
			.voltage_max		= TARGET_WITH_MARGIN(8960, 5),
			.voltage_normal		= 7780, /* mV */
			.voltage_min		= 6000, /* mV */
			.precharge_current	= 570,	/* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 60,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= -20,
			.discharging_max_c	= 60,
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_POWER_TECH;

enum battery_present battery_hw_present(void)
{
	enum gpio_signal batt_pres;

	batt_pres = GPIO_EC_BATT_PRES_ODL;

	/* The GPIO is low when the battery is physically present */
	return gpio_get_level(batt_pres) ? BP_NO : BP_YES;
}
