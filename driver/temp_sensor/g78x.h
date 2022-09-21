/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* G781/G782 temperature sensor module for Chrome EC */

#ifndef __CROS_EC_G78X_H
#define __CROS_EC_G78X_H

#if defined(CONFIG_TEMP_SENSOR_G781) && defined(CONFIG_TEMP_SENSOR_G782)
#error Cannot support both G781 and G782 together!
#endif

#define G78X_I2C_ADDR_FLAGS 0x4C

#define G78X_IDX_INTERNAL 0
#define G78X_IDX_EXTERNAL1 1
#define G78X_IDX_EXTERNAL2 2

#if defined(CONFIG_TEMP_SENSOR_G781)
/* G781 register */
#define G78X_TEMP_LOCAL 0x00
#define G78X_TEMP_REMOTE1 0x01
#define G78X_STATUS 0x02
#define G78X_CONFIGURATION_R 0x03
#define G78X_CONVERSION_RATE_R 0x04
#define G78X_LOCAL_TEMP_HIGH_LIMIT_R 0x05
#define G78X_LOCAL_TEMP_LOW_LIMIT_R 0x06
#define G78X_REMOTE1_TEMP_HIGH_LIMIT_R 0x07
#define G78X_REMOTE1_TEMP_LOW_LIMIT_R 0x08
#define G78X_CONFIGURATION_W 0x09
#define G78X_CONVERSION_RATE_W 0x0a
#define G78X_LOCAL_TEMP_HIGH_LIMIT_W 0x0b
#define G78X_LOCAL_TEMP_LOW_LIMIT_W 0x0c
#define G78X_REMOTE1_TEMP_HIGH_LIMIT_W 0x0d
#define G78X_REMOTE1_TEMP_LOW_LIMIT_W 0x0e
#define G78X_ONESHOT 0x0f
#define G78X_REMOTE1_TEMP_EXTENDED 0x10
#define G78X_REMOTE1_TEMP_OFFSET_HIGH 0x11
#define G78X_REMOTE1_TEMP_OFFSET_EXTD 0x12
#define G78X_REMOTE1_T_HIGH_LIMIT_EXTD 0x13
#define G78X_REMOTE1_T_LOW_LIMIT_EXTD 0x14
#define G78X_REMOTE1_TEMP_THERM_LIMIT 0x19
#define G78X_LOCAL_TEMP_THERM_LIMIT 0x20
#define G78X_THERM_HYSTERESIS 0x21
#define G78X_ALERT_FAULT_QUEUE_CODE 0x22
#define G78X_MANUFACTURER_ID 0xFE
#define G78X_DEVICE_ID 0xFF

/* Config register bits */
#define G78X_CONFIGURATION_STANDBY BIT(6)
#define G78X_CONFIGURATION_ALERT_MASK BIT(7)

/* Status register bits */
#define G78X_STATUS_LOCAL_TEMP_THERM_ALARM BIT(0)
#define G78X_STATUS_REMOTE1_TEMP_THERM_ALARM BIT(1)
#define G78X_STATUS_REMOTE1_TEMP_FAULT BIT(2)
#define G78X_STATUS_REMOTE1_TEMP_LOW_ALARM BIT(3)
#define G78X_STATUS_REMOTE1_TEMP_HIGH_ALARM BIT(4)
#define G78X_STATUS_LOCAL_TEMP_LOW_ALARM BIT(5)
#define G78X_STATUS_LOCAL_TEMP_HIGH_ALARM BIT(6)
#define G78X_STATUS_BUSY BIT(7)

