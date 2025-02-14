/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "motion_sense.h"
#include "test/drivers/test_state.h"

#include <zephyr/ztest.h>

extern enum chipset_state_mask sensor_active;

ZTEST_SUITE(motion_sense, drivers_predicate_post_main, NULL, NULL, NULL, NULL);

ZTEST_USER(motion_sense, test_ec_motion_sensor_fill_values)
{
	struct ec_response_motion_sensor_data dst = {
		.data = { 1, 2, 3 },
	};
	const int32_t v[] = { 4, 5, 6 };

	ec_motion_sensor_fill_values(&dst, v);
	zassert_equal(dst.data[0], v[0]);
	zassert_equal(dst.data[1], v[1]);
	zassert_equal(dst.data[2], v[2]);
}

ZTEST_USER(motion_sense, test_ec_motion_sensor_clamp_i16)
{
	zassert_equal(ec_motion_sensor_clamp_i16(0), 0);
	zassert_equal(ec_motion_sensor_clamp_i16(200), 200);
	zassert_equal(ec_motion_sensor_clamp_i16(-512), -512);
	zassert_equal(ec_motion_sensor_clamp_i16(INT16_MAX + 1), INT16_MAX,
		      NULL);
	zassert_equal(ec_motion_sensor_clamp_i16(INT16_MIN - 1), INT16_MIN,
		      NULL);
}

ZTEST_USER(motion_sense, test_ec_motion_sense_get_ec_config)
{
	/* illegal state, should be translated to S5 */
	sensor_active = 42;
	zassert_equal(motion_sense_get_ec_config(), SENSOR_CONFIG_EC_S5);
	/* all valid states */
	sensor_active = SENSOR_ACTIVE_S0;
	zassert_equal(motion_sense_get_ec_config(), SENSOR_CONFIG_EC_S0);
	sensor_active = SENSOR_ACTIVE_S3;
	zassert_equal(motion_sense_get_ec_config(), SENSOR_CONFIG_EC_S3);
	sensor_active = SENSOR_ACTIVE_S5;
	zassert_equal(motion_sense_get_ec_config(), SENSOR_CONFIG_EC_S5);
}
