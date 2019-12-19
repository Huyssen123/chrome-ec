/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Strago board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/*
 * By default, enable all console messages excepted HC, ACPI and event:
 * The sensor stack is generating a lot of activity.
 */
#define CC_DEFAULT     (CC_ALL & ~(CC_MASK(CC_EVENTS) | CC_MASK(CC_LPC)))
/* By default, set hcdebug to off */
#undef CONFIG_HOSTCMD_DEBUG_MODE
#define CONFIG_HOSTCMD_DEBUG_MODE HCDEBUG_OFF

/* Optional features */
#define CONFIG_WATCHDOG_HELP
#define CONFIG_CLOCK_CRYSTAL
#define CONFIG_CHIPSET_BRASWELL
#define CONFIG_SCI_GPIO GPIO_PCH_SCI_L

#define CONFIG_BOARD_VERSION

#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_IRQ_GPIO GPIO_KBD_IRQ_L
#undef CONFIG_KEYBOARD_KSO_BASE
#define CONFIG_KEYBOARD_KSO_BASE 4 /* KSO starts from KSO04 */
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_LID_SWITCH
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_LOW_POWER_PSEUDO_G3
#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_USE_HOST_EVENT
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_SHUTDOWN_PAUSE_IN_S5
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_VBOOT_HASH
#define CONFIG_PORT80_TASK_EN
#define CONFIG_WIRELESS

#define CONFIG_SPI_PORT 1
#define CONFIG_SPI_CS_GPIO GPIO_PVT_CS0
#define CONFIG_SPI_FLASH
#define CONFIG_SPI_FLASH_SIZE 524288
#define CONFIG_SPI_FLASH_W25X40

#define CONFIG_USB_PORT_POWER_SMART
#define CONFIG_USB_PORT_POWER_SMART_SIMPLE

#define CONFIG_TEMP_SENSOR
#define CONFIG_TEMP_SENSOR_TMP432

#define CONFIG_PMIC

#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_PRESENT_GPIO GPIO_BAT_PRESENT_L
#define CONFIG_BATTERY_SMART
#define CONFIG_BATTERY_VENDOR_PARAM
#define CONFIG_CHARGER
#define CONFIG_CHARGER_V2
#define CONFIG_CHARGER_BQ24770
#define CONFIG_CHARGER_ILIM_PIN_DISABLED
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10
#define CONFIG_CHARGER_INPUT_CURRENT 2040
#define CONFIG_CHARGER_DISCHARGE_ON_AC

#define CONFIG_PWM
#define CONFIG_PWM_DSLEEP
#define CONFIG_LED_COMMON

#define CONFIG_I2C

/* Accelerometer */
#define CONFIG_ACCEL_KXCJ9
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO

/* Lower maximal ODR to 100Hz */
#define CONFIG_EC_MAX_SENSOR_FREQ_MILLIHZ 100000

/* Depends on how fast the AP boots and typical ODRs */
#define CONFIG_ACCEL_FIFO 512
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO / 3)

#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE

/* Wireless signals */
#define WIRELESS_GPIO_WLAN	GPIO_WLAN_OFF_L

/* Number of buttons */
#define CONFIG_BUTTON_COUNT		2

#undef DEFERRABLE_MAX_COUNT
#define DEFERRABLE_MAX_COUNT	10

/* Modules we want to exclude */
#undef CONFIG_EEPROM
#undef CONFIG_EOPTION
#undef CONFIG_PSTORE
#undef CONFIG_PECI
#undef CONFIG_FANS
#undef CONFIG_ADC
#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* I2C ports */
#define I2C_PORT_BATTERY	MEC1322_I2C0_0
#define I2C_PORT_CHARGER	MEC1322_I2C0_0
#define I2C_PORT_ACCEL		MEC1322_I2C2
#define I2C_PORT_THERMAL	MEC1322_I2C3

/* battery firmware update */
#define CONFIG_CRC8
#define CONFIG_SB_FIRMWARE_UPDATE
#define CONFIG_SMBUS

/* ADC signal */
enum adc_channel {
	/* Number of ADC channels */
	ADC_CH_COUNT
};

/* Sensor index definition */
enum sensor_id {
	BASE_ACCEL = 0,
	LID_ACCEL = 1,
};

/*
 * We have not enabled the sensor FIFO on the accels, so we force the EC
 * to collect at every sample.
 */
#define CONFIG_ACCEL_FORCE_MODE_MASK \
	((1 << BASE_ACCEL) | (1 << LID_ACCEL))

#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL

/* power signal definitions */
enum power_signal {
	X86_ALL_SYS_PWRGD = 0,
	X86_RSMRST_L_PWRGD,
	X86_SLP_S3_DEASSERTED,
	X86_SLP_S4_DEASSERTED,

	/* Number of X86 signals */
	POWER_SIGNAL_COUNT
};

enum pwm_channel {
	PWM_CH_LED_GREEN,
	PWM_CH_LED_BLUE,
	PWM_CH_LED_RED,
	/* Number of PWM channels */
	PWM_CH_COUNT
};

enum temp_sensor_id {
	/* TMP432 local and remote sensors */
	TEMP_SENSOR_I2C_TMP432_LOCAL,
	TEMP_SENSOR_I2C_TMP432_REMOTE1,
	TEMP_SENSOR_I2C_TMP432_REMOTE2,

	/* Battery temperature sensor */
	TEMP_SENSOR_BATTERY,

	TEMP_SENSOR_COUNT
};

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
