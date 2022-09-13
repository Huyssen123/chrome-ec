/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * AP UART state machine
 */
#include "case_closed_debug.h"
#include "ccd_config.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "timer.h"
#include "uart_bitbang.h"
#include "uartn.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

static enum device_state state = DEVICE_STATE_INIT;

void print_ap_uart_state(void)
{
	ccprintf("AP UART: %s\n", device_state_name(state));
}

int ap_uart_is_on(void)
{
	/* Debouncing and on are both still on */
	return (state == DEVICE_STATE_DEBOUNCING || state == DEVICE_STATE_ON);
}

/**
 * Set the AP UART state.
 *
 * Done as a function to make it easier to debug state transitions.  Note that
 * this ONLY sets the state (and possibly prints debug info), and doesn't do
 * all the additional transition work that set_ap_uart_on(), etc. do.
 *
 * @param new_state	State to set.
 */
static void set_state(enum device_state new_state)
{
#ifdef CR50_DEBUG_AP_UART_STATE
	/* Print all state transitions.  May spam the console. */
	if (state != new_state)
		CPRINTS("AP UART %s -> %s",
			device_state_name(state), device_state_name(new_state));
#endif
	state = new_state;
}

/* Move the AP UART to the OFF state. */
static void set_ap_uart_off(void)
{
	CPRINTS("AP UART off");
	set_state(DEVICE_STATE_OFF);

	ccd_update_state();
}

/**
 * Move the AP UART to the ON state.
 *
 * This can be deferred from the interrupt handler, or called from the state
 * machine which also runs in HOOK task, so it needs to check the current state
 * to determine whether we're already on.
 */
static void set_ap_uart_on_deferred(void)
{
	/* If we were debouncing ON->OFF, cancel it because we're still on */
	if (state == DEVICE_STATE_DEBOUNCING)
		set_state(DEVICE_STATE_ON);

	/* If we're already on, done */
	if (state == DEVICE_STATE_ON)
		return;

	/* We were previously off */
	CPRINTS("AP UART on");
	set_state(DEVICE_STATE_ON);

	ccd_update_state();
}
DECLARE_DEFERRED(set_ap_uart_on_deferred);

/**
 * Interrupt handler for AP detect asserted
 */
void ap_detect_asserted(enum gpio_signal signal)
{
	gpio_disable_interrupt(GPIO_DETECT_AP_UART);
	hook_call_deferred(&set_ap_uart_on_deferred_data, 0);
}

/**
 * Detect state machine
 */
static void ap_uart_poll(void)
{
	/* Disable interrupts if we had them on for debouncing */
	gpio_disable_interrupt(GPIO_DETECT_AP_UART);

	/* If the AP UART signal is high, make sure it's on */
	if (gpio_get_level(GPIO_DETECT_AP_UART)) {
		hook_call_deferred(&set_ap_uart_on_deferred_data, 0);
		return;
	}

	/*
	 * Make sure the interrupt is enabled. We will need to detect the on
	 * transition if we enter the off or debouncing state
	 */
	gpio_enable_interrupt(GPIO_DETECT_AP_UART);

	/* AP UART wasn't detected.  If we're already off, done. */
	if (state == DEVICE_STATE_OFF)
		return;

	/* If we were debouncing, we're now sure we're off */
	if (state == DEVICE_STATE_DEBOUNCING ||
	    state == DEVICE_STATE_INIT_DEBOUNCING) {
		set_ap_uart_off();
		return;
	}

	/*
	 * Otherwise, we were on or initializing, and we're not sure if the AP
	 * UART is actually off or just sending a 0-bit.  So start debouncing.
	 */
	if (state == DEVICE_STATE_INIT)
		set_state(DEVICE_STATE_INIT_DEBOUNCING);
	else
		set_state(DEVICE_STATE_DEBOUNCING);
}

static void ap_uart_detect(void);
DECLARE_DEFERRED(ap_uart_detect);

static void ap_uart_detect(void)
{
	ap_uart_poll();

	/*
	 * Some platforms require that the H1 reacts to the AP shutting down
	 * within 100 ms.
	 *
	 * It takes up to three polls to detect AP shutdown, hence let's limit
	 * poll interval to 33 ms when CCD is active.
	 *
	 * UART is shut off unconditionally when CCD is inactive, no need to
	 * poll too often in that case, as increased poll frequency causes
	 * increased power consumption.
	 */
	hook_call_deferred(&ap_uart_detect_data, ccd_ext_is_enabled() ?
			   33 * MSEC : SECOND);
}

DECLARE_HOOK(HOOK_INIT, ap_uart_detect, HOOK_PRIO_DEFAULT);
