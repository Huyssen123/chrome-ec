/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/zephyr.h>
#include <zephyr/ztest.h>
#include "ec_app_main.h"
#include "test/drivers/test_state.h"

bool drivers_predicate_pre_main(const void *state)
{
	return ((struct test_state *)state)->ec_app_main_run == false;
}

bool drivers_predicate_post_main(const void *state)
{
	return !drivers_predicate_pre_main(state);
}

void test_main(void)
{
	struct test_state state = {
		.ec_app_main_run = false,
	};

	/* Run all the suites that depend on main not being called yet */
	ztest_run_all(&state);

	ec_app_main();
	state.ec_app_main_run = true;

	/* Run all the suites that depend on main being called */
	ztest_run_all(&state);
}
