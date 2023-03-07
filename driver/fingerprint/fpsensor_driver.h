/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_DRIVER_FINGERPRINT_FPSENSOR_DRIVER_H_
#define __CROS_EC_DRIVER_FINGERPRINT_FPSENSOR_DRIVER_H_

#if defined(HAVE_PRIVATE) && !defined(EMU_BUILD)
#define HAVE_FP_PRIVATE_DRIVER
#if defined(CONFIG_FP_SENSOR_ELAN80) || defined(CONFIG_FP_SENSOR_ELAN515)
#include "elan/elan_sensor.h"
#define FP_SENSOR_IMAGE_SIZE (FP_SENSOR_IMAGE_SIZE_ELAN)
#define FP_SENSOR_RES_X (FP_SENSOR_RES_X_ELAN)
#define FP_SENSOR_RES_Y (FP_SENSOR_RES_Y_ELAN)
#define FP_ALGORITHM_TEMPLATE_SIZE (FP_ALGORITHM_TEMPLATE_SIZE_ELAN)
#define FP_MAX_FINGER_COUNT (FP_MAX_FINGER_COUNT_ELAN)
#else
#include "fpc/fpc_sensor.h"
#define FP_SENSOR_IMAGE_SIZE (FP_SENSOR_IMAGE_SIZE_FPC)
#define FP_SENSOR_RES_X (FP_SENSOR_RES_X_FPC)
#define FP_SENSOR_RES_Y (FP_SENSOR_RES_Y_FPC)
#define FP_ALGORITHM_TEMPLATE_SIZE (FP_ALGORITHM_TEMPLATE_SIZE_FPC)
#define FP_MAX_FINGER_COUNT (FP_MAX_FINGER_COUNT_FPC)
#endif
#else
/* These values are used by the host (emulator) tests. */
#define FP_SENSOR_IMAGE_SIZE 0
#define FP_SENSOR_RES_X 0
#define FP_SENSOR_RES_Y 0
#define FP_ALGORITHM_TEMPLATE_SIZE 0
#define FP_MAX_FINGER_COUNT 5
#endif

#if defined(HAVE_PRIVATE) && defined(TEST_BUILD)
/*
 * For unittest in a private build, enable driver-related code in
 * common/fpsensor/ so that they can be tested (with fp_sensor_mock).
 */
#define HAVE_FP_PRIVATE_DRIVER
#endif

#endif /* __CROS_EC_DRIVER_FINGERPRINT_FPSENSOR_DRIVER_H_ */
