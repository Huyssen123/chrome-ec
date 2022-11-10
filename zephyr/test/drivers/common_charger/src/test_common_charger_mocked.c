/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <string.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include "charger.h"
#include "test/drivers/charger_utils.h"
#include "test/drivers/test_state.h"

/* This test suite only works if the chg_chips array is not const. */
BUILD_ASSERT(IS_ENABLED(CONFIG_PLATFORM_EC_CHARGER_RUNTIME_CONFIG),
	     "chg_chips array cannot be const.");

/** Index of the charger chip we are overriding / working with. */
#define CHG_NUM (0)

/* FFF fakes for driver functions. These get assigned to members of the
 * charger_drv struct
 */
FAKE_VALUE_FUNC(enum ec_error_list, enable_otg_power, int, int);
FAKE_VALUE_FUNC(enum ec_error_list, set_otg_current_voltage, int, int, int);
FAKE_VALUE_FUNC(int, is_sourcing_otg_power, int, int);
FAKE_VALUE_FUNC(enum ec_error_list, get_actual_current, int, int *);
FAKE_VALUE_FUNC(enum ec_error_list, get_actual_voltage, int, int *);
FAKE_VALUE_FUNC(enum ec_error_list, set_voltage, int, int);

struct common_charger_mocked_driver_fixture {
	/* The original driver pointer that gets restored after the tests */
	const struct charger_drv *saved_driver_ptr;
	/* Mock driver that gets substituted */
	struct charger_drv mock_driver;
};

ZTEST(common_charger_mocked_driver, test_charger_enable_otg_power__invalid)
{
	/* charger number out of bounds */
	zassert_equal(EC_ERROR_INVAL, charger_enable_otg_power(-1, 0));
	zassert_equal(EC_ERROR_INVAL, charger_enable_otg_power(INT_MAX, 0));
}

ZTEST(common_charger_mocked_driver, test_charger_enable_otg_power__unimpl)
{
	/* enable_otg_power is NULL */
	zassert_equal(EC_ERROR_UNIMPLEMENTED,
		      charger_enable_otg_power(CHG_NUM, 1));
}

ZTEST_F(common_charger_mocked_driver, test_charger_enable_otg_power)
{
	fixture->mock_driver.enable_otg_power = enable_otg_power;
	enable_otg_power_fake.return_val = 123;

	zassert_equal(enable_otg_power_fake.return_val,
		      charger_enable_otg_power(CHG_NUM, 1));

	zassert_equal(1, enable_otg_power_fake.call_count);
	zassert_equal(CHG_NUM, enable_otg_power_fake.arg0_history[0]);
	zassert_equal(1, enable_otg_power_fake.arg1_history[0]);
}

ZTEST(common_charger_mocked_driver,
      test_charger_set_otg_current_voltage__invalid)
{
	/* charger number out of bounds */
	zassert_equal(EC_ERROR_INVAL,
		      charger_set_otg_current_voltage(-1, 0, 0));
	zassert_equal(EC_ERROR_INVAL,
		      charger_set_otg_current_voltage(INT_MAX, 0, 0));
}

ZTEST(common_charger_mocked_driver,
      test_charger_set_otg_current_voltage__unimpl)
{
	/* set_otg_current_voltage is NULL */
	zassert_equal(EC_ERROR_UNIMPLEMENTED,
		      charger_set_otg_current_voltage(CHG_NUM, 0, 0));
}

ZTEST_F(common_charger_mocked_driver, test_charger_set_otg_current_voltage)
{
	fixture->mock_driver.set_otg_current_voltage = set_otg_current_voltage;
	set_otg_current_voltage_fake.return_val = 123;

	zassert_equal(set_otg_current_voltage_fake.return_val,
		      charger_set_otg_current_voltage(CHG_NUM, 10, 20));

	zassert_equal(1, set_otg_current_voltage_fake.call_count);
	zassert_equal(CHG_NUM, set_otg_current_voltage_fake.arg0_history[0]);
	zassert_equal(10, set_otg_current_voltage_fake.arg1_history[0]);
	zassert_equal(20, set_otg_current_voltage_fake.arg2_history[0]);
}

ZTEST(common_charger_mocked_driver, test_charger_is_sourcing_otg_power__invalid)
{
	/* is_sourcing_otg_power is NULL */
	zassert_equal(0, charger_is_sourcing_otg_power(0));
}

ZTEST_F(common_charger_mocked_driver, test_charger_is_sourcing_otg_power)
{
	fixture->mock_driver.is_sourcing_otg_power = is_sourcing_otg_power;
	is_sourcing_otg_power_fake.return_val = 123;

	zassert_equal(is_sourcing_otg_power_fake.return_val,
		      charger_is_sourcing_otg_power(0));

	zassert_equal(1, is_sourcing_otg_power_fake.call_count);
}

ZTEST(common_charger_mocked_driver, test_charger_get_actual_current__invalid)
{
	/* charger number out of bounds */
	zassert_equal(EC_ERROR_INVAL, charger_get_actual_current(-1, NULL));
	zassert_equal(EC_ERROR_INVAL,
		      charger_get_actual_current(INT_MAX, NULL));
}

