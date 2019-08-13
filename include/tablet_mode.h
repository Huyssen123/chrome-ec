/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_TABLET_MODE_H
#define __CROS_EC_TABLET_MODE_H

/**
 * Get tablet mode state
 *
 * Return 1 if in tablet mode, 0 otherwise
 */
int tablet_get_mode(void);

/**
 * Set tablet mode state
 *
 * @param mode 1: tablet mode. 0 clamshell mode.
 */
void tablet_set_mode(int mode);

/**
 * Disable tablet mode
 */
void tablet_disable(void);

/**
 * Interrupt service routine for hall sensor.
 *
 * HALL_SENSOR_GPIO_L must be defined.
 *
 * @param signal: GPIO signal
 */
void hall_sensor_isr(enum gpio_signal signal);

/**
 * Disables the interrupt on GPIO connected to hall sensor. Additionally, it
 * disables the tablet mode switch sub-system and turns off tablet mode. This is
 * useful when the same firmware is shared between convertible and clamshell
 * devices to turn off hall sensor and tablet mode detection on clamshell.
 */
void hall_sensor_disable(void);

#endif
