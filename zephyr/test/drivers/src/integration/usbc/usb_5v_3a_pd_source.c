/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>

#include "battery_smart.h"
#include "emul/emul_isl923x.h"
#include "emul/emul_smart_battery.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "hooks.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_pd.h"
#include "usb_prl_sm.h"

#define BATTERY_ORD DT_DEP_ORD(DT_NODELABEL(battery))

#define TEST_USB_PORT USBC_PORT_C0

struct usb_attach_5v_3a_pd_source_fixture {
	struct tcpci_partner_data source_5v_3a;
	struct tcpci_src_emul_data src_ext;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
};

static void *usb_attach_5v_3a_pd_source_setup(void)
{
	static struct usb_attach_5v_3a_pd_source_fixture test_fixture;

	/* Get references for the emulators */
	test_fixture.tcpci_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(tcpci_emul)));
	test_fixture.charger_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(isl923x_emul)));

	/* Initialized the charger to supply 5V and 3A */
	tcpci_partner_init(&test_fixture.source_5v_3a, PD_REV20);
	test_fixture.source_5v_3a.extensions = tcpci_src_emul_init(
		&test_fixture.src_ext, &test_fixture.source_5v_3a, NULL);
	test_fixture.src_ext.pdo[1] =
		PDO_FIXED(5000, 3000, PDO_FIXED_UNCONSTRAINED);

	return &test_fixture;
}

static void usb_attach_5v_3a_pd_source_before(void *data)
{
	struct usb_attach_5v_3a_pd_source_fixture *fixture = data;

	connect_source_to_port(&fixture->source_5v_3a, &fixture->src_ext, 1,
			       fixture->tcpci_emul, fixture->charger_emul);
}

static void usb_attach_5v_3a_pd_source_after(void *data)
{
	struct usb_attach_5v_3a_pd_source_fixture *fixture = data;

	disconnect_source_from_port(fixture->tcpci_emul, fixture->charger_emul);
}

ZTEST_SUITE(usb_attach_5v_3a_pd_source, drivers_predicate_post_main,
	    usb_attach_5v_3a_pd_source_setup, usb_attach_5v_3a_pd_source_before,
	    usb_attach_5v_3a_pd_source_after, NULL);

ZTEST(usb_attach_5v_3a_pd_source, test_battery_is_charging)
{
	struct i2c_emul *i2c_emul = sbat_emul_get_ptr(BATTERY_ORD);
	uint16_t battery_status;

	zassume_ok(sbat_emul_get_word_val(i2c_emul, SB_BATTERY_STATUS,
					  &battery_status),
		   NULL);
	zassert_equal(battery_status & STATUS_DISCHARGING, 0,
		      "Battery is discharging: %d", battery_status);
}

ZTEST(usb_attach_5v_3a_pd_source, test_charge_state)
{
	struct ec_response_charge_state state = host_cmd_charge_state(0);

	zassert_true(state.get_state.ac, "AC_OK not triggered");
	zassert_true(state.get_state.chg_voltage > 0,
		     "Expected a charge voltage, but got %dmV",
		     state.get_state.chg_voltage);
	zassert_true(state.get_state.chg_current > 0,
		     "Expected a charge current, but got %dmA",
		     state.get_state.chg_current);
}

ZTEST(usb_attach_5v_3a_pd_source, test_typec_status)
{
	struct ec_response_typec_status status = host_cmd_typec_status(0);

	zassert_true(status.pd_enabled, "PD is disabled");
	zassert_true(status.dev_connected, "Device disconnected");
	zassert_true(status.sop_connected, "Charger is not SOP capable");
	zassert_equal(status.source_cap_count, 2,
		      "Expected 2 source PDOs, but got %d",
		      status.source_cap_count);
	zassert_equal(status.power_role, PD_ROLE_SINK,
		      "Expected power role to be %d, but got %d", PD_ROLE_SINK,
		      status.power_role);
}

