/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Eve board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/*
 * By default, enable all console messages except ACPI and host event because
 * the sensor stack is generating a lot of activity.
 */
#define CC_DEFAULT (CC_ALL & ~(CC_MASK(CC_EVENTS) | CC_MASK(CC_LPC)))

/* EC */
#define CONFIG_ADC
#define CONFIG_BACKLIGHT_LID
#define CONFIG_BACKLIGHT_LID_ACTIVE_LOW
#define CONFIG_BOARD_VERSION_CBI
#define CONFIG_BOARD_FORCE_RESET_PIN
#define CONFIG_CRC8
#define CONFIG_CBI_EEPROM
#define CONFIG_CASE_CLOSED_DEBUG_EXTERNAL
#define CONFIG_DPTF
#define CONFIG_FLASH_SIZE_BYTES 0x80000
#define CONFIG_FPU
#define CONFIG_I2C
#define CONFIG_I2C_CONTROLLER

#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_KEYBOARD_KEYPAD
#define CONFIG_LED_COMMON
#define CONFIG_LID_SWITCH
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_LTO
#define CONFIG_CHIP_PANIC_BACKUP
#define CONFIG_PWM
#define CONFIG_SPI_FLASH_REGS
#define CONFIG_SPI_FLASH_W25X40
#define CONFIG_VBOOT_HASH
#define CONFIG_SHA256_UNROLLED
#define CONFIG_VOLUME_BUTTONS
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1
#define CONFIG_WATCHDOG_HELP
#define CONFIG_WIRELESS
#define CONFIG_WIRELESS_SUSPEND \
	(EC_WIRELESS_SWITCH_WLAN | EC_WIRELESS_SWITCH_WLAN_POWER)
#define WIRELESS_GPIO_WLAN_POWER GPIO_PP3300_DX_WLAN
#undef CONFIG_SUPPORT_CHIP_HIBERNATION
#define CONFIG_FANS 1
#undef CONFIG_FAN_INIT_SPEED
#define CONFIG_FAN_INIT_SPEED 50
#define CONFIG_FAN_DYNAMIC
#define CONFIG_THROTTLE_AP
#define CONFIG_PWM_KBLIGHT

/* EC console commands */
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO
#define CONFIG_CMD_BUTTON

/* Port80 */
#undef CONFIG_PORT80_HISTORY_LEN
#define CONFIG_PORT80_HISTORY_LEN 256

/* SOC */
#define CONFIG_CHIPSET_SKYLAKE
#define CONFIG_CHIPSET_HAS_PLATFORM_PMIC_RESET
#define CONFIG_CHIPSET_HAS_PRE_INIT_CALLBACK
#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_HOST_INTERFACE_ESPI
#define CONFIG_HOST_INTERFACE_ESPI_VW_SLP_S3
#define CONFIG_HOST_INTERFACE_ESPI_VW_SLP_S4
#define CONFIG_HOSTCMD_FLASH_SPI_INFO
#define GPIO_PG_EC_RSMRST_ODL GPIO_RSMRST_L_PGOOD

/* Battery */
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_HW_PRESENT_CUSTOM
#define CONFIG_BATTERY_DEVICE_CHEMISTRY "LION"
#define CONFIG_BATTERY_PRESENT_CUSTOM
#define CONFIG_BATTERY_SMART
#define CONFIG_PWR_STATE_DISCHARGE_FULL
#define CONFIG_BATTERY_REQUESTS_NIL_WHEN_DEAD
#undef CONFIG_BATT_HOST_FULL_FACTOR
#define CONFIG_BATT_HOST_FULL_FACTOR 100