ZTEST(common_charger_mocked_driver, test_charger_get_actual_current__unimpl)
{
	/* get_actual_current is NULL */
	zassert_equal(EC_ERROR_UNIMPLEMENTED,
		      charger_get_actual_current(CHG_NUM, NULL));
}

/**
 * @brief Custom fake for get_actual_current that can write to the output param
 */
static enum ec_error_list get_actual_current_custom_fake(int chgnum,
							 int *current)
{
	ARG_UNUSED(chgnum);

	*current = 1000;

	return EC_SUCCESS;
}

ZTEST_F(common_charger_mocked_driver, test_charger_get_actual_current)
{
	int current;

	fixture->mock_driver.get_actual_current = get_actual_current;
	get_actual_current_fake.custom_fake = get_actual_current_custom_fake;

	zassert_equal(EC_SUCCESS,
		      charger_get_actual_current(CHG_NUM, &current));

	zassert_equal(1, get_actual_current_fake.call_count);
	zassert_equal(CHG_NUM, get_actual_current_fake.arg0_history[0]);
	zassert_equal(1000, current);
}

ZTEST(common_charger_mocked_driver, test_charger_get_actual_voltage__invalid)
{
	/* charger number out of bounds */
	zassert_equal(EC_ERROR_INVAL, charger_get_actual_voltage(-1, NULL));
	zassert_equal(EC_ERROR_INVAL,
		      charger_get_actual_voltage(INT_MAX, NULL));
}

ZTEST(common_charger_mocked_driver, test_charger_get_actual_voltage__unimpl)
{
	/* get_actual_voltage is NULL */
	zassert_equal(EC_ERROR_UNIMPLEMENTED,
		      charger_get_actual_voltage(CHG_NUM, NULL));
}

/**
 * @brief Custom fake for get_actual_voltage that can write to the output param
 */
static enum ec_error_list get_actual_voltage_custom_fake(int chgnum,
							 int *voltage)
{
	ARG_UNUSED(chgnum);

	*voltage = 2000;

	return EC_SUCCESS;
}

ZTEST_F(common_charger_mocked_driver, test_charger_get_actual_voltage)
{
	int voltage;

	fixture->mock_driver.get_actual_voltage = get_actual_voltage;
	get_actual_voltage_fake.custom_fake = get_actual_voltage_custom_fake;

	zassert_equal(EC_SUCCESS,
		      charger_get_actual_voltage(CHG_NUM, &voltage));

	zassert_equal(1, get_actual_voltage_fake.call_count);
	zassert_equal(CHG_NUM, get_actual_voltage_fake.arg0_history[0]);
	zassert_equal(2000, voltage);
}

ZTEST(common_charger_mocked_driver, test_charger_set_voltage__invalid)
{
	/* charger number out of bounds */
	zassert_equal(EC_ERROR_INVAL, charger_set_voltage(-1, 0));
	zassert_equal(EC_ERROR_INVAL, charger_set_voltage(INT_MAX, 0));
}

ZTEST(common_charger_mocked_driver, test_charger_set_voltage__unimpl)
{
	/* set_voltage is NULL */
	zassert_equal(EC_ERROR_UNIMPLEMENTED, charger_set_voltage(CHG_NUM, 0));
}

ZTEST_F(common_charger_mocked_driver, test_charger_set_voltage)
{
	fixture->mock_driver.set_voltage = set_voltage;
	set_voltage_fake.return_val = 123;

	zassert_equal(set_voltage_fake.return_val,
		      charger_set_voltage(CHG_NUM, 2000));

	zassert_equal(1, set_voltage_fake.call_count);
	zassert_equal(CHG_NUM, set_voltage_fake.arg0_history[0]);
	zassert_equal(2000, set_voltage_fake.arg1_history[0]);
}

static void *setup(void)
{
	static struct common_charger_mocked_driver_fixture f;

	zassert_true(board_get_charger_chip_count() > 0,
		     "Need at least one charger chip present.");

	/* Back up the current charger driver and substitute our own */
	f.saved_driver_ptr = chg_chips[CHG_NUM].drv;
	chg_chips[CHG_NUM].drv = &f.mock_driver;

	return &f;
}

static void reset(void *data)
{
	struct common_charger_mocked_driver_fixture *f = data;

	/* Reset the mock driver's function pointer table. Each tests adds these
	 * as-needed
	 */
	f->mock_driver = (struct charger_drv){ 0 };

	/* Reset fakes */
	RESET_FAKE(enable_otg_power);
	RESET_FAKE(set_otg_current_voltage);
	RESET_FAKE(is_sourcing_otg_power);
	RESET_FAKE(get_actual_current);
	RESET_FAKE(get_actual_voltage);
	RESET_FAKE(set_voltage);
}

static void teardown(void *data)
{
	struct common_charger_mocked_driver_fixture *f = data;

	/* Restore the original driver */
	chg_chips[CHG_NUM].drv = f->saved_driver_ptr;
}

ZTEST_SUITE(common_charger_mocked_driver, drivers_predicate_post_main, setup,
	    reset, reset, teardown);