ZTEST(usb_attach_5v_3a_pd_source, test_power_info)
{
	struct ec_response_usb_pd_power_info info = host_cmd_power_info(0);

	zassert_equal(info.role, USB_PD_PORT_POWER_SINK,
		      "Expected role to be %d, but got %d",
		      USB_PD_PORT_POWER_SINK, info.role);
	zassert_equal(info.type, USB_CHG_TYPE_PD,
		      "Expected type to be %d, but got %d", USB_CHG_TYPE_PD,
		      info.type);
	zassert_equal(info.meas.voltage_max, 5000,
		      "Expected charge voltage max of 5000mV, but got %dmV",
		      info.meas.voltage_max);
	zassert_within(
		info.meas.voltage_now, 5000, 500,
		"Charging voltage expected to be near 5000mV, but was %dmV",
		info.meas.voltage_now);
	zassert_equal(info.meas.current_max, 3000,
		      "Current max expected to be 3000mV, but was %dmV",
		      info.meas.current_max);
	zassert_true(info.meas.current_lim >= 3000,
		     "VBUS max is set to 3000mA, but PD is reporting %dmA",
		     info.meas.current_lim);
	zassert_equal(info.max_power, 5000 * 3000,
		      "Charging expected to be at %duW, but PD max is %duW",
		      5000 * 3000, info.max_power);
}

ZTEST_F(usb_attach_5v_3a_pd_source, test_disconnect_battery_not_charging)
{
	struct i2c_emul *i2c_emul = sbat_emul_get_ptr(BATTERY_ORD);
	uint16_t battery_status;

	disconnect_source_from_port(fixture->tcpci_emul, fixture->charger_emul);
	zassert_ok(sbat_emul_get_word_val(i2c_emul, SB_BATTERY_STATUS,
					  &battery_status),
		   NULL);
	zassert_equal(battery_status & STATUS_DISCHARGING, STATUS_DISCHARGING,
		      "Battery is not discharging: %d", battery_status);
}

ZTEST_F(usb_attach_5v_3a_pd_source, test_disconnect_charge_state)
{
	struct ec_response_charge_state charge_state;

	disconnect_source_from_port(fixture->tcpci_emul, fixture->charger_emul);
	charge_state = host_cmd_charge_state(0);

	zassert_false(charge_state.get_state.ac, "AC_OK not triggered");
	zassert_equal(charge_state.get_state.chg_current, 0,
		      "Max charge current expected 0mA, but was %dmA",
		      charge_state.get_state.chg_current);
	zassert_equal(charge_state.get_state.chg_input_current,
		      CONFIG_PLATFORM_EC_CHARGER_INPUT_CURRENT,
		      "Charge input current limit expected %dmA, but was %dmA",
		      CONFIG_PLATFORM_EC_CHARGER_INPUT_CURRENT,
		      charge_state.get_state.chg_input_current);
}

ZTEST_F(usb_attach_5v_3a_pd_source, test_disconnect_typec_status)
{
	struct ec_response_typec_status typec_status;

	disconnect_source_from_port(fixture->tcpci_emul, fixture->charger_emul);
	typec_status = host_cmd_typec_status(0);

	zassert_false(typec_status.pd_enabled, NULL);
	zassert_false(typec_status.dev_connected, NULL);
	zassert_false(typec_status.sop_connected, NULL);
	zassert_equal(typec_status.source_cap_count, 0,
		      "Expected 0 source caps, but got %d",
		      typec_status.source_cap_count);
	zassert_equal(typec_status.power_role, USB_CHG_TYPE_NONE,
		      "Expected power role to be %d, but got %d",
		      USB_CHG_TYPE_NONE, typec_status.power_role);
}

