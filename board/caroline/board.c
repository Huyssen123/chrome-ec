/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Caroline board-specific configuration */

#include "adc_chip.h"
#include "bd99992gw.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "console.h"
#include "driver/accel_bma2x2.h"
#include "driver/accelgyro_bmi160.h"
#include "driver/als_bh1730.h"
#include "driver/kbl_max14521.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "math_util.h"
#include "motion_sense.h"
#include "motion_lid.h"
#include "pi3usb9281.h"
#include "power.h"
#include "power_button.h"
#include "ps8740.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "temp_sensor.h"
#include "timer.h"
#include "uart.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#define GPIO_KB_INPUT (GPIO_INPUT | GPIO_PULL_UP)
#define GPIO_KB_OUTPUT (GPIO_ODR_HIGH)

#define I2C_ADDR_BD99992 0x60

/* Exchange status with PD MCU. */
static void pd_mcu_interrupt(enum gpio_signal signal)
{
#ifdef HAS_TASK_PDCMD
	/* Exchange status with PD MCU to determine interrupt cause */
	host_command_pd_send_status(0);
#endif
}

void vbus_evt(enum gpio_signal signal)
{
	int port = (signal == GPIO_USB_C1_VBUS_WAKE_L);

	/* VBUS present GPIO is inverted */
	usb_charger_vbus_change(port, !gpio_get_level(signal));
	task_wake(port ? TASK_ID_PD_C1 : TASK_ID_PD_C0);
}

void usb0_evt(enum gpio_signal signal)
{
	task_set_event(TASK_ID_USB_CHG_P0, USB_CHG_EVENT_BC12, 0);
}

void usb1_evt(enum gpio_signal signal)
{
	task_set_event(TASK_ID_USB_CHG_P1, USB_CHG_EVENT_BC12, 0);
}

#include "gpio_list.h"

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_RSMRST_L_PGOOD,    1, "RSMRST_N_PWRGD"},
	{GPIO_PCH_SLP_S3_L,      1, "SLP_S3_DEASSERTED"},
	{GPIO_PCH_SLP_S4_L,      1, "SLP_S4_DEASSERTED"},
	{GPIO_PCH_SLP_SUS_L,     1, "SLP_SUS_DEASSERTED"},
	{GPIO_PMIC_DPWROK,       1, "PMIC_DPWROK"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Vbus sensing. Converted to mV, full ADC is equivalent to 30V. */
	[ADC_VBUS] = {"VBUS", 30000, 1024, 0, 1},
	/* Adapter current output or battery discharging current */
	[ADC_AMON_BMON] = {"AMON_BMON", 25000, 3072, 0, 3},
	/* System current consumption */
	[ADC_PSYS] = {"PSYS", 1, 1, 0, 4},

};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct i2c_port_t i2c_ports[]  = {
	{"pmic",     MEC1322_I2C0_0, 400,  GPIO_I2C0_0_SCL, GPIO_I2C0_0_SDA},
	{"muxes",    MEC1322_I2C0_1, 400,  GPIO_I2C0_1_SCL, GPIO_I2C0_1_SDA},
	{"pd_mcu",   MEC1322_I2C1,   500,  GPIO_I2C1_SCL,   GPIO_I2C1_SDA},
	{"sensors",  MEC1322_I2C2,   400,  GPIO_I2C2_SCL,   GPIO_I2C2_SDA  },
	{"batt",     MEC1322_I2C3,   100,  GPIO_I2C3_SCL,   GPIO_I2C3_SDA  },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
	{I2C_PORT_TCPC, CONFIG_TCPC_I2C_BASE_ADDR},
	{I2C_PORT_TCPC, CONFIG_TCPC_I2C_BASE_ADDR + 2},
};

/* SPI devices */
const struct spi_device_t spi_devices[] = {
	{ CONFIG_SPI_FLASH_PORT, 0, GPIO_PVT_CS0},
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_AC_PRESENT,
	GPIO_LID_OPEN,
	GPIO_POWER_BUTTON_L,
};

const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

struct pi3usb9281_config pi3usb9281_chips[] = {
	{
		.i2c_port = I2C_PORT_USB_CHARGER_1,
		.mux_lock = NULL,
	},
	{
		.i2c_port = I2C_PORT_USB_CHARGER_2,
		.mux_lock = NULL,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3usb9281_chips) ==
	     CONFIG_USB_SWITCH_PI3USB9281_CHIP_COUNT);

