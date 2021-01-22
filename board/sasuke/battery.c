/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery_fuel_gauge.h"
#include "charge_state.h"
#include "common.h"

/*
 * Battery info for all sasuke battery types. Note that the fields
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
 *
 * Battery FET Status in Manufacture Access : bit15 & bit14
 *  b'00 - dfet : on / cfet : on
 *  b'01 - dfet : on / cfet : off
 *  b'10 - dfet : off / cfet : off
 *  b'11 - dfet : off / cfet : on
 *  The value b'10 is disconnect_val, so we can use b'01 for cfet_off_val
 */
const struct board_batt_params board_battery_info[] = {
	/* SDI Battery Information */
	[BATTERY_SDI] = {
		.fuel_gauge = {
			.manuf_name = "SDI",
			.device_name = "4432D53",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.mfgacc_support = 0,
				.reg_addr = 0x00,
				.reg_mask = 0xc000,
				.disconnect_val = 0x8000,
				.cfet_mask = 0xc000,
				.cfet_off_val = 0x2000,
			}
		},
		.batt_info = {
			.voltage_max            = 8860,
			.voltage_normal         = 7720, /* mV */
			.voltage_min            = 6000, /* mV */
			.precharge_current      = 200,  /* mA */
			.start_charging_min_c   = 0,
			.start_charging_max_c   = 45,
			.charging_min_c         = 0,
			.charging_max_c         = 55,
			.discharging_min_c      = -20,
			.discharging_max_c      = 70,
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_SDI;
