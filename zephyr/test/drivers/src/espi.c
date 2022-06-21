/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/zephyr.h>
#include <ztest.h>

#include "ec_commands.h"
#include "host_command.h"
#include "test/drivers/test_state.h"


#define PORT 0

ZTEST_USER(espi, test_host_command_get_protocol_info)
{
	struct ec_response_get_protocol_info response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_GET_PROTOCOL_INFO, 0,
					    response);

	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	zassert_equal(args.response_size, sizeof(response), NULL);
	zassert_equal(response.protocol_versions, BIT(3), NULL);
	zassert_equal(response.max_request_packet_size, EC_LPC_HOST_PACKET_SIZE,
		      NULL);
	zassert_equal(response.max_response_packet_size,
		      EC_LPC_HOST_PACKET_SIZE, NULL);
	zassert_equal(response.flags, 0, NULL);
}

ZTEST_USER(espi, test_host_command_usb_pd_power_info)
{
	/* Only test we've enabled the command */
	struct ec_response_usb_pd_power_info response;
	struct ec_params_usb_pd_power_info params = { .port = PORT };
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_USB_PD_POWER_INFO, 0, response, params);

	args.params = &params;
	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	zassert_equal(args.response_size, sizeof(response), NULL);
}

ZTEST_USER(espi, test_host_command_typec_status)
{
	/* Only test we've enabled the command */
	struct ec_params_typec_status params = { .port = PORT };
	struct ec_response_typec_status response;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_TYPEC_STATUS, 0, response, params);

	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	zassert_equal(args.response_size, sizeof(response), NULL);
}

ZTEST_USER(espi, test_host_command_usb_pd_get_amode)
{
	/* Only test we've enabled the command */
	struct ec_params_usb_pd_get_mode_request params = {
		.port = PORT,
		.svid_idx = 0,
	};
	struct ec_params_usb_pd_get_mode_response response;
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_USB_PD_GET_AMODE, 0, response, params);

	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	/* Note: with no SVIDs the response size is the size of the svid field.
	 * See the usb alt mode test for verifying larger struct sizes
	 *
	 * TODO(b/219562077): Add the above described test.
	 */
	zassert_equal(args.response_size, sizeof(response.svid), NULL);
}

ZTEST_SUITE(espi, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
