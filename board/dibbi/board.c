/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Dibbi board-specific configuration */

#include "adc_chip.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state_v2.h"
#include "charger.h"
#include "driver/tcpm/it83xx_pd.h"
#include "driver/temp_sensor/thermistor.h"
#include "driver/usb_mux/it5205.h"
#include "gpio.h"
#include "hooks.h"
#include "intc.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "switch.h"
#include "system.h"
#include "tablet_mode.h"
#include "task.h"
#include "tcpm/tcpci.h"
#include "temp_sensor.h"
#include "uart.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#define CPRINTUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)

/* ADC channels */
const struct adc_t adc_channels[] = {
	[ADC_VSNS_PP3300_A] = { .name = "PP3300_A_PGOOD",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH0 },
	[ADC_TEMP_SENSOR_1] = { .name = "TEMP_SENSOR1",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH2 },
	[ADC_TEMP_SENSOR_2] = { .name = "TEMP_SENSOR2",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH3 },
	[ADC_PPVAR_PWR_IN_IMON] = { .name = "ADC_PPVAR_PWR_IN_IMON",
				    .factor_mul = ADC_MAX_MVOLT,
				    .factor_div = ADC_READ_MAX + 1,
				    .shift = 0,
				    .channel = CHIP_ADC_CH15 },
	[ADC_SNS_PPVAR_PWR_IN] = { .name = "ADC_SNS_PPVAR_PWR_IN",
				   .factor_mul = ADC_MAX_MVOLT,
				   .factor_div = ADC_READ_MAX + 1,
				   .shift = 0,
				   .channel = CHIP_ADC_CH16 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* TCPCs */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_EMBEDDED,
		.drv = &it83xx_tcpm_drv,
	},
};

/* USB Muxes */
const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 0,
				.i2c_port = I2C_PORT_USB_C0,
				.i2c_addr_flags = IT5205_I2C_ADDR1_FLAGS,
				.driver = &it5205_usb_mux_driver,
			},
	},
};

/* USB-A ports */
const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_USB_A0_VBUS,
	GPIO_EN_USB_A1_VBUS,
	GPIO_EN_USB_A2_VBUS,
};

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_LED_RED] = {
		.channel = 1,
		.flags = PWM_CONFIG_DSLEEP | PWM_CONFIG_ACTIVE_LOW,
		.freq_hz = 2400,
	},

	[PWM_CH_LED_GREEN] = {
		.channel = 2,
		.flags = PWM_CONFIG_DSLEEP | PWM_CONFIG_ACTIVE_LOW,
		.freq_hz = 2400,
	},

	[PWM_CH_LED_BLUE] = {
		.channel = 3,
		.flags = PWM_CONFIG_DSLEEP | PWM_CONFIG_ACTIVE_LOW,
		.freq_hz = 2400,
	}

};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* Thermistors */
const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1] = { .name = "Memory",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_1 },
	[TEMP_SENSOR_2] = { .name = "Ambient",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_2 },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

void board_init(void)
{
	int on;

	gpio_enable_interrupt(GPIO_BJ_ADP_PRESENT_L);

	/* Turn on 5V if the system is on, otherwise turn it off */
	on = chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_ANY_SUSPEND |
			      CHIPSET_STATE_SOFT_OFF);
	board_power_5v_enable(on);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

void board_reset_pd_mcu(void)
{
	/*
	 * Nothing to do.  TCPC C0 is internal.
	 */
}

__override void board_power_5v_enable(int enable)
{
	/*
	 * Mainboard 5V regulator activated by GPIO.
	 * USB-A ports are activated by usb_port_power_dumb.
	 */
	gpio_set_level(GPIO_EN_PP5000, !!enable);
}

void board_set_charge_limit(int port, int supplier, int charge_ma, int max_ma,
			    int charge_mv)
{
	int insufficient_power =
		(charge_ma * charge_mv) <
		(CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON * 1000);
	/* TODO(b/259467280) blink LED on error */
	(void)insufficient_power;
}

int board_vbus_source_enabled(int port)
{
	if (port != CHARGE_PORT_TYPEC0)
		return 0;

	return tcpm_check_vbus_level(port, VBUS_PRESENT);
}

/* Vconn control for integrated ITE TCPC */
void board_pd_vconn_ctrl(int port, enum usbpd_cc_pin cc_pin, int enabled)
{
	/* Vconn control is only for port 0 */
	if (port)
		return;

	if (cc_pin == USBPD_CC_PIN_1)
		gpio_set_level(GPIO_EN_USB_C0_CC1_VCONN, !!enabled);
	else
		gpio_set_level(GPIO_EN_USB_C0_CC2_VCONN, !!enabled);
}

__override void typec_set_source_current_limit(int port, enum tcpc_rp_value rp)
{
	int ilim3A;

	if (port < 0 || port > CONFIG_USB_PD_PORT_MAX_COUNT)
		return;

	/* Switch between 1.5A and 3A ILIM values */
	ilim3A = (rp == TYPEC_RP_3A0);

	tcpm_select_rp_value(0, ilim3A);
}

/******************************************************************************/
/*
 * Since dibbi has no battery, it must source all of its power from either
 * USB-C or the barrel jack (preferred). Fizz operates in continuous safe
 * mode (charge_manager_leave_safe_mode() will never be called), which
 * modifies port selection as follows:
 *
 * - Dual-role / dedicated capability of the port partner is ignored.
 * - Charge ceiling on PD voltage transition is ignored.
 * - CHARGE_PORT_NONE will never be selected.
 */

