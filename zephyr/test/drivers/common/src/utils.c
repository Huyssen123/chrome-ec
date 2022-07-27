/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/zephyr.h>
#include <ztest.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/drivers/gpio/gpio_emul.h>

#include "battery.h"
#include "battery_smart.h"
#include "charge_state.h"
#include "emul/emul_isl923x.h"
#include "emul/emul_smart_battery.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "hooks.h"
#include "power.h"
#include "test/drivers/stubs.h"
#include "chipset.h"
#include "test/drivers/utils.h"

#define BATTERY_ORD DT_DEP_ORD(DT_NODELABEL(battery))
#define GPIO_BATT_PRES_ODL_PATH DT_PATH(named_gpios, ec_batt_pres_odl)
#define GPIO_BATT_PRES_ODL_PORT DT_GPIO_PIN(GPIO_BATT_PRES_ODL_PATH, gpios)

void test_set_chipset_to_s0(void)
{
	struct sbat_emul_bat_data *bat;
	struct i2c_emul *emul;
	const struct device *battery_gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_BATT_PRES_ODL_PATH, gpios));

	printk("%s: Forcing power on\n", __func__);
	emul = sbat_emul_get_ptr(BATTERY_ORD);
	bat = sbat_emul_get_bat_data(emul);

	/*
	 * Make sure that battery is in good condition to
	 * not trigger hibernate in charge_state_v2.c
	 * Set battery voltage to expected value and capacity to 75%. Battery
	 * will not be full and accepts charging, but will not trigger
	 * hibernate. Charge level is chosen arbitrary.
	 */
	bat->cap = bat->full_cap * 3 / 4;
	bat->volt = battery_get_info()->voltage_normal;
	bat->design_mv = bat->volt;

	/* Set battery present gpio. */
	zassert_ok(gpio_emul_input_set(battery_gpio_dev,
				       GPIO_BATT_PRES_ODL_PORT, 0),
		   NULL);

	/* We need to wait for the charge task to re-read battery parameters */
	WAIT_FOR(!charge_want_shutdown(), CHARGE_MAX_SLEEP_USEC + 1,
		 k_sleep(K_SECONDS(1)));

	/* The easiest way to power on seems to be the shell command. */
	zassert_equal(EC_SUCCESS, shell_execute_cmd(get_ec_shell(), "power on"),
		      NULL);

	k_sleep(K_SECONDS(1));

	/* Check if chipset is in correct state */
	zassert_equal(POWER_S0, power_get_state(), "Expected S0, got %d",
		      power_get_state());
}

void test_set_chipset_to_power_level(enum power_state new_state)
{
	zassert_true(new_state == POWER_G3 || new_state == POWER_S5 ||
			     new_state == POWER_S4 || new_state == POWER_S3 ||
			     new_state == POWER_S0
#ifdef CONFIG_POWER_S0IX
			     || new_state == POWER_S0ix
#endif
		     ,
		     "Power state must be one of the steady states");

	if (new_state == POWER_G3) {
		test_set_chipset_to_g3();
		return;
	}

	test_set_chipset_to_s0();

	power_set_state(new_state);

	k_sleep(K_SECONDS(1));

	/* Check if chipset is in correct state */
	zassert_equal(new_state, power_get_state(), "Expected %d, got %d",
		      new_state, power_get_state());
}

void test_set_chipset_to_g3(void)
{
	printk("%s: Forcing shutdown\n", __func__);
	chipset_force_shutdown(CHIPSET_RESET_KB_SYSRESET);
	k_sleep(K_SECONDS(20));
	/* Check if chipset is in correct state */
	zassert_equal(POWER_G3, power_get_state(), "Expected G3, got %d",
		      power_get_state());
}

void connect_source_to_port(struct tcpci_partner_data *partner,
			    struct tcpci_src_emul_data *src, int pdo_index,
			    const struct emul *tcpci_emul,
			    const struct emul *charger_emul)
{
	set_ac_enabled(true);
	zassume_ok(tcpci_partner_connect_to_tcpci(partner, tcpci_emul), NULL);

	isl923x_emul_set_adc_vbus(charger_emul,
				  PDO_FIXED_GET_VOLT(src->pdo[pdo_index]));

	k_sleep(K_SECONDS(10));
}