/* Charger */
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGE_RAMP_HW /* This, or just RAMP? */
#define CONFIG_CHARGE_MANAGER_EXTERNAL_POWER_LIMIT
#define CONFIG_CHARGER
#define CONFIG_CHARGER_ISL9238
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_INPUT_CURRENT 512
#define CONFIG_CHARGER_MIN_INPUT_CURRENT_LIMIT 512
#define CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON 27000
#define CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON_WITH_BATT 15000
/* AP's thresholds. */
#define CONFIG_CHARGER_LIMIT_POWER_THRESH_BAT_PCT 3
#define CONFIG_CHARGER_LIMIT_POWER_THRESH_CHG_MW 27000
#define CONFIG_CHARGER_PROFILE_OVERRIDE
#define CONFIG_CHARGER_PSYS
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 20
#define CONFIG_CMD_CHARGER_ADC_AMON_BMON
#define CONFIG_HOSTCMD_PD_CONTROL
#define CONFIG_EXTPOWER_GPIO
#undef CONFIG_EXTPOWER_DEBOUNCE_MS
#define CONFIG_EXTPOWER_DEBOUNCE_MS 1000
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#undef CONFIG_POWER_BUTTON_INIT_TIMEOUT
#define CONFIG_POWER_BUTTON_INIT_TIMEOUT 6
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_SIGNAL_INTERRUPT_STORM_DETECT_THRESHOLD 30
#define CONFIG_POWER_S0IX
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE

/* Sensor */
#define CONFIG_TEMP_SENSOR
#define CONFIG_TEMP_SENSOR_F75303

#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_USE_HOST_EVENT
#define CONFIG_ACCELGYRO_BMI160
#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)
#define CONFIG_ACCELGYRO_BMI160_INT2_OUTPUT
#define CONFIG_ACCEL_BMA255
#define CONFIG_ACCEL_KX022
#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_DYNAMIC_MOTION_SENSOR_COUNT

/* KB backlight driver */
#define CONFIG_LED_DRIVER_LM3509

/* Enable sensor fifo, must also define the _SIZE and _THRES */
#define CONFIG_ACCEL_FIFO
/* FIFO size is in power of 2. */
#define CONFIG_ACCEL_FIFO_SIZE 512
/* Depends on how fast the AP boots and typical ODRs */
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO_SIZE / 3)

#define CONFIG_TABLET_MODE
#define CONFIG_TABLET_MODE_SWITCH
#define CONFIG_GMR_TABLET_MODE

/* USB */
#define CONFIG_USB_CHARGER
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_COMM_LOCKED
#define CONFIG_USB_PD_DISCHARGE_TCPC
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT TYPEC_RP_3A0
#define CONFIG_USB_PD_PORT_MAX_COUNT 2
#define CONFIG_USB_PD_MAX_TOTAL_SOURCE_CURRENT 4500
#define CONFIG_USB_PD_VBUS_DETECT_GPIO
#define CONFIG_USB_PD_TCPC_LOW_POWER
#define CONFIG_USB_PD_TCPM_MUX
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_TCPM_ANX7447
#define CONFIG_USB_PD_TCPM_ANX7447_OCM_ERASE_COMMAND
#define CONFIG_USB_PD_TCPM_PS8751
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_TCPMV1
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP
#define CONFIG_USB_MUX_RUNTIME_CONFIG

/* BC 1.2 charger */
#define CONFIG_BC12_DETECT_PI3USB9281
#define CONFIG_BC12_DETECT_PI3USB9281_CHIP_COUNT 2

/* Optional feature to configure npcx chip */
#define NPCX_UART_MODULE2 1 /* 1:GPIO64/65 as UART */
#define NPCX_JTAG_MODULE2 0 /* 0:GPIO21/17/16/20 as JTAG */
#define NPCX_TACH_SEL2 1 /* 0:GPIO40/73 1:GPIO93/A6 as TACH */