ZTEST_F(usb_attach_5v_3a_pd_source, test_disconnect_power_info)
{
	struct ec_response_usb_pd_power_info power_info;

	disconnect_source_from_port(fixture->tcpci_emul, fixture->charger_emul);
	power_info = host_cmd_power_info(0);

	zassert_equal(power_info.role, USB_PD_PORT_POWER_DISCONNECTED,
		      "Expected power role to be %d, but got %d",
		      USB_PD_PORT_POWER_DISCONNECTED, power_info.role);
	zassert_equal(power_info.type, USB_CHG_TYPE_NONE,
		      "Expected charger type to be %d, but got %s",
		      USB_CHG_TYPE_NONE, power_info.type);
	zassert_equal(power_info.max_power, 0,
		      "Expected the maximum power to be 0uW, but got %duW",
		      power_info.max_power);
	zassert_equal(power_info.meas.voltage_max, 0,
		      "Expected maximum voltage of 0mV, but got %dmV",
		      power_info.meas.voltage_max);
	zassert_within(power_info.meas.voltage_now, 5, 5,
		       "Expected present voltage near 0mV, but got %dmV",
		       power_info.meas.voltage_now);
	zassert_equal(power_info.meas.current_max, 0,
		      "Expected maximum current of 0mA, but got %dmA",
		      power_info.meas.current_max);
	zassert_true(power_info.meas.current_lim >= 0,
		     "Expected the PD current limit to be >= 0, but got %dmA",
		     power_info.meas.current_lim);
}

ZTEST_F(usb_attach_5v_3a_pd_source, verify_dock_with_power_button)
{
	/* Clear Alert and Status receive checks */
	tcpci_src_emul_clear_alert_received(&fixture->src_ext);
	tcpci_src_emul_clear_status_received(&fixture->src_ext);
	zassert_false(fixture->src_ext.alert_received, NULL);
	zassert_false(fixture->src_ext.status_received, NULL);

	/* Setting up revision for the full Status message */
	prl_set_rev(TEST_USB_PORT, TCPCI_MSG_SOP, PD_REV30);
	k_sleep(K_MSEC(10));
	pd_dpm_request(TEST_USB_PORT, DPM_REQUEST_GET_REVISION);
	k_sleep(K_MSEC(10));

	/* Suspend and check partner received Alert and Status messages */
	hook_notify(HOOK_CHIPSET_SUSPEND);
	k_sleep(K_SECONDS(2));
	zassert_true(fixture->src_ext.alert_received, NULL);
	zassert_true(fixture->src_ext.status_received, NULL);
	tcpci_src_emul_clear_alert_received(&fixture->src_ext);
	tcpci_src_emul_clear_status_received(&fixture->src_ext);
	zassert_false(fixture->src_ext.alert_received, NULL);
	zassert_false(fixture->src_ext.status_received, NULL);

	/* Shutdown and check partner received Alert and Status messages */
	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	k_sleep(K_SECONDS(2));
	zassert_true(fixture->src_ext.alert_received, NULL);
	zassert_true(fixture->src_ext.status_received, NULL);
	tcpci_src_emul_clear_alert_received(&fixture->src_ext);
	tcpci_src_emul_clear_status_received(&fixture->src_ext);
	zassert_false(fixture->src_ext.alert_received, NULL);
	zassert_false(fixture->src_ext.status_received, NULL);

	/* Startup and check partner received Alert and Status messages */
	hook_notify(HOOK_CHIPSET_STARTUP);
	k_sleep(K_SECONDS(2));
	zassert_true(fixture->src_ext.alert_received, NULL);
	zassert_true(fixture->src_ext.status_received, NULL);
	tcpci_src_emul_clear_alert_received(&fixture->src_ext);
	tcpci_src_emul_clear_status_received(&fixture->src_ext);
	zassert_false(fixture->src_ext.alert_received, NULL);
	zassert_false(fixture->src_ext.status_received, NULL);

	/* Resume and check partner received Alert and Status messages */
	hook_notify(HOOK_CHIPSET_RESUME);
	k_sleep(K_SECONDS(2));
	zassert_true(fixture->src_ext.alert_received, NULL);
	zassert_true(fixture->src_ext.status_received, NULL);
	tcpci_src_emul_clear_alert_received(&fixture->src_ext);
	tcpci_src_emul_clear_status_received(&fixture->src_ext);
	zassert_false(fixture->src_ext.alert_received, NULL);
	zassert_false(fixture->src_ext.status_received, NULL);
}
