/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include "accelgyro.h"
#include "button.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/accelgyro_bmi323.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "motionsense_sensors.h"
#include "motion_sense.h"
#include "tablet_mode.h"

#include "nissa_common.h"

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

static bool use_alt_sensor;

void motion_interrupt(enum gpio_signal signal)
{
	if (use_alt_sensor)
		lsm6dsm_interrupt(signal);
	else
		bmi3xx_interrupt(signal);
}

static void sensor_init(void)
{
	int ret;
	uint32_t val;
	/* check which base sensor is used for motion_interrupt */
	use_alt_sensor = cros_cbi_ssfc_check_match(
		CBI_SSFC_VALUE_ID(DT_NODELABEL(base_sensor_1)));

	motion_sensors_check_ssfc();

	/* Check if it's tablet or not */
	ret = cros_cbi_get_fw_config(FW_TABLET, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_TABLET);
		return;
	}
	if (val == FW_TABLET_NOT_PRESENT) {
		LOG_INF("Clamshell: disable motionsense function.");
		motion_sensor_count = 0;
		gmr_tablet_switch_disable();
		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_imu_int_l),
				      GPIO_DISCONNECTED);

		LOG_INF("Clamshell: disable volume button function.");
		button_disable_gpio(BUTTON_VOLUME_UP);
		button_disable_gpio(BUTTON_VOLUME_DOWN);
	} else {
		LOG_INF("Tablet: Enable motionsense function.");
	}
}
DECLARE_HOOK(HOOK_INIT, sensor_init, HOOK_PRIO_POST_I2C);