void disconnect_source_from_port(const struct emul *tcpci_emul,
				 const struct emul *charger_emul)
{
	set_ac_enabled(false);
	zassume_ok(tcpci_emul_disconnect_partner(tcpci_emul), NULL);
	isl923x_emul_set_adc_vbus(charger_emul, 0);
	k_sleep(K_SECONDS(1));
}

void host_cmd_motion_sense_dump(int max_sensor_count,
				struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_DUMP,
		.dump = {
			.max_sensor_count = max_sensor_count,
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 4, *response, params);

	zassume_ok(host_command_process(&args),
		   "Failed to get motion_sense dump");
}

int host_cmd_motion_sense_data(uint8_t sensor_num,
			       struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_DATA,
		.sensor_odr = {
			.sensor_num = sensor_num,
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 4, *response, params);

	return host_command_process(&args);
}

int host_cmd_motion_sense_info(uint8_t cmd_version, uint8_t sensor_num,
			       struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_INFO,
		.sensor_odr = {
			.sensor_num = sensor_num,
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, cmd_version, *response, params);

	return host_command_process(&args);
}

int host_cmd_motion_sense_ec_rate(uint8_t sensor_num, int data_rate_ms,
				  struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_EC_RATE,
		.ec_rate = {
			.sensor_num = sensor_num,
			.data = data_rate_ms,
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 1, *response, params);

	return host_command_process(&args);
}

int host_cmd_motion_sense_odr(uint8_t sensor_num, int32_t odr, bool round_up,
			      struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_SENSOR_ODR,
		.sensor_odr = {
			.sensor_num = sensor_num,
			.data = odr,
			.roundup = round_up,
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 1, *response, params);

	return host_command_process(&args);
}

int host_cmd_motion_sense_range(uint8_t sensor_num, int32_t range,
				bool round_up,
				struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_SENSOR_RANGE,
		.sensor_range = {
			.sensor_num = sensor_num,
			.data = range,
			.roundup = round_up,
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 1, *response, params);

	return host_command_process(&args);
}

int host_cmd_motion_sense_offset(uint8_t sensor_num, uint16_t flags,
				 int16_t temperature, int16_t offset_x,
				 int16_t offset_y, int16_t offset_z,
				 struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_SENSOR_OFFSET,
		.sensor_offset = {
			.sensor_num = sensor_num,
			.flags = flags,
			.temp = temperature,
			.offset = { offset_x, offset_y, offset_z },
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 1, *response, params);

	return host_command_process(&args);
}

int host_cmd_motion_sense_scale(uint8_t sensor_num, uint16_t flags,
				int16_t temperature, int16_t scale_x,
				int16_t scale_y, int16_t scale_z,
				struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_SENSOR_SCALE,
		.sensor_scale = {
			.sensor_num = sensor_num,
			.flags = flags,
			.temp = temperature,
			.scale = { scale_x, scale_y, scale_z },
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 1, *response, params);

	return host_command_process(&args);
}

int host_cmd_motion_sense_calib(uint8_t sensor_num, bool enable,
				struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_PERFORM_CALIB,
		.perform_calib = {
			.sensor_num = sensor_num,
			.enable = enable,
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 1, *response, params);

	return host_command_process(&args);
}

int host_cmd_motion_sense_fifo_flush(uint8_t sensor_num,
				     struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_FIFO_FLUSH,
		.sensor_odr = {
			.sensor_num = sensor_num,
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 1, *response, params);

	return host_command_process(&args);
}

int host_cmd_motion_sense_fifo_info(struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_FIFO_INFO,
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 1, *response, params);

	return host_command_process(&args);
}

int host_cmd_motion_sense_fifo_read(uint8_t buffer_length,
				    struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_FIFO_READ,
		.fifo_read = {
			.max_data_vector = buffer_length,
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 1, *response, params);

	return host_command_process(&args);
}

int host_cmd_motion_sense_int_enable(int8_t enable,
				     struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_FIFO_INT_ENABLE,
		.fifo_int_enable = {
			.enable = enable,
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 1, *response, params);

	return host_command_process(&args);
}

int host_cmd_motion_sense_spoof(uint8_t sensor_num, uint8_t enable,
				int16_t values0, int16_t values1,
				int16_t values2,
				struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_SPOOF,
		.spoof = {
			.sensor_id = sensor_num,
			.spoof_enable = enable,
			.components = { values0, values1, values2 },
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 1, *response, params);

	return host_command_process(&args);
}

