/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr.h>
#include <ztest.h>
#include <ztest_assert.h>

#include "emul/emul_syv682x.h"

#include "stubs.h"
#include "syv682x.h"
#include "timer.h"
#include "usbc_ppc.h"

#define SYV682X_ORD DT_DEP_ORD(DT_NODELABEL(syv682x_emul))

static const int syv682x_port = 1;

static void test_ppc_syv682x_vbus_enable(void)
{
	struct i2c_emul *emul = syv682x_emul_get(SYV682X_ORD);
	uint8_t reg;

	zassert_ok(syv682x_emul_get_reg(emul, SYV682X_CONTROL_1_REG, &reg),
			"Reading CONTROL_1 failed");
	zassert_equal(reg & SYV682X_CONTROL_1_PWR_ENB,
			SYV682X_CONTROL_1_PWR_ENB, "VBUS sourcing disabled");
	zassert_false(ppc_is_sourcing_vbus(syv682x_port),
			"PPC sourcing VBUS at beginning of test");

	zassert_ok(ppc_vbus_source_enable(syv682x_port, true),
			"VBUS enable failed");
	zassert_ok(syv682x_emul_get_reg(emul, SYV682X_CONTROL_1_REG, &reg),
			"Reading CONTROL_1 failed");
	zassert_equal(reg & SYV682X_CONTROL_1_PWR_ENB, 0,
			"VBUS sourcing disabled");
	zassert_true(ppc_is_sourcing_vbus(syv682x_port),
			"PPC is not sourcing VBUS after VBUS enabled");
}

static void test_ppc_syv682x_interrupt(void)
{
	struct i2c_emul *emul = syv682x_emul_get(SYV682X_ORD);

	syv682x_emul_set_status(emul, SYV682X_STATUS_OC_5V);
	syv682x_interrupt(syv682x_port);

	/* An OC event less than 100 ms should not cause VBUS to turn off. */
	msleep(50);
	syv682x_interrupt(syv682x_port);
	zassert_true(ppc_is_sourcing_vbus(syv682x_port),
			"PPC is not sourcing VBUS after 50 ms OC");
	/* But one greater than 100 ms should. */
	msleep(60);
	syv682x_interrupt(syv682x_port);
	zassert_false(ppc_is_sourcing_vbus(syv682x_port),
			"PPC is sourcing VBUS after 100 ms OC");

	syv682x_emul_set_status(emul, 0x0);
	/*
	 * TODO(b/190519131): Organize the tests to be more hermetic and avoid
	 * the following issue: The driver triggers overcurrent protection. If
	 * overcurrent protection is triggered 3 times, the TC won't turn the
	 * port back on without a detach. This could frustrate efforts to test
	 * the TC.
	 */

	/*
	 * A TSD event should cause the driver to disable source and sink paths.
	 * (The device will have already physically disabled them.) The state of
	 * the sink path is not part of the driver's API.
	 */
	zassert_ok(ppc_vbus_source_enable(syv682x_port, true),
			"Source enable failed");
	syv682x_emul_set_status(emul, SYV682X_STATUS_TSD);
	syv682x_interrupt(syv682x_port);
	msleep(1);
	zassert_false(ppc_is_sourcing_vbus(syv682x_port),
			"PPC is sourcing power after TSD");
	syv682x_emul_set_status(emul, 0);

	/* An OVP event should cause the driver to disable the source path. */
	zassert_ok(ppc_vbus_source_enable(syv682x_port, true),
			"Source enable failed");
	syv682x_emul_set_status(emul, SYV682X_STATUS_OVP);
	syv682x_interrupt(syv682x_port);
	msleep(1);
	zassert_false(ppc_is_sourcing_vbus(syv682x_port),
			"PPC is sourcing power after OVP");
	syv682x_emul_set_status(emul, 0);
}

static void test_ppc_syv682x(void)
{
	zassert_ok(ppc_init(syv682x_port), "PPC init failed");

	test_ppc_syv682x_vbus_enable();
	test_ppc_syv682x_interrupt();
}

void test_suite_ppc(void)
{
	ztest_test_suite(ppc,
			 ztest_user_unit_test(test_ppc_syv682x));
	ztest_run_test_suite(ppc);
}
