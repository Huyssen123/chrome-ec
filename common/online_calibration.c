/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accel_cal.h"
#include "accelgyro.h"
#include "atomic.h"
#include "common.h"
#include "ec_commands.h"
#include "gyro_cal.h"
#include "hwtimer.h"
#include "mag_cal.h"
#include "mkbp_event.h"
#include "online_calibration.h"
#include "task.h"
#include "util.h"
#include "vec3.h"

#define CPRINTS(format, args...) cprints(CC_MOTION_SENSE, format, ##args)

#ifndef CONFIG_MKBP_EVENT
#error "Must use CONFIG_MKBP_EVENT for online calibration"
#endif /* CONFIG_MKBP_EVENT */

/** Bitmap telling which online calibration values are valid. */
static uint32_t sensor_calib_cache_valid_map;
/** Bitmap telling which online calibration values are dirty. */
static uint32_t sensor_calib_cache_dirty_map;

struct mutex g_calib_cache_mutex;

static int get_temperature(struct motion_sensor_t *sensor, int *temp)
{
	struct online_calib_data *entry = sensor->online_calib_data;
	uint32_t now;

	if (sensor->drv->read_temp == NULL)
		return EC_ERROR_UNIMPLEMENTED;

	now = __hw_clock_source_read();
	if (entry->last_temperature < 0 ||
	    time_until(entry->last_temperature_timestamp, now) >
		    CONFIG_TEMP_CACHE_STALE_THRES) {
		int t;
		int rc = sensor->drv->read_temp(sensor, &t);

		if (rc == EC_SUCCESS) {
			entry->last_temperature = t;
			entry->last_temperature_timestamp = now;
		} else {
			return rc;
		}
	}

	*temp = entry->last_temperature;
	return EC_SUCCESS;
}

static void data_int16_to_fp(const struct motion_sensor_t *s,
			     const int16_t *data, fpv3_t out)
{
	int i;
	fp_t range = INT_TO_FP(s->current_range);

	for (i = 0; i < 3; ++i) {
		fp_t v = INT_TO_FP((int32_t)data[i]);

		out[i] = fp_div(v, INT_TO_FP((data[i] >= 0) ? 0x7fff : 0x8000));
		out[i] = fp_mul(out[i], range);
		/* Check for overflow */
		out[i] = CLAMP(out[i], -range, range);
	}
}

static void data_fp_to_int16(const struct motion_sensor_t *s, const fpv3_t data,
			     int16_t *out)
{
	int i;
	fp_t range = INT_TO_FP(s->current_range);

	for (i = 0; i < 3; ++i) {
		int32_t iv;
		fp_t v = fp_div(data[i], range);

		v = fp_mul(v, INT_TO_FP(0x7fff));
		iv = FP_TO_INT(v);
		/* Check for overflow */
		out[i] = ec_motion_sensor_clamp_i16(iv);
	}
}

/**
 * Check a gyroscope for new bias. This function checks a given sensor (must be
 * a gyroscope) for new bias values. If found, it will update the appropriate
 * caches and notify the AP.
 *
 * @param sensor Pointer to the gyroscope sensor to check.
 */
static bool check_gyro_cal_new_bias(struct motion_sensor_t *sensor,
				    fpv3_t bias_out)
{
	struct online_calib_data *calib_data =
		(struct online_calib_data *)sensor->online_calib_data;
	struct gyro_cal_data *data =
		(struct gyro_cal_data *)calib_data->type_specific_data;
	int temp_out;
	uint32_t timestamp_out;

	/* Check that we have a new bias. */
	if (data == NULL || calib_data == NULL ||
	    !gyro_cal_new_bias_available(&data->gyro_cal))
		return false;

	/* Read the calibration values. */
	gyro_cal_get_bias(&data->gyro_cal, bias_out, &temp_out, &timestamp_out);
	return true;
}

static void set_gyro_cal_cache_values(struct motion_sensor_t *sensor,
				      fpv3_t bias)
{
	size_t sensor_num = sensor - motion_sensors;
	struct online_calib_data *calib_data =
		(struct online_calib_data *)sensor->online_calib_data;

	mutex_lock(&g_calib_cache_mutex);
	/* Convert result to the right scale and save to cache. */
	data_fp_to_int16(sensor, bias, calib_data->cache);
	/* Set valid and dirty. */
	sensor_calib_cache_valid_map |= BIT(sensor_num);
	sensor_calib_cache_dirty_map |= BIT(sensor_num);
	mutex_unlock(&g_calib_cache_mutex);
	/* Notify the AP. */
	mkbp_send_event(EC_MKBP_EVENT_ONLINE_CALIBRATION);
}