void host_cmd_typec_discovery(int port, enum typec_partner_type partner_type,
			      void *response, size_t response_size)
{
	struct ec_params_typec_discovery params = {
		.port = port, .partner_type = partner_type
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_TYPEC_DISCOVERY, 0, params);
	/* The expected response to EC_CMD_TYPEC_DISCOVERY extends beyond the
	 * bounds of struct ec_response_typec_discovery.
	 */
	args.response = response;
	args.response_max = response_size;

	zassume_ok(host_command_process(&args),
		   "Failed to get Type-C state for port %d", port);
}

void host_cmd_typec_control_enter_mode(int port, enum typec_mode mode)
{
	struct ec_params_typec_control params = {
		.port = port,
		.command = TYPEC_CONTROL_COMMAND_ENTER_MODE,
		.mode_to_enter = mode
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_TYPEC_CONTROL, 0, params);

	zassume_ok(host_command_process(&args),
		   "Failed to send Type-C control for port %d", port);
}

void host_cmd_typec_control_exit_modes(int port)
{
	struct ec_params_typec_control params = {
		.port = port, .command = TYPEC_CONTROL_COMMAND_EXIT_MODES
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_TYPEC_CONTROL, 0, params);

	zassume_ok(host_command_process(&args),
		   "Failed to send Type-C control for port %d", port);
}

void host_cmd_typec_control_usb_mux_set(int port,
					struct typec_usb_mux_set mux_set)
{
	struct ec_params_typec_control params = {
		.port = port,
		.command = TYPEC_CONTROL_COMMAND_USB_MUX_SET,
		.mux_params = mux_set,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_TYPEC_CONTROL, 0, params);

	zassume_ok(host_command_process(&args),
		   "Failed to send Type-C control for port %d", port);
}

void host_cmd_typec_control_clear_events(int port, uint32_t events)
{
	struct ec_params_typec_control params = {
		.port = port,
		.command = TYPEC_CONTROL_COMMAND_CLEAR_EVENTS,
		.clear_events_mask = events,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_TYPEC_CONTROL, 0, params);

	zassume_ok(host_command_process(&args),
		   "Failed to send Type-C control for port %d", port);
}

void host_cmd_usb_pd_get_amode(
	uint8_t port, uint16_t svid_idx,
	struct ec_params_usb_pd_get_mode_response *response, int *response_size)
{
	struct ec_params_usb_pd_get_mode_request params = {
		.port = port,
		.svid_idx = svid_idx,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_USB_PD_GET_AMODE, 0, params);
	args.response = response;

	zassume_ok(host_command_process(&args),
		   "Failed to get alternate-mode info for port %d", port);
	*response_size = args.response_size;
}

K_HEAP_DEFINE(test_heap, 2048);

void *test_malloc(size_t bytes)
{
	void *mem;

	mem = k_heap_alloc(&test_heap, bytes, K_NO_WAIT);

	if (mem == NULL) {
		printk("Failed to alloc %d bytes\n", bytes);
	}

	return mem;
}

void test_free(void *mem)
{
	k_heap_free(&test_heap, mem);
}

static struct k_poll_signal shutdown_complete_signal =
	K_POLL_SIGNAL_INITIALIZER(shutdown_complete_signal);
static struct k_poll_event shutdown_complete_event = K_POLL_EVENT_INITIALIZER(
	K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &shutdown_complete_signal);

static void handle_chipset_shutdown_complete_event(void)
{
	k_poll_signal_raise(&shutdown_complete_signal, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN_COMPLETE,
	     handle_chipset_shutdown_complete_event, HOOK_PRIO_LAST);

void test_set_chipset_to_g3_then_transition_to_s5(void)
{
	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		k_poll_signal_reset(&shutdown_complete_signal);
		chipset_force_shutdown(CHIPSET_RESET_INIT);
		k_poll(&shutdown_complete_event, 1, K_MSEC(1000));
	}

	/*
	 * Signal will trigger during S3->S5, but we want to wait until we're
	 * actually at S5.  Give it a quick sleep if required.
	 */
	WAIT_FOR(!chipset_in_state(CHIPSET_STATE_ANY_OFF), 1000000 /* 1s */,
		 k_msleep(5));

	/*
	 * TODO(b/236726670): Why do we need to sleep after restarting chipset?
	 */
	k_sleep(K_SECONDS(1));
}