/* List of BJ adapters */
enum bj_adapter {
	BJ_NONE,
	BJ_65W_19V,
};

/* Barrel-jack power adapter ratings. */
static const struct charge_port_info bj_adapters[] = {
	[BJ_NONE] = { .current = 0, .voltage = 0 },
	[BJ_65W_19V] = { .current = 3420, .voltage = 19000 },
};
#define BJ_ADP_RATING_DEFAULT BJ_65W_19V /* BJ power ratings default */
#define ADP_DEBOUNCE_MS 1000 /* Debounce time for BJ plug/unplug */

/* Debounced connection state of the barrel jack */
static int8_t bj_adp_connected = -1;
static void adp_connect_deferred(void)
{
	const struct charge_port_info *pi;
	int connected = !gpio_get_level(GPIO_BJ_ADP_PRESENT_L);

	/* Debounce */
	if (connected == bj_adp_connected)
		return;

	if (connected) {
		pi = &bj_adapters[BJ_ADP_RATING_DEFAULT];
	} else {
		/* No barrel-jack, zero out this power supply */
		pi = &bj_adapters[BJ_NONE];
	}
	/* This will result in a call to board_set_active_charge_port */
	charge_manager_update_charge(CHARGE_SUPPLIER_DEDICATED,
				     DEDICATED_CHARGE_PORT, pi);
	bj_adp_connected = connected;
}
DECLARE_DEFERRED(adp_connect_deferred);

/* IRQ for BJ plug/unplug. It shouldn't be called if BJ is the power source. */
void adp_connect_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&adp_connect_deferred_data, ADP_DEBOUNCE_MS * MSEC);
}

/* IRQ for USB-C plug/unplug. */
void usbc_connect_interrupt(enum gpio_signal signal)
{
	task_wake(TASK_ID_PD_C0);
}

int board_set_active_charge_port(int port)
{
	const int active_port = charge_manager_get_active_charge_port();

	CPRINTUSB("Requested charge port change to %d", port);

	if (port < 0 || CHARGE_PORT_COUNT <= port)
		return EC_ERROR_INVAL;

	if (port == active_port)
		return EC_SUCCESS;

	/* Don't sink from a source port */
	if (board_vbus_source_enabled(port))
		return EC_ERROR_INVAL;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		int bj_requested;

		if (charge_manager_get_active_charge_port() != CHARGE_PORT_NONE)
			/* Change is only permitted while the system is off */
			return EC_ERROR_INVAL;

		/*
		 * Current setting is no charge port but the AP is on, so the
		 * charge manager is out of sync (probably because we're
		 * reinitializing after sysjump). Reject requests that aren't
		 * in sync with our outputs.
		 */
		bj_requested = port == CHARGE_PORT_BARRELJACK;
		if (bj_adp_connected != bj_requested)
			return EC_ERROR_INVAL;
	}

	CPRINTUSB("New charger p%d", port);

	switch (port) {
	case CHARGE_PORT_TYPEC0:
		/* TODO(b/267742066): Actually enable USBC */
		/* gpio_set_level(GPIO_EN_PPVAR_USBC_ADP_L, 0); */
		gpio_set_level(GPIO_EN_PPVAR_BJ_ADP_OD, 1);
		gpio_enable_interrupt(GPIO_BJ_ADP_PRESENT_L);
		break;
	case CHARGE_PORT_BARRELJACK:
		/* Make sure BJ adapter is sourcing power */
		if (gpio_get_level(GPIO_BJ_ADP_PRESENT_L))
			return EC_ERROR_INVAL;
		gpio_set_level(GPIO_EN_PPVAR_BJ_ADP_OD, 0);
		/* TODO(b/267742066): Actually disable USBC */
		/* gpio_set_level(GPIO_EN_PPVAR_USBC_ADP_L, 1); */
		gpio_disable_interrupt(GPIO_BJ_ADP_PRESENT_L);
		break;
	default:
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}

static void board_charge_manager_init(void)
{
	enum charge_port port;

	/*
	 * Initialize all charge suppliers to 0. The charge manager waits until
	 * all ports have reported in before doing anything.
	 */
	for (int i = 0; i < CHARGE_PORT_COUNT; i++) {
		for (int j = 0; j < CHARGE_SUPPLIER_COUNT; j++)
			charge_manager_update_charge(j, i, NULL);
	}

	port = gpio_get_level(GPIO_BJ_ADP_PRESENT_L) ? CHARGE_PORT_TYPEC0 :
						       CHARGE_PORT_BARRELJACK;
	CPRINTUSB("Power source is p%d (%s)", port,
		  port == CHARGE_PORT_TYPEC0 ? "USB-C" : "BJ");

	/* Initialize the power source supplier */
	switch (port) {
	case CHARGE_PORT_TYPEC0:
		typec_set_input_current_limit(port, 3000, 5000);
		break;
	case CHARGE_PORT_BARRELJACK:
		charge_manager_update_charge(
			CHARGE_SUPPLIER_DEDICATED, DEDICATED_CHARGE_PORT,
			&bj_adapters[BJ_ADP_RATING_DEFAULT]);
		break;
	}

	/* Report charge state from the barrel jack. */
	adp_connect_deferred();
}
DECLARE_HOOK(HOOK_INIT, board_charge_manager_init,
	     HOOK_PRIO_INIT_CHARGE_MANAGER + 1);

__override int extpower_is_present(void)
{
	/*
	 * There's no battery, so running this method implies we have power.
	 */
	return 1;
}

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"
