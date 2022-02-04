/*
 * Copyright 2020 Google LLC.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef DT_BINDINGS_GPIO_DEFINES_H_
#define DT_BINDINGS_GPIO_DEFINES_H_

#include <dt-bindings/gpio/gpio.h>

/*
 * The GPIO_INPUT and GPIO_OUTPUT defines are normally not available to
 * the device tree. For GPIOs that are controlled by the platform/ec module, we
 * allow device tree to set the initial state.
 *
 * Note the raw defines (e.g. GPIO_OUTPUT) in this file are copies from
 * <drivers/gpio.h>
 *
 * The combined defined (e.g. GPIO_OUT_LOW) have been renamed to fit with
 * gpio defined in platform/ec codebase.
 */

/** Enables pin as input. */
#define GPIO_INPUT              (1U << 8)

/** Enables pin as output, no change to the output state. */
#define GPIO_OUTPUT             (1U << 9)

/* Initializes output to a low state. */
#define GPIO_OUTPUT_INIT_LOW    (1U << 10)

/* Initializes output to a high state. */
#define GPIO_OUTPUT_INIT_HIGH   (1U << 11)

/* Configures GPIO pin as output and initializes it to a low state. */
#define GPIO_OUTPUT_LOW         (GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW)

/* Configures GPIO pin as output and initializes it to a high state. */
#define GPIO_OUTPUT_HIGH        (GPIO_OUTPUT | GPIO_OUTPUT_INIT_HIGH)

/* Configures GPIO pin as input with pull-up. */
#define GPIO_INPUT_PULL_UP      (GPIO_INPUT | GPIO_PULL_UP)

/* Configures GPIO pin as input with pull-down. */
#define GPIO_INPUT_PULL_DOWN    (GPIO_INPUT | GPIO_PULL_DOWN)

/** Configures GPIO pin as output and initializes it to a low state. */
#define GPIO_OUT_LOW         (GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW)

/** Configures GPIO pin as output and initializes it to a high state. */
#define GPIO_OUT_HIGH        (GPIO_OUTPUT | GPIO_OUTPUT_INIT_HIGH)

/** Configures GPIO pin as ODR output and initializes it to a low state. */
#define GPIO_ODR_LOW (GPIO_OUT_LOW | GPIO_OPEN_DRAIN)

/** Configures GPIO pin as ODR output and initializes it to a high state. */
#define GPIO_ODR_HIGH (GPIO_OUT_HIGH | GPIO_OPEN_DRAIN)

/*
 * GPIO interrupt flags, taken from <drivers/gpio.h>
 *
 * TODO: Remove these once upstream changes to make these flags
 * available lands.
 */

/** Disables GPIO pin interrupt. */
#define GPIO_INT_DISABLE               (1U << 13)

/* Enables GPIO pin interrupt. */
#define GPIO_INT_ENABLE                (1U << 14)

/* GPIO interrupt is sensitive to logical levels.
 *
 * This is a component flag that should be combined with other
 * `GPIO_INT_*` flags to produce a meaningful configuration.
 */
#define GPIO_INT_LEVELS_LOGICAL        (1U << 15)

/* GPIO interrupt is edge sensitive.
 *
 * Note: by default interrupts are level sensitive.
 *
 * This is a component flag that should be combined with other
 * `GPIO_INT_*` flags to produce a meaningful configuration.
 */
#define GPIO_INT_EDGE                  (1U << 16)

/* Trigger detection when input state is (or transitions to) physical low or
 * logical 0 level.
 *
 * This is a component flag that should be combined with other
 * `GPIO_INT_*` flags to produce a meaningful configuration.
 */
#define GPIO_INT_LOW_0                 (1U << 17)

/* Trigger detection on input state is (or transitions to) physical high or
 * logical 1 level.
 *
 * This is a component flag that should be combined with other
 * `GPIO_INT_*` flags to produce a meaningful configuration.
 */
#define GPIO_INT_HIGH_1                (1U << 18)

/** Configures GPIO interrupt to be triggered on pin rising edge and enables it.
 */
#define GPIO_INT_EDGE_RISING           (GPIO_INT_ENABLE | \
					GPIO_INT_EDGE | \
					GPIO_INT_HIGH_1)

/** Configures GPIO interrupt to be triggered on pin falling edge and enables
 * it.
 */
#define GPIO_INT_EDGE_FALLING          (GPIO_INT_ENABLE | \
					GPIO_INT_EDGE | \
					GPIO_INT_LOW_0)

/** Configures GPIO interrupt to be triggered on pin rising or falling edge and
 * enables it.
 */
#define GPIO_INT_EDGE_BOTH             (GPIO_INT_ENABLE | \
					GPIO_INT_EDGE | \
					GPIO_INT_LOW_0 | \
					GPIO_INT_HIGH_1)

/** Configures GPIO interrupt to be triggered on pin physical level low and
 * enables it.
 */
#define GPIO_INT_LEVEL_LOW             (GPIO_INT_ENABLE | \
					GPIO_INT_LOW_0)

/** Configures GPIO interrupt to be triggered on pin physical level high and
 * enables it.
 */
#define GPIO_INT_LEVEL_HIGH            (GPIO_INT_ENABLE | \
					GPIO_INT_HIGH_1)

/** Configures GPIO interrupt to be triggered on pin state change to logical
 * level 0 and enables it.
 */
#define GPIO_INT_EDGE_TO_INACTIVE      (GPIO_INT_ENABLE | \
					GPIO_INT_LEVELS_LOGICAL | \
					GPIO_INT_EDGE | \
					GPIO_INT_LOW_0)

/** Configures GPIO interrupt to be triggered on pin state change to logical
 * level 1 and enables it.
 */
#define GPIO_INT_EDGE_TO_ACTIVE        (GPIO_INT_ENABLE | \
					GPIO_INT_LEVELS_LOGICAL | \
					GPIO_INT_EDGE | \
					GPIO_INT_HIGH_1)

/** Configures GPIO interrupt to be triggered on pin logical level 0 and enables
 * it.
 */
#define GPIO_INT_LEVEL_INACTIVE        (GPIO_INT_ENABLE | \
					GPIO_INT_LEVELS_LOGICAL | \
					GPIO_INT_LOW_0)

/** Configures GPIO interrupt to be triggered on pin logical level 1 and enables
 * it.
 */
#define GPIO_INT_LEVEL_ACTIVE          (GPIO_INT_ENABLE | \
					GPIO_INT_LEVELS_LOGICAL | \
					GPIO_INT_HIGH_1)

/** Enable GPIO pin debounce.
 *
 * @note Drivers that do not support a debounce feature should ignore
 * this flag rather than rejecting the configuration with -ENOTSUP.
 */
#define GPIO_INT_DEBOUNCE              (1U << 19)

#endif /* DT_BINDINGS_GPIO_DEFINES_H_ */