/**
 * Update the data stream (accel/mag) for a given sensor and data in all
 * gyroscopes that are interested.
 *
 * @param sensor Pointer to the sensor that generated the data.
 * @param data 3 floats/fixed point data points generated by the sensor.
 * @param timestamp The timestamp at which the data was generated.
 */
static void update_gyro_cal(struct motion_sensor_t *sensor, fpv3_t data,
			    uint32_t timestamp)
{
	int i;
	fpv3_t gyro_cal_data_out;

	/*
	 * Find gyroscopes, while we don't currently have instance where more
	 * than one are present in a board, this loop will work with any number
	 * of them.
	 */
	for (i = 0; i < SENSOR_COUNT; ++i) {
		struct motion_sensor_t *s = motion_sensors + i;
		struct gyro_cal_data *gyro_cal_data =
			(struct gyro_cal_data *)
				s->online_calib_data->type_specific_data;
		bool has_new_gyro_cal_bias = false;

		/*
		 * If we're not looking at a gyroscope OR if the calibration
		 * data is NULL, skip this sensor.
		 */
		if (s->type != MOTIONSENSE_TYPE_GYRO || gyro_cal_data == NULL)
			continue;

		/*
		 * Update the appropriate data stream (accel/mag) depending on
		 * which sensors the gyroscope is tracking.
		 */
		if (sensor->type == MOTIONSENSE_TYPE_ACCEL &&
		    gyro_cal_data->accel_sensor_id == sensor - motion_sensors) {
			gyro_cal_update_accel(&gyro_cal_data->gyro_cal,
					      timestamp, data[X], data[Y],
					      data[Z]);
			has_new_gyro_cal_bias =
				check_gyro_cal_new_bias(s, gyro_cal_data_out);
		} else if (sensor->type == MOTIONSENSE_TYPE_MAG &&
			   gyro_cal_data->mag_sensor_id ==
				   sensor - motion_sensors) {
			gyro_cal_update_mag(&gyro_cal_data->gyro_cal, timestamp,
					    data[X], data[Y], data[Z]);
			has_new_gyro_cal_bias =
				check_gyro_cal_new_bias(s, gyro_cal_data_out);
		}

		if (has_new_gyro_cal_bias)
			set_gyro_cal_cache_values(s, gyro_cal_data_out);
	}
}

void online_calibration_init(void)
{
	size_t i;

	for (i = 0; i < SENSOR_COUNT; i++) {
		struct motion_sensor_t *s = motion_sensors + i;
		void *type_specific_data = NULL;

		s->online_calib_data->last_temperature = -1;
		type_specific_data = s->online_calib_data->type_specific_data;

		if (!type_specific_data)
			continue;

		switch (s->type) {
		case MOTIONSENSE_TYPE_ACCEL: {
			accel_cal_reset((struct accel_cal *)type_specific_data);
			break;
		}
		case MOTIONSENSE_TYPE_MAG: {
			init_mag_cal((struct mag_cal_t *)type_specific_data);
			break;
		}
		case MOTIONSENSE_TYPE_GYRO: {
			init_gyro_cal(
				&((struct gyro_cal_data *)type_specific_data)
					 ->gyro_cal);
			break;
		}
		default:
			break;
		}
	}
}

bool online_calibration_has_new_values(void)
{
	bool has_dirty;

	mutex_lock(&g_calib_cache_mutex);
	has_dirty = sensor_calib_cache_dirty_map != 0;
	mutex_unlock(&g_calib_cache_mutex);

	return has_dirty;
}

bool online_calibration_read(struct motion_sensor_t *sensor,
			     struct ec_response_online_calibration_data *out)
{
	int sensor_num = sensor - motion_sensors;
	bool has_valid;

	mutex_lock(&g_calib_cache_mutex);
	has_valid = (sensor_calib_cache_valid_map & BIT(sensor_num)) != 0;
	if (has_valid) {
		/* Update data in out */
		memcpy(out->data, sensor->online_calib_data->cache,
		       sizeof(out->data));
		/* Clear dirty bit */
		sensor_calib_cache_dirty_map &= ~(1 << sensor_num);
	}
	mutex_unlock(&g_calib_cache_mutex);

	return has_valid;
}

