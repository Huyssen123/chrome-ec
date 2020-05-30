/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Asurada board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "button.h"
#include "charge_manager.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/charger/isl923x.h"
#include "driver/ppc/syv682x.h"
#include "driver/tcpm/it83xx_pd.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "motion_sense.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "switch.h"
#include "tablet_mode.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

static void ppc_interrupt(enum gpio_signal signal);
static void x_ec_interrupt(enum gpio_signal signal);

#include "gpio_list.h"

const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};
const unsigned int chg_cnt = ARRAY_SIZE(chg_chips);

/*
 * PWM channels. Must be in the exactly same order as in enum pwm_channel.
 * There total three 16 bits clock prescaler registers for all pwm channels,
 * so use the same frequency and prescaler register setting is required if
 * number of pwm channel greater than three.
 */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_PWRLED] = {
		.channel = 0,
		.flags = PWM_CONFIG_DSLEEP | PWM_CONFIG_ACTIVE_LOW,
		.freq_hz = 500,
		.pcfsr_sel = PWM_PRESCALER_C4
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* Wake-up pins for hibernate */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_AC_PRESENT,
	GPIO_LID_OPEN,
	GPIO_POWER_BUTTON_L,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_PMIC_EC_PWRGD, POWER_SIGNAL_ACTIVE_HIGH, "PMIC_PWR_GOOD"},
	{GPIO_AP_IN_SLEEP_L, POWER_SIGNAL_ACTIVE_LOW, "AP_IN_S3_L"},
	{GPIO_AP_EC_WATCHDOG_L,
	  POWER_SIGNAL_ACTIVE_LOW | POWER_SIGNAL_DISABLE_AT_BOOT,
	  "AP_WDT_ASSERTED"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* Detect subboard */
static enum board_sub_board board_get_sub_board(void);

/* Initialize board. */
static void board_init(void)
{
	/* For Rev0 only. Set GPM0~6 1.8V input. */
	IT83XX_GPIO_GCR30 |= BIT(4);

	/* Set PWM of PWRLED to 5%. */
	pwm_set_duty(PWM_CH_PWRLED, 5);
	pwm_enable(PWM_CH_PWRLED, 1);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static void board_tcpc_init(void)
{
	gpio_enable_interrupt(GPIO_USB_C0_PPC_INT_ODL);
	if (board_get_sub_board() == SUB_BOARD_TYPEC)
		gpio_enable_interrupt(GPIO_USB_C1_PPC_INT_ODL);
}
/* Must be done after I2C and subboard */
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_I2C + 1);

/* ADC channels. Must be in the exactly same order as in enum adc_channel. */
const struct adc_t adc_channels[] = {
	/* Convert to mV (3000mV/1024). */
	{"TEMP_SENSOR_SUBPMIC", 3000, 1024, 0, CHIP_ADC_CH0},
	{"BOARD_ID_0", 3000, 1024, 0, CHIP_ADC_CH1},
	{"BOARD_ID_1", 3000, 1024, 0, CHIP_ADC_CH2},
	{"TEMP_SENSOR_AMB", 3000, 1024, 0, CHIP_ADC_CH3},
	{"TEMP_SENSOR_CHARGER", 3000, 1024, 0, CHIP_ADC_CH5},
	{"CHARGER_PMON", 3000, 1024, 0, CHIP_ADC_CH6},
	{"TEMP_SENSOR_AP", 3000, 1024, 0, CHIP_ADC_CH7},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* Keyboard scan setting */
struct keyboard_scan_config keyscan_config = {
	.output_settle_us = 35,
	.debounce_down_us = 5 * MSEC,
	.debounce_up_us = 40 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0x14, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};

/*
 * I2C channels (A, B, and C) are using the same timing registers (00h~07h)
 * at default.
 * In order to set frequency independently for each channels,
 * We use timing registers 09h~0Bh, and the supported frequency will be:
 * 50KHz, 100KHz, 400KHz, or 1MHz.
 * I2C channels (D, E and F) can be set different frequency on different ports.
 * The I2C(D/E/F) frequency depend on the frequency of SMBus Module and
 * the individual prescale register.
 * The frequency of SMBus module is 24MHz on default.
 * The allowed range of I2C(D/E/F) frequency is as following setting.
 * SMBus Module Freq = PLL_CLOCK / ((IT83XX_ECPM_SCDCR2 & 0x0F) + 1)
 * (SMBus Module Freq / 510) <=  I2C Freq <= (SMBus Module Freq / 8)
 * Channel D has multi-function and can be used as UART interface.
 * Channel F is reserved for EC debug.
 */

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"bat_chg",  IT83XX_I2C_CH_A, 100, GPIO_I2C_A_SCL, GPIO_I2C_A_SDA},
	{"sensor",   IT83XX_I2C_CH_B, 100, GPIO_I2C_B_SCL, GPIO_I2C_B_SDA},
	{"usb0",     IT83XX_I2C_CH_C, 100, GPIO_I2C_C_SCL, GPIO_I2C_C_SDA},
	{"usb1",     IT83XX_I2C_CH_E, 100, GPIO_I2C_E_SCL, GPIO_I2C_E_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* PPC */
struct ppc_config_t ppc_chips[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.i2c_port = I2C_PORT_PPC0,
		.i2c_addr_flags = SYV682X_ADDR0_FLAGS,
		.drv = &syv682x_drv
	},
	{
		.i2c_port = I2C_PORT_PPC1,
		.i2c_addr_flags = SYV682X_ADDR0_FLAGS,
		.drv = &syv682x_drv
	},
};
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

static void ppc_interrupt(enum gpio_signal signal)
{
	if (signal == GPIO_USB_C0_PPC_INT_ODL)
		/* C0: PPC interrupt */
		syv682x_interrupt(0);
}

static void hdmi_hpd_interrupt(enum gpio_signal signal)
{
	/* TODO: implement HDMI HPD */
}

/* HDMI/TYPE-C function shared subboard interrupt */
static void x_ec_interrupt(enum gpio_signal signal)
{
	int sub = board_get_sub_board();

	if (sub == SUB_BOARD_TYPEC)
		/* C1: PPC interrupt */
		syv682x_interrupt(1);
	else if (sub == SUB_BOARD_HDMI)
		hdmi_hpd_interrupt(signal);
	else
		CPRINTS("Undetected subboard interrupt.");
}

int ppc_get_alert_status(int port)
{
	if (port == 0)
		return gpio_get_level(GPIO_USB_C0_PPC_INT_ODL) == 0;
	if (port == 1 && board_get_sub_board() == SUB_BOARD_TYPEC)
		return gpio_get_level(GPIO_USB_C1_PPC_INT_ODL) == 0;

	return 0;
}

void board_overcurrent_event(int port, int is_overcurrented)
{
	/* TODO: check correct operation for Asurada */
}

/* TCPC */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_EMBEDDED,
		/* TCPC is embedded within EC so no i2c config needed */
		.drv = &it83xx_tcpm_drv,
		/* Alert is active-low, push-pull */
		.flags = 0,
	},
	{
		.bus_type = EC_BUS_TYPE_EMBEDDED,
		/* TCPC is embedded within EC so no i2c config needed */
		.drv = &it83xx_tcpm_drv,
		/* Alert is active-low, push-pull */
		.flags = 0,
	},
};