/* I2C ports */
#define I2C_PORT_TCPC0 NPCX_I2C_PORT0_0
#define I2C_PORT_TCPC1 NPCX_I2C_PORT0_1
#define I2C_PORT_USB_CHARGER_0 NPCX_I2C_PORT0_0
#define I2C_PORT_USB_CHARGER_1 NPCX_I2C_PORT0_1
#define I2C_PORT_EEPROM NPCX_I2C_PORT0_0
#define I2C_PORT_BATTERY NPCX_I2C_PORT1
#define I2C_PORT_CHARGER NPCX_I2C_PORT1
#define I2C_PORT_PMIC NPCX_I2C_PORT2
#define I2C_PORT_KBLIGHT NPCX_I2C_PORT2
#define I2C_PORT_GYRO NPCX_I2C_PORT3
#define I2C_PORT_ACCEL NPCX_I2C_PORT3
#define I2C_PORT_THERMAL NPCX_I2C_PORT3
#define I2C_PORT_ALS NPCX_I2C_PORT3

/* I2C addresses */
#define I2C_ADDR_MP2949_FLAGS 0x20
#define I2C_ADDR_EEPROM_FLAGS 0x50

#ifndef __ASSEMBLER__

/* support factory keyboard test */
#define CONFIG_KEYBOARD_FACTORY_TEST

#include "gpio_signal.h"
#include "registers.h"

enum temp_sensor_id {
	TEMP_SENSOR_LOCAL = 0,
	TEMP_SENSOR_REMOTE1,
	TEMP_SENSOR_REMOTE2,
	TEMP_SENSOR_COUNT,
};

/*
 * Motion sensors:
 * When reading through IO memory is set up for sensors (LPC is used),
 * the first 2 entries must be accelerometers, then gyroscope.
 * For BMI160, accel, gyro and compass sensors must be next to each other.
 */

enum sensor_id {
	LID_ACCEL = 0,
	BASE_ACCEL,
	BASE_GYRO,
	LID_ALS,
	SENSOR_COUNT,
};

enum adc_channel {
	ADC_BASE_DET,
	ADC_VBUS,
	ADC_AMON_BMON,
	ADC_CH_COUNT,
};

enum pwm_channel {
	PWM_CH_LED1,
	PWM_CH_LED2,
	PWM_CH_FAN,
	PWM_CH_KBLIGHT,
	/* Number of PWM channels */
	PWM_CH_COUNT,
};

enum fan_channel {
	FAN_CH_0 = 0,
	/* Number of FAN channels */
	FAN_CH_COUNT,
};

enum mft_channel {
	MFT_CH_0 = 0,
	/* Number of MFT channels */
	MFT_CH_COUNT,
};

enum oem_id {
	PROJECT_AKALI = 1,
	PROJECT_VAYNE = 3,
	PROJECT_SONA,
	PROJECT_PANTHEON,
	PROJECT_NAMI,
	PROJECT_COUNT,
};

enum model_id {
	/* Sona variants */
	MODEL_SYNDRA = 1,
	/* Akali variants */
	MODEL_EKKO = 1,
	MODEL_BARD = 2,
};

#define SKU_ID_MASK_KBLIGHT BIT(0)
#define SKU_ID_MASK_CONVERTIBLE BIT(9)
#define SKU_ID_MASK_KEYPAD BIT(15)
#define SKU_ID_MASK_UK2 BIT(18)

/* TODO(crosbug.com/p/61098): Verify the numbers below. */
/*
 * delay to turn on the power supply max is ~16ms.
 * delay to turn off the power supply max is about ~180ms.
 */
#define PD_POWER_SUPPLY_TURN_ON_DELAY 30000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 250000 /* us */

/* delay to turn on/off vconn */

/* Define typical operating power and max power */
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_POWER_MW 70000
#define PD_MAX_CURRENT_MA 3500
#define PD_MAX_VOLTAGE_MV 20000

/* Board specific handlers */
void board_reset_pd_mcu(void);
void board_set_tcpc_power_mode(int port, int mode);

/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK (BIT(LID_ACCEL) | BIT(LID_ALS))

/* These should be referenced only after  HOOK_INIT:HOOK_PRIO_INIT_I2C+1. */
extern uint16_t board_version;
extern uint8_t oem;
extern uint32_t sku;
extern uint8_t model;

/* SKU_ID[24:31] are dedicated to OEM customization */
#define CBI_SKU_CUSTOM_FIELD(val) ((val) >> 24)

void ccd_mode_isr(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