static int ps8740_tune_mux(const struct usb_mux *mux)
{
	/* increase USB EQ to 15.5dB on RX */
	ps8740_tune_usb_eq(mux->port_addr,
			   PS8740_USB_EQ_TX_10_1_DB,
			   PS8740_USB_EQ_RX_15_5_DB);
	/* High Speed Signal Detector threshold adjustment */
	ps8740_tune_hs_vth(mux->port_addr, PS8740_HS_VTH_SUB_25P);

	return EC_SUCCESS;
}

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_COUNT] = {
	{
		.port_addr = 0xa8,
		.driver = &pi3usb30532_usb_mux_driver,
	},
	{
		.port_addr = 0x20,
		.driver = &ps8740_usb_mux_driver,
		.board_init = &ps8740_tune_mux,
	}
};

static int cached_board_id;

/**
 * Reset PD MCU
 */
void board_reset_pd_mcu(void)
{
	gpio_set_level(GPIO_PD_RST_L, 0);
	usleep(100);
	gpio_set_level(GPIO_PD_RST_L, 1);
}

void board_rtc_reset(void)
{
	CPRINTS("Asserting RTCRST# to PCH");
	gpio_set_level(GPIO_PCH_RTCRST, 1);
	udelay(100);
	gpio_set_level(GPIO_PCH_RTCRST, 0);
}