/* USB Mux */
const struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
};

uint16_t tcpc_get_alert_status(void)
{
	/*
	 * C0 & C1: TCPC is embedded in the EC and processes interrupts in the
	 * chip code (it83xx/intc.c)
	 */
	return 0;
}

void board_reset_pd_mcu(void)
{
	/*
	 * C0 & C1: TCPC is embedded in the EC and processes interrupts in the
	 * chip code (it83xx/intc.c)
	 */
}

int board_get_version(void)
{
	return 0;
}

int board_set_active_charge_port(int port)
{
	int i;
	int is_valid_port = port == 0 || (port == 1 && board_get_sub_board() ==
							       SUB_BOARD_TYPEC);

	if (!is_valid_port && port != CHARGE_PORT_NONE)
		return EC_ERROR_INVAL;

	if (port == CHARGE_PORT_NONE) {
		CPRINTS("Disabling all charger ports");

		/* Disable all ports. */
		for (i = 0; i < ppc_cnt; i++) {
			/*
			 * Do not return early if one fails otherwise we can
			 * get into a boot loop assertion failure.
			 */
			if (ppc_vbus_sink_enable(i, 0))
				CPRINTS("Disabling C%d as sink failed.", i);
		}

		return EC_SUCCESS;
	}

	/* Check if the port is sourcing VBUS. */
	if (ppc_is_sourcing_vbus(port)) {
		CPRINTF("Skip enable C%d", port);
		return EC_ERROR_INVAL;
	}

	CPRINTS("New charge port: C%d", port);

	/*
	 * Turn off the other ports' sink path FETs, before enabling the
	 * requested charge port.
	 */
	for (i = 0; i < ppc_cnt; i++) {
		if (i == port)
			continue;

		if (ppc_vbus_sink_enable(i, 0))
			CPRINTS("C%d: sink path disable failed.", i);
	}

	/* Enable requested charge port. */
	if (ppc_vbus_sink_enable(port, 1)) {
		CPRINTS("C%d: sink path enable failed.", port);
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
}

void board_pd_vconn_ctrl(int port, enum usbpd_cc_pin cc_pin, int enabled)
{
}

/* Sub-board */

static enum board_sub_board board_get_sub_board(void)
{
	static enum board_sub_board sub = SUB_BOARD_NONE;

	if (sub != SUB_BOARD_NONE)
		return sub;

	/* HDMI board has external pull high. */
	if (gpio_get_level(GPIO_EC_X_GPIO3)) {
		sub = SUB_BOARD_HDMI;
		/* Only has 1 PPC with HDMI subboard */
		ppc_cnt = 1;
	} else {
		sub = SUB_BOARD_TYPEC;
		/* EC_X_GPIO1 */
		gpio_set_flags(GPIO_USB_C1_FRS_EN, GPIO_OUT_LOW);
		/* X_EC_GPIO2 */
		gpio_set_flags(GPIO_USB_C1_PPC_INT_ODL,
			       GPIO_INT_BOTH | GPIO_PULL_UP);
		/* EC_X_GPIO3 */
		gpio_set_flags(GPIO_USB_C1_DP_IN_HPD, GPIO_OUT_LOW);
	}

	CPRINTS("Detect %s SUB", sub == SUB_BOARD_HDMI ? "HDMI" : "TYPEC");
	return sub;
}

static void sub_board_init(void)
{
	board_get_sub_board();
}
DECLARE_HOOK(HOOK_INIT, sub_board_init, HOOK_PRIO_INIT_I2C - 1);

__override uint8_t board_get_usb_pd_port_count(void)
{
	if (board_get_sub_board() == SUB_BOARD_TYPEC)
		return CONFIG_USB_PD_PORT_MAX_COUNT;
	else
		return CONFIG_USB_PD_PORT_MAX_COUNT - 1;
}

/* Sensor */

static struct mutex g_base_mutex;

static struct bmi_drv_data_t g_bmi160_data;

/* Matrix to rotate accelerometer into standard reference frame */
/* TODO: update the matrix after we have assembled unit */
static const mat33_fp_t base_standard_ref = {
	{FLOAT_TO_FP(-1), 0, 0},
	{0, FLOAT_TO_FP(-1), 0},
	{0, 0, FLOAT_TO_FP(1)},
};

/* Matrix to rotate accelrator into standard reference frame */
/* TODO: update the matrix after we have assembled unit */
static const mat33_fp_t mag_standard_ref = {
	{0, FLOAT_TO_FP(-1), 0},
	{FLOAT_TO_FP(-1), 0, 0},
	{0, 0, FLOAT_TO_FP(-1)},
};

struct motion_sensor_t motion_sensors[] = {
	/*
	 * Note: bmi160: supports accelerometer and gyro sensor
	 * Requirement: accelerometer sensor must init before gyro sensor
	 * DO NOT change the order of the following table.
	 */
	[BASE_ACCEL] = {
		.name = "Base Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi160_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bmi160_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
		.rot_standard_ref = &base_standard_ref,
		.default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
		.min_frequency = BMI_ACCEL_MIN_FREQ,
		.max_frequency = BMI_ACCEL_MAX_FREQ,
		.config = {
			/* Sensor on for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
			/* Sensor on for angle detection */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
		},
	},
	[BASE_GYRO] = {
		.name = "Gyro",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi160_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bmi160_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
		.default_range = 1000, /* dps */
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = BMI_GYRO_MIN_FREQ,
		.max_frequency = BMI_GYRO_MAX_FREQ,
	},
	[BASE_MAG] = {
		.name = "Lid Mag",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_MAG,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi160_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bmi160_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
		.default_range = BIT(11), /* 16LSB / uT, fixed */
		.rot_standard_ref = &mag_standard_ref,
		.min_frequency = BMM150_MAG_MIN_FREQ,
		.max_frequency = BMM150_MAG_MAX_FREQ(SPECIAL),
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);
