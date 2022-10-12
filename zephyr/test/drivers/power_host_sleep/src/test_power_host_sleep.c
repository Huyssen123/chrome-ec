/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include "ec_commands.h"
#include "host_command.h"
#include "power.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"

#define ARBITRARY_SLEEP_TRANSITIONS 1

/*
 * TODO(b/253224061): Reorganize fakes by public interface
 */
/* Fake to allow full linking */
FAKE_VOID_FUNC(chipset_reset, enum chipset_shutdown_reason);
FAKE_VALUE_FUNC(enum power_state, power_chipset_init);
const struct power_signal_info power_signal_list[] = {};

FAKE_VOID_FUNC(power_chipset_handle_host_sleep_event, enum host_sleep_event,
	       struct host_sleep_event_context *);

/* Per-Test storage of host_sleep_event_context to validate argument values */
static struct host_sleep_event_context test_saved_context;

/* Test-specific custom fake */
static void _test_power_chipset_handle_host_sleep_event(
	enum host_sleep_event state, struct host_sleep_event_context *ctx)
{
	switch (state) {
	case HOST_SLEEP_EVENT_S0IX_RESUME:
	case HOST_SLEEP_EVENT_S3_RESUME:
		ctx->sleep_transitions = ARBITRARY_SLEEP_TRANSITIONS;
		break;

	case HOST_SLEEP_EVENT_S3_SUSPEND:
	case HOST_SLEEP_EVENT_S0IX_SUSPEND:
	case HOST_SLEEP_EVENT_S3_WAKEABLE_SUSPEND:
		break;
	}

	memcpy(&test_saved_context, ctx,
	       sizeof(struct host_sleep_event_context));
}

static void power_host_sleep_before_after(void *test_data)
{
	ARG_UNUSED(test_data);

	RESET_FAKE(power_chipset_handle_host_sleep_event);
	memset(&test_saved_context, 0, sizeof(struct host_sleep_event_context));
}

ZTEST_USER(power_host_sleep, test_non_existent_sleep_event_v1__bad_event)
{
	struct ec_params_host_sleep_event_v1 p = {
		/* No such sleep event */
		.sleep_event = UINT8_MAX,
		/* Non-existent sleep event, so suspend params don't matter */
		.suspend_params = { 0 },
	};
	struct ec_response_host_sleep_event_v1 r;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_HOST_SLEEP_EVENT, 1, r, p);

	/* Clear garbage for verifiable value */
	r.resume_response.sleep_transitions = 0;

	power_chipset_handle_host_sleep_event_fake.custom_fake =
		_test_power_chipset_handle_host_sleep_event;

	zassert_ok(host_command_process(&args));
	zassert_equal(args.response_size, 0);
	zassert_equal(power_chipset_handle_host_sleep_event_fake.call_count, 1);
	zassert_equal(power_chipset_handle_host_sleep_event_fake.arg0_val,
		      p.sleep_event);

	/*
	 * Unknown host sleep events don't retrieve sleep transitions from
	 * chip-specific handler.
	 */
	zassert_equal(r.resume_response.sleep_transitions, 0);
}

ZTEST_USER(power_host_sleep, test_non_existent_sleep_event_v1__s3_suspend)
{
	struct ec_params_host_sleep_event_v1 p = {
		.sleep_event = HOST_SLEEP_EVENT_S3_SUSPEND,
	};
	struct ec_response_host_sleep_event_v1 r;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_HOST_SLEEP_EVENT, 1, r, p);

	/* Set m/lsb of uint16_t to check for type coercion errors */
	p.suspend_params.sleep_timeout_ms = BIT(15) + 1;

	power_chipset_handle_host_sleep_event_fake.custom_fake =
		_test_power_chipset_handle_host_sleep_event;

	zassert_ok(host_command_process(&args));
	zassert_equal(args.response_size, 0);
	zassert_equal(power_chipset_handle_host_sleep_event_fake.call_count, 1);
	zassert_equal(power_chipset_handle_host_sleep_event_fake.arg0_val,
		      p.sleep_event);

	/*
	 * Verify sleep timeout propagated to chip-specific handler to use.
	 */
	zassert_equal(test_saved_context.sleep_timeout_ms,
		      p.suspend_params.sleep_timeout_ms);
}

ZTEST_USER(power_host_sleep, test_non_existent_sleep_event_v1__s3_resume)
{
	struct ec_params_host_sleep_event_v1 p = {
		.sleep_event = HOST_SLEEP_EVENT_S3_RESUME,
	};
	struct ec_response_host_sleep_event_v1 r;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_HOST_SLEEP_EVENT, 1, r, p);

	power_chipset_handle_host_sleep_event_fake.custom_fake =
		_test_power_chipset_handle_host_sleep_event;

	zassert_ok(host_command_process(&args));
	zassert_equal(args.response_size, sizeof(r));
	zassert_equal(power_chipset_handle_host_sleep_event_fake.call_count, 1);
	zassert_equal(power_chipset_handle_host_sleep_event_fake.arg0_val,
		      p.sleep_event);

	/*
	 * Verify sleep context propagated from chip-specific handler.
	 */
	zassert_equal(r.resume_response.sleep_transitions,
		      ARBITRARY_SLEEP_TRANSITIONS);
}

ZTEST_SUITE(power_host_sleep, drivers_predicate_post_main, NULL,
	    power_host_sleep_before_after, power_host_sleep_before_after, NULL);