const struct temp_sensor_t temp_sensors[] = {
	{"Battery", TEMP_SENSOR_TYPE_BATTERY, charge_temp_sensor_get_val, 0, 4},

	/* These BD99992GW temp sensors are only readable in S0 */
	{"Ambient", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
		BD99992GW_ADC_CHANNEL_SYSTHERM0, 4},
	{"Charger", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
		BD99992GW_ADC_CHANNEL_SYSTHERM1, 4},
	{"DRAM", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
		BD99992GW_ADC_CHANNEL_SYSTHERM2, 4},
	{"Wifi", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
		BD99992GW_ADC_CHANNEL_SYSTHERM3, 4},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/*
 * Thermal limits for each temp sensor.  All temps are in degrees K.  Must be in
 * same order as enum temp_sensor_id.  To always ignore any temp, use 0.
 */
struct ec_thermal_config thermal_params[] = {
	/* {Twarn, Thigh, Thalt}, fan_off, fan_max */
	{{0, 0, 0}, 0, 0},	/* Battery */
	{{0, 0, 0}, 0, 0},	/* Ambient */
	{{0, 0, 0}, 0, 0},	/* Charger */
	{{0, 0, 0}, 0, 0},	/* DRAM */
	{{0, 0, 0}, 0, 0},	/* Wifi */
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

const struct button_config buttons[CONFIG_BUTTON_COUNT] = {
	{"Volume Down", KEYBOARD_BUTTON_VOLUME_DOWN, GPIO_VOLUME_DOWN_L,
	 30 * MSEC, 0},
	{"Volume Up", KEYBOARD_BUTTON_VOLUME_UP, GPIO_VOLUME_UP_L,
	 30 * MSEC, 0},
};

static void board_pmic_init(void)
{
	/* No need to re-init PMIC since settings are sticky across sysjump */
	if (system_jumped_to_this_image())
		return;

	/* DISCHGCNT3 - enable 100 ohm discharge on V1.00A */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x3e, 0x04);

	/*
	 * [5:4] 0:1 Set V5ADS3VSEL = Vnom+2% (chrome-os-partner:56642)
	 * [3:2] 0:0 Set AOACCNTV5ADS3 = fast-charge mode disable
	 * [1:0] 1:0 Set CTLV5ADS3 = Auto (chrome-os-partner:60383)
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x31, 0x12);

	/* Set CSDECAYEN / VCCIO decays to 0V at assertion of SLP_S0# */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x30, 0x4a);

	/*
	 * Set V100ACNT / V1.00A Control Register:
	 * Nominal output = 1.0V.
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x37, 0x1a);

	/*
	 * Set V085ACNT / V0.85A Control Register:
	 * Lower power mode = 0.7V.
	 * Nominal output = 1.0V.
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x38, 0x7a);
}
DECLARE_HOOK(HOOK_INIT, board_pmic_init, HOOK_PRIO_DEFAULT);

/* Initialize board. */
static void board_init(void)
{
	/* Enable PD MCU interrupt */
	gpio_enable_interrupt(GPIO_PD_MCU_INT);
	/* Enable VBUS interrupt */
	gpio_enable_interrupt(GPIO_USB_C0_VBUS_WAKE_L);
	gpio_enable_interrupt(GPIO_USB_C1_VBUS_WAKE_L);

	/* Enable pericom BC1.2 interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_L);
	gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_L);

	/* Enable interrupts from BMI160 sensor. */
	gpio_enable_interrupt(GPIO_ACCEL3_INT);

	/* Provide AC status to the PCH */
	gpio_set_level(GPIO_PCH_ACOK, extpower_is_present());

	cached_board_id = system_get_board_version();

	/*
	 * If board id is lower than 6 and we define ALS in build stage
	 * Disable ALS.
	 * LID_ALS : als sensor index is the last of motion_sensors.
	 */
#ifdef CONFIG_DYNAMIC_MOTION_SENSOR_COUNT
	if (cached_board_id < 6)
		motion_sensor_count -= 1;
#else
	/* In case of Caroline, we have to enable ALS motion seneor dynamically. */
#error "Need to define CONFIG_DYNAMIC_MOTION_SENSOR_COUNT on caroline."
#endif
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/**
 * Buffer the AC present GPIO to the PCH.
 */
static void board_extpower(void)
{
	gpio_set_level(GPIO_PCH_ACOK, extpower_is_present());
}
DECLARE_HOOK(HOOK_AC_CHANGE, board_extpower, HOOK_PRIO_DEFAULT);

/**
 * Set active charge port -- only one port can be active at a time.
 *
 * @param charge_port   Charge port to enable.
 *
 * Returns EC_SUCCESS if charge port is accepted and made active,
 * EC_ERROR_* otherwise.
 */
int board_set_active_charge_port(int charge_port)
{
	/* charge port is a realy physical port */
	int is_real_port = (charge_port >= 0 &&
			    charge_port < CONFIG_USB_PD_PORT_COUNT);
	/* check if we are source vbus on that port */
	int source = gpio_get_level(charge_port == 0 ? GPIO_USB_C0_5V_EN :
						       GPIO_USB_C1_5V_EN);

	if (is_real_port && source) {
		CPRINTS("Skip enable p%d", charge_port);
		return EC_ERROR_INVAL;
	}

	CPRINTS("New chg p%d", charge_port);

	if (charge_port == CHARGE_PORT_NONE) {
		/* Disable both ports */
		gpio_set_level(GPIO_USB_C0_CHARGE_EN_L, 1);
		gpio_set_level(GPIO_USB_C1_CHARGE_EN_L, 1);
	} else {
		/* Make sure non-charging port is disabled */
		gpio_set_level(charge_port ? GPIO_USB_C0_CHARGE_EN_L :
					     GPIO_USB_C1_CHARGE_EN_L, 1);
		/* Enable charging port */
		gpio_set_level(charge_port ? GPIO_USB_C1_CHARGE_EN_L :
					     GPIO_USB_C0_CHARGE_EN_L, 0);
	}

	return EC_SUCCESS;
}

/**
 * Set the charge limit based upon desired maximum.
 *
 * @param charge_ma     Desired charge limit (mA).
 */
void board_set_charge_limit(int charge_ma)
{
	/*
	 * Limit the input current to 95% negotiated limit,
	 * to account for the charger chip margin.
	 */
	charge_ma = charge_ma * 95 / 100;
	charge_set_input_current_limit(MAX(charge_ma,
					   CONFIG_CHARGER_INPUT_CURRENT));
}

/**
 * Return whether ramping is allowed for given supplier
 */
int board_is_ramp_allowed(int supplier)
{
	/* Don't allow ramping in RO when write protected */
	if (system_get_image_copy() != SYSTEM_IMAGE_RW
	    && system_is_locked())
		return 0;
	else
		return supplier == CHARGE_SUPPLIER_BC12_DCP ||
		       supplier == CHARGE_SUPPLIER_BC12_SDP ||
		       supplier == CHARGE_SUPPLIER_BC12_CDP ||
		       supplier == CHARGE_SUPPLIER_PROPRIETARY;
}

/**
 * Return the maximum allowed input current
 */
int board_get_ramp_current_limit(int supplier, int sup_curr)
{
	switch (supplier) {
	case CHARGE_SUPPLIER_BC12_DCP:
		return 2000;
	case CHARGE_SUPPLIER_BC12_SDP:
		return 1000;
	case CHARGE_SUPPLIER_BC12_CDP:
	case CHARGE_SUPPLIER_PROPRIETARY:
		return sup_curr;
	default:
		return 500;
	}
}

/* Called on AP S5 -> S3 transition */
static void board_chipset_startup(void)
{
	gpio_set_level(GPIO_ENABLE_TOUCHPAD, 1);
	gpio_set_level(GPIO_PP1800_DX_SENSOR_EN, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S5 transition */
static void board_chipset_shutdown(void)
{
	gpio_set_level(GPIO_ENABLE_TOUCHPAD, 0);
	gpio_set_level(GPIO_PP1800_DX_SENSOR_EN, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
	gpio_set_level(GPIO_PP1800_DX_AUDIO_EN, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	gpio_set_level(GPIO_PP1800_DX_AUDIO_EN, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

void board_hibernate(void)
{
	CPRINTS("Triggering PMIC shutdown.");
	uart_flush_output();

	/* Trigger PMIC shutdown. */
	if (i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x49, 0x01)) {
		/*
		 * If we can't tell the PMIC to shutdown, instead reset
		 * and don't start the AP. Hopefully we'll be able to
		 * communicate with the PMIC next time.
		 */
		CPRINTS("PMIC i2c failed.");
		system_reset(SYSTEM_RESET_LEAVE_AP_OFF);
	}

	/* Await shutdown. */
	while (1)
		;
}

/* Make the pmic re-sequence the power rails under these conditions. */
#define PMIC_RESET_FLAGS \
	(RESET_FLAG_WATCHDOG | RESET_FLAG_SOFT | RESET_FLAG_HARD)
static void board_handle_reboot(void)
{
	int flags;

	if (system_jumped_to_this_image())
		return;

	/* Interrogate current reset flags from previous reboot. */
	flags = system_get_reset_flags();

	if (!(flags & PMIC_RESET_FLAGS))
		return;

	/* Preserve AP off request. */
	if (flags & RESET_FLAG_AP_OFF)
		chip_save_reset_flags(RESET_FLAG_AP_OFF);

	ccprintf("Restarting system with PMIC.\n");
	/* Flush console */
	cflush();

	/* Bring down all rails but RTC rail (including EC power). */
	gpio_set_flags(GPIO_PLATFORM_RST, GPIO_OUT_HIGH);
	while (1)
		; /* wait here */
}
DECLARE_HOOK(HOOK_INIT, board_handle_reboot, HOOK_PRIO_FIRST);


static void tablet_mode_changed(void)
{
	host_set_single_event(EC_HOST_EVENT_MODE_CHANGE);
}
DECLARE_HOOK(HOOK_TABLET_MODE_CHANGE, tablet_mode_changed, HOOK_PRIO_DEFAULT);

#ifdef HAS_TASK_MOTIONSENSE
/* Motion sensors */
/* Mutexes */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

/* BMA255 private data */
struct bma2x2_accel_data g_bma255_data = {
	.variant = BMA255,
};
struct bh1730_drv_data_t g_bh1730_data;

/* Matrix to rotate accelrator into standard reference frame */
const matrix_3x3_t base_standard_ref = {
	{ 0, FLOAT_TO_FP(1),  0},
	{ FLOAT_TO_FP(1),  0, 0},
	{ 0,  0, FLOAT_TO_FP(-1)}
};

const matrix_3x3_t lid_standard_ref = {
	{ 0,  FLOAT_TO_FP(1), 0},
	{ FLOAT_TO_FP(-1),  0,  0},
	{ 0,  0, FLOAT_TO_FP(1)}
};

struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
		.name = "Lid Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMA255,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &bma2x2_accel_drv,
		.mutex = &g_lid_mutex,
		.drv_data = &g_bma255_data,
		.addr = BMA2x2_I2C_ADDR1,
		.rot_standard_ref = &lid_standard_ref,
		.default_range = 8, /* g, to support tablet mode */
		.min_frequency = BMA255_ACCEL_MIN_FREQ,
		.max_frequency = BMA255_ACCEL_MAX_FREQ,
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
			/* Sensor on in S3 */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
		},
	},
	[BASE_ACCEL] = {
		.name = "Base Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi160_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bmi160_data,
		.addr = BMI160_ADDR0,
		.rot_standard_ref = &base_standard_ref,
		.default_range = 8, /* g, to support tablet mode  */
		.min_frequency = BMI160_ACCEL_MIN_FREQ,
		.max_frequency = BMI160_ACCEL_MAX_FREQ,
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
			/* Sensor on in S3 */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
		},
	},
	[BASE_GYRO] = {
		.name = "Base Gyro",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi160_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bmi160_data,
		.addr = BMI160_ADDR0,
		.default_range = 1000, /* dps */
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = BMI160_GYRO_MIN_FREQ,
		.max_frequency = BMI160_GYRO_MAX_FREQ,
	},
	[LID_ALS] = {
		.name = "Light",
		.active_mask = SENSOR_ACTIVE_S0,
		.chip = MOTIONSENSE_CHIP_BH1730,
		.type = MOTIONSENSE_TYPE_LIGHT,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &bh1730_drv,
		.drv_data = &g_bh1730_data,
		.addr = BH1730_I2C_ADDR,
		.rot_standard_ref = NULL,
		.default_range = 65535,
		.min_frequency = BH1730_MIN_FREQ,
		.max_frequency = BH1730_MAX_FREQ,
		.config = {
			[SENSOR_CONFIG_EC_S0] = {
				.odr = BH1730_10000_MHZ,
				.ec_rate = 0,
			},
		},
	},
};

#ifdef CONFIG_DYNAMIC_MOTION_SENSOR_COUNT
unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);
#else
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);
#endif

/* ALS instances when LPC mapping is needed. Each entry directs to a sensor. */
const struct motion_sensor_t *motion_als_sensors[] = {
	&motion_sensors[LID_ALS],
};
BUILD_ASSERT(ARRAY_SIZE(motion_als_sensors) == ALS_COUNT);
#endif /* HAS_TASK_MOTIONSENSE */

int board_get_version(void)
{
	int v = 0;

	if (gpio_get_level(GPIO_BOARD_VERSION1))
		v |= 0x01;
	if (gpio_get_level(GPIO_BOARD_VERSION2))
		v |= 0x02;
	if (gpio_get_level(GPIO_BOARD_VERSION3))
		v |= 0x04;

	return v;
}

#ifdef CONFIG_LID_ANGLE_UPDATE
static int has_trackpad_disable = -1;

void lid_angle_peripheral_enable(int enable)
{
	int chipset_in_s0 = chipset_in_state(CHIPSET_STATE_ON);

	/*
	 * Support old hardware where GPIO 127 is directly connected
	 * to the trackpad interrupt.
	 * Can be removed once board version 0 and 1 are retired.
	 */
	if (has_trackpad_disable < 0) {
		has_trackpad_disable = board_get_version() >= 2;
		if (!has_trackpad_disable)
			gpio_set_flags(GPIO_TRACKPAD_INT_DISABLE, GPIO_INPUT);
	}

	if (enable) {
		keyboard_scan_enable(1, KB_SCAN_DISABLE_LID_ANGLE);
		if (has_trackpad_disable)
			gpio_set_level(GPIO_TRACKPAD_INT_DISABLE, 0);
	} else {
		/*
		 * Ensure chipset is off before disabling keyboard. When chipset
		 * is on, EC keeps keyboard enabled and the AP decides when to
		 * ignore keys based on its more accurate lid angle calculation.
		 *
		 * TODO(crosbug.com/p/43695): Remove this check once we have a
		 * host command that can inform EC when we are entering or
		 * exiting tablet mode in S0. Also, add this check back to the
		 * function lid_angle_update in lid_angle.c
		 */
		if (!chipset_in_s0) {
			keyboard_scan_enable(0, KB_SCAN_DISABLE_LID_ANGLE);
			if (has_trackpad_disable)
				gpio_set_level(GPIO_TRACKPAD_INT_DISABLE, 1);
		}
	}
}
#endif

/*
 * Various voltage rails will be enabled / disabled by the PMIC when
 * GPIO_PMIC_SLP_SUS_L changes. We need to delay the disable of V0.85A
 * by approximately 50ms in order to allow V1.00A to sufficiently discharge
 * first.
 *
 * Therefore, after GPIO_PMIC_SLP_SUS_L goes high, ignore the state of
 * the V12_EN pin: Keep V0.85A enabled.
 *
 * When GPIO_PMIC_SLP_SUS_L goes low, delay 50ms, and make V12_EN function
 * as normal - this should result in V0.85A discharging immediately after the
 * i2c write completes.
 */
void chipset_set_pmic_slp_sus_l(int level)
{
	static int previous_level;
	int val;

	gpio_set_level(GPIO_PMIC_SLP_SUS_L, level);

	if (previous_level != level) {
		/* Rising edge: Force V0.85A enable. Falling: Pin control. */
		val = level ? 0x80 : 0;
		if (!level)
			msleep(50);

		i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x43, val);
		previous_level = level;
	}
}

/*
 * Control KBLIGHT
 */
/*****************************************************************************/
/* Hooks */

static void kblight_enable(void)
{
	if(cached_board_id < 6)
		return;
	gpio_set_level(GPIO_KBDBKLIT_RST_L, 1);
	msleep(10);
	max14521_init();
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, kblight_enable, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_INIT, kblight_enable, HOOK_PRIO_DEFAULT);

static void kblight_disable(void)
{
	if(cached_board_id < 6)
		return;
	gpio_set_level(GPIO_KBDBKLIT_RST_L, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, kblight_disable, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, kblight_disable, HOOK_PRIO_DEFAULT);
