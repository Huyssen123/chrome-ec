/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/tcpm/anx7447_public.h"

#include <zephyr/devicetree.h>

#define ANX7447_EMUL_COMPAT cros_anx7447_tcpc_emul

#define TCPC_CONFIG_ANX7447_EMUL(id) \
	{                                                                      \
		.bus_type = EC_BUS_TYPE_I2C,                                   \
		.i2c_info = {                                                  \
			.port = I2C_PORT_BY_DEV(id),                           \
			.addr_flags = DT_REG_ADDR(id),                         \
		},                                                             \
		.drv = &anx7447_tcpm_drv,                                      \
		.alert_signal = COND_CODE_1(DT_NODE_HAS_PROP(id, alert_gpio),  \
			(GPIO_SIGNAL(DT_PHANDLE(id, alert_gpio))),             \
			(GPIO_LIMIT)),                                         \
	},