int online_calibration_process_data(struct ec_response_motion_sensor_data *data,
				    struct motion_sensor_t *sensor,
				    uint32_t timestamp)
{
	int sensor_num = sensor - motion_sensors;
	int rc;
	int temperature;
	struct online_calib_data *calib_data;
	fpv3_t fdata;
	bool is_spoofed = IS_ENABLED(CONFIG_ONLINE_CALIB_SPOOF_MODE) &&
			  (sensor->flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE);
	bool has_new_calibration_values = false;

	/* Convert data to fp. */
	data_int16_to_fp(sensor, data->data, fdata);

	calib_data = sensor->online_calib_data;
	switch (sensor->type) {
	case MOTIONSENSE_TYPE_ACCEL: {
		struct accel_cal *cal =
			(struct accel_cal *)(calib_data->type_specific_data);

		if (is_spoofed) {
			/* Copy the data to the calibration result. */
			cal->bias[X] = fdata[X];
			cal->bias[Y] = fdata[Y];
			cal->bias[Z] = fdata[Z];
			has_new_calibration_values = true;
		} else {
			/* Possibly update the gyroscope calibration. */
			update_gyro_cal(sensor, fdata, timestamp);

			/*
			 * Temperature is required for accelerometer
			 * calibration.
			 */
			rc = get_temperature(sensor, &temperature);
			if (rc != EC_SUCCESS)
				return rc;

			has_new_calibration_values = accel_cal_accumulate(
				cal, timestamp, fdata[X], fdata[Y], fdata[Z],
				temperature);
		}

		if (has_new_calibration_values) {
			mutex_lock(&g_calib_cache_mutex);
			/* Convert result to the right scale. */
			data_fp_to_int16(sensor, cal->bias, calib_data->cache);
			/* Set valid and dirty. */
			sensor_calib_cache_valid_map |= BIT(sensor_num);
			sensor_calib_cache_dirty_map |= BIT(sensor_num);
			mutex_unlock(&g_calib_cache_mutex);
			/* Notify the AP. */
			mkbp_send_event(EC_MKBP_EVENT_ONLINE_CALIBRATION);
		}
		break;
	}
	case MOTIONSENSE_TYPE_MAG: {
		struct mag_cal_t *cal =
			(struct mag_cal_t *)(calib_data->type_specific_data);
		int idata[] = {
			(int)data->data[X],
			(int)data->data[Y],
			(int)data->data[Z],
		};

		if (is_spoofed) {
			/* Copy the data to the calibration result. */
			cal->bias[X] = INT_TO_FP(idata[X]);
			cal->bias[Y] = INT_TO_FP(idata[Y]);
			cal->bias[Z] = INT_TO_FP(idata[Z]);
			has_new_calibration_values = true;
		} else {
			/* Possibly update the gyroscope calibration. */
			update_gyro_cal(sensor, fdata, timestamp);

			has_new_calibration_values = mag_cal_update(cal, idata);
		}

		if (has_new_calibration_values) {
			mutex_lock(&g_calib_cache_mutex);
			/* Copy the values */
			calib_data->cache[X] = cal->bias[X];
			calib_data->cache[Y] = cal->bias[Y];
			calib_data->cache[Z] = cal->bias[Z];
			/* Set valid and dirty. */
			sensor_calib_cache_valid_map |= BIT(sensor_num);
			sensor_calib_cache_dirty_map |= BIT(sensor_num);
			mutex_unlock(&g_calib_cache_mutex);
			/* Notify the AP. */
			mkbp_send_event(EC_MKBP_EVENT_ONLINE_CALIBRATION);
		}
		break;
	}
	case MOTIONSENSE_TYPE_GYRO: {
		if (is_spoofed) {
			/*
			 * Gyroscope uses fdata to store the calibration
			 * result, so there's no need to copy anything.
			 */
			has_new_calibration_values = true;
		} else {
			struct gyro_cal_data *gyro_cal_data =
				(struct gyro_cal_data *)
					calib_data->type_specific_data;
			struct gyro_cal *gyro_cal = &gyro_cal_data->gyro_cal;

			/* Temperature is required for gyro calibration. */
			rc = get_temperature(sensor, &temperature);
			if (rc != EC_SUCCESS)
				return rc;

			/* Update gyroscope calibration. */
			gyro_cal_update_gyro(gyro_cal, timestamp, fdata[X],
					     fdata[Y], fdata[Z], temperature);
			has_new_calibration_values =
				check_gyro_cal_new_bias(sensor, fdata);
		}

		if (has_new_calibration_values)
			set_gyro_cal_cache_values(sensor, fdata);
		break;
	}
	default:
		break;
	}

	return EC_SUCCESS;
}