#elif defined(CONFIG_TEMP_SENSOR_G782)
/* G782 register */
#define G78X_TEMP_LOCAL 0x00
#define G78X_TEMP_REMOTE1 0x01
#define G78X_TEMP_REMOTE2 0x02
#define G78X_STATUS 0x03
#define G78X_CONFIGURATION_R 0x04
#define G78X_CONFIGURATION_W 0x04
#define G78X_CONVERSION_RATE_R 0x05
#define G78X_CONVERSION_RATE_W 0x05
#define G78X_LOCAL_TEMP_HIGH_LIMIT_R 0x06
#define G78X_LOCAL_TEMP_HIGH_LIMIT_W 0x06
#define G78X_LOCAL_TEMP_LOW_LIMIT_R 0x07
#define G78X_LOCAL_TEMP_LOW_LIMIT_W 0x07
#define G78X_REMOTE1_TEMP_HIGH_LIMIT_R 0x08
#define G78X_REMOTE1_TEMP_HIGH_LIMIT_W 0x08
#define G78X_REMOTE1_TEMP_LOW_LIMIT_R 0x09
#define G78X_REMOTE1_TEMP_LOW_LIMIT_W 0x09
#define G78X_REMOTE2_TEMP_HIGH_LIMIT_R 0x0a
#define G78X_REMOTE2_TEMP_HIGH_LIMIT_W 0x0a
#define G78X_REMOTE2_TEMP_LOW_LIMIT_R 0x0b
#define G78X_REMOTE2_TEMP_LOW_LIMIT_W 0x0b
#define G78X_ONESHOT 0x0c
#define G78X_REMOTE1_TEMP_EXTENDED 0x0d
#define G78X_REMOTE1_TEMP_OFFSET_HIGH 0x0e
#define G78X_REMOTE1_TEMP_OFFSET_EXTD 0x0f
#define G78X_REMOTE1_T_HIGH_LIMIT_EXTD 0x10
#define G78X_REMOTE1_T_LOW_LIMIT_EXTD 0x11
#define G78X_REMOTE1_TEMP_THERM_LIMIT 0x12
#define G78X_REMOTE2_TEMP_EXTENDED 0x13
#define G78X_REMOTE2_TEMP_OFFSET_HIGH 0x14
#define G78X_REMOTE2_TEMP_OFFSET_EXTD 0x15
#define G78X_REMOTE2_T_HIGH_LIMIT_EXTD 0x16
#define G78X_REMOTE2_T_LOW_LIMIT_EXTD 0x17
#define G78X_REMOTE2_TEMP_THERM_LIMIT 0x18
#define G78X_STATUS1 0x19
#define G78X_LOCAL_TEMP_THERM_LIMIT 0x20
#define G78X_THERM_HYSTERESIS 0x21
#define G78X_ALERT_FAULT_QUEUE_CODE 0x22
#define G78X_MANUFACTURER_ID 0xFE
#define G78X_DEVICE_ID 0xFF

/* Config register bits */
#define G78X_CONFIGURATION_REMOTE2_DIS BIT(5)
#define G78X_CONFIGURATION_STANDBY BIT(6)
#define G78X_CONFIGURATION_ALERT_MASK BIT(7)

/* Status register bits */
#define G78X_STATUS_LOCAL_TEMP_LOW_ALARM BIT(0)
#define G78X_STATUS_LOCAL_TEMP_HIGH_ALARM BIT(1)
#define G78X_STATUS_LOCAL_TEMP_THERM_ALARM BIT(2)
#define G78X_STATUS_REMOTE2_TEMP_THERM_ALARM BIT(3)
#define G78X_STATUS_REMOTE1_TEMP_THERM_ALARM BIT(4)
#define G78X_STATUS_REMOTE2_TEMP_FAULT BIT(5)
#define G78X_STATUS_REMOTE1_TEMP_FAULT BIT(6)
#define G78X_STATUS_BUSY BIT(7)

/* Status1 register bits */
#define G78X_STATUS_REMOTE2_TEMP_LOW_ALARM BIT(4)
#define G78X_STATUS_REMOTE2_TEMP_HIGH_ALARM BIT(5)
#define G78X_STATUS_REMOTE1_TEMP_LOW_ALARM BIT(6)
#define G78X_STATUS_REMOTE1_TEMP_HIGH_ALARM BIT(7)
#endif

/**
 * Get the last polled value of a sensor.
 *
 * @param idx		Index to read. Idx indicates whether to read die
 *			temperature or external temperature.
 * @param temp_ptr	Destination for temperature in K.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int g78x_get_val(int idx, int *temp_ptr);

#endif /* __CROS_EC_G78X_H */
