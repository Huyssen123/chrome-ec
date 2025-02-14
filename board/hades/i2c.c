/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "compile_time_macros.h"
#include "hooks.h"
#include "i2c.h"

#define BOARD_ID_FAST_PLUS_CAPABLE 2

/* I2C port map configuration */
const struct i2c_port_t i2c_ports[] = {
	{
		/* I2C1 */
		.name = "tcpc0",
		.port = I2C_PORT_USB_C0_TCPC,
		.kbps = 400,
		.scl = GPIO_EC_I2C_USB_C0_SCL,
		.sda = GPIO_EC_I2C_USB_C0_SDA,
	},
	{
		/* I2C2 */
		.name = "tcpc1",
		.port = I2C_PORT_USB_C1_TCPC,
		.kbps = 400,
		.scl = GPIO_EC_I2C_USB_C1_SCL,
		.sda = GPIO_EC_I2C_USB_C1_SDA,
	},
	{
		/* I2C5 */
		.name = "battery",
		.port = I2C_PORT_BATTERY,
		.kbps = 400,
		.scl = GPIO_EC_I2C_BAT_SCL,
		.sda = GPIO_EC_I2C_BAT_SDA,
	},
	{
		/* I2C7 */
		.name = "misc",
		.port = I2C_PORT_MISC,
		.kbps = 400,
		.scl = GPIO_EC_I2C_MISC_SCL_R,
		.sda = GPIO_EC_I2C_MISC_SDA_R,
	},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);
