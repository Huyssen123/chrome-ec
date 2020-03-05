/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Driver for Intel Burnside Bridge - Thunderbolt/USB/DisplayPort Retimer
 */

#include "bb_retimer.h"
#include "common.h"
#include "console.h"
#include "i2c.h"
#include "timer.h"
#include "usb_pd.h"
#include "util.h"

#define BB_RETIMER_REG_SIZE	4
#define BB_RETIMER_READ_SIZE	(BB_RETIMER_REG_SIZE + 1)
#define BB_RETIMER_WRITE_SIZE	(BB_RETIMER_REG_SIZE + 2)
#define BB_RETIMER_MUX_DATA_PRESENT (USB_PD_MUX_USB_ENABLED \
				| USB_PD_MUX_DP_ENABLED \
				| USB_PD_MUX_SAFE_MODE \
				| USB_PD_MUX_TBT_COMPAT_ENABLED \
				| USB_PD_MUX_USB4_ENABLED)

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

/**
 * Utility functions
 */
static int bb_retimer_read(const struct usb_mux *me,
			   const uint8_t offset, uint32_t *data)
{
	int rv;
	uint8_t buf[BB_RETIMER_READ_SIZE];

	/*
	 * Read sequence
	 * Slave Addr(w) - Reg offset - repeated start - Slave Addr(r)
	 * byte[0]   : Read size
	 * byte[1:4] : Data [LSB -> MSB]
	 * Stop
	 */
	rv = i2c_xfer(me->i2c_port, me->i2c_addr_flags,
		      &offset, 1, buf, BB_RETIMER_READ_SIZE);
	if (rv)
		return rv;
	if (buf[0] != BB_RETIMER_REG_SIZE)
		return EC_ERROR_UNKNOWN;

	*data = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);

	return EC_SUCCESS;
}

static int bb_retimer_write(const struct usb_mux *me,
			    const uint8_t offset, uint32_t data)
{
	uint8_t buf[BB_RETIMER_WRITE_SIZE];

	/*
	 * Write sequence
	 * Slave Addr(w)
	 * byte[0]   : Reg offset
	 * byte[1]   : Write Size
	 * byte[2:5] : Data [LSB -> MSB]
	 * stop
	 */
	buf[0] = offset;
	buf[1] = BB_RETIMER_REG_SIZE;
	buf[2] = data & 0xFF;
	buf[3] = (data >> 8) & 0xFF;
	buf[4] = (data >> 16) & 0xFF;
	buf[5] = (data >> 24) & 0xFF;

	return i2c_xfer(me->i2c_port,
			me->i2c_addr_flags,
			buf, BB_RETIMER_WRITE_SIZE, NULL, 0);
}

static void bb_retimer_power_handle(const struct usb_mux *me, int on_off)
{
	const struct bb_usb_control *control = &bb_controls[me->usb_port];

	/* handle retimer's power domain */

	if (on_off) {
		gpio_set_level(control->usb_ls_en_gpio, 1);
		msleep(1);
		gpio_set_level(control->retimer_rst_gpio, 1);
		msleep(10);
		gpio_set_level(control->force_power_gpio, 1);

		/*
		 * If BB retimer NVM is shared between multiple ports, allow
		 * 40ms time for all the retimers to be initialized.
		 * Else allow 20ms to initialize.
		 */
		if (control->shared_nvm)
			msleep(40);
		else
			msleep(20);
	} else {
		gpio_set_level(control->force_power_gpio, 0);
		msleep(1);
		gpio_set_level(control->retimer_rst_gpio, 0);
		msleep(1);
		gpio_set_level(control->usb_ls_en_gpio, 0);
	}
}

/**
 * Driver interface functions
 */
static int retimer_set_state(const struct usb_mux *me, mux_state_t mux_state)
{
	uint32_t set_retimer_con = 0;
	uint8_t dp_pin_mode;
	int port = me->usb_port;
	union tbt_mode_resp_cable cable_resp;
	union tbt_mode_resp_device dev_resp;

	/*
	 * Bit 0: DATA_CONNECTION_PRESENT
	 * 0 - No connection present
	 * 1 - Connection present
	 */
	if (mux_state & BB_RETIMER_MUX_DATA_PRESENT)
		set_retimer_con |= BB_RETIMER_DATA_CONNECTION_PRESENT;

	/*
	 * Bit 1: CONNECTION_ORIENTATION
	 * 0 - Normal
	 * 1 - reversed
	 */
	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		set_retimer_con |= BB_RETIMER_CONNECTION_ORIENTATION;

	/*
	 * Bit 5: USB_3_CONNECTION
	 * 0 - No USB3.1 Connection
	 * 1 - USB3.1 connection
	 */
	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		set_retimer_con |= BB_RETIMER_USB_3_CONNECTION;

		/*
		 * Bit 7: USB_DATA_ROLE (ignored if BIT5=0)
		 * 0 - DFP
		 * 1 - UPF
		 */
		if (pd_partner_is_ufp(port))
			set_retimer_con |= BB_RETIMER_USB_DATA_ROLE;
	}

	/*
	 * Bit 8: DP_CONNECTION
	 * 0 – No DP connection
	 * 1 – DP connected
	 */
	if (mux_state & USB_PD_MUX_DP_ENABLED) {
		set_retimer_con |= BB_RETIMER_DP_CONNECTION;

		/*
		 * Bit 11-10: DP_PIN_ASSIGNMENT (ignored if BIT8 = 0)
		 * 00 – Pin assignments E/E’
		 * 01 – Pin assignments C/C’/D/D’1,2
		 * 10, 11 - reserved
		 */
		dp_pin_mode = get_dp_pin_mode(port);
		if (dp_pin_mode == MODE_DP_PIN_C ||
			dp_pin_mode == MODE_DP_PIN_D)
			set_retimer_con |= BB_RETIMER_DP_PIN_ASSIGNMENT;

		/*
		 * Bit 14: IRQ_HPD (ignored if BIT8 = 0)
		 * 0 - No IRQ_HPD
		 * 1 - IRQ_HPD received
		 */
		if (mux_state & USB_PD_MUX_HPD_IRQ)
			set_retimer_con |= BB_RETIMER_IRQ_HPD;

		/*
		 * Bit 15: HPD_LVL (ignored if BIT8 = 0)
		 * 0 - HPD_State Low
		 * 1 - HPD_State High
		 */
		if (mux_state & USB_PD_MUX_HPD_LVL)
			set_retimer_con |= BB_RETIMER_HPD_LVL;
	}

	/*
	 * Bit 12: DEBUG_ACCESSORY_MODE
	 * 0 - Not in debug mode
	 * 1 - In debug accessory mode
	 */
	if (pd_is_debug_acc(port))
		set_retimer_con |= BB_RETIMER_DEBUG_ACCESSORY_MODE;

	if (mux_state & (USB_PD_MUX_TBT_COMPAT_ENABLED |
			 USB_PD_MUX_USB4_ENABLED)) {
		cable_resp = get_cable_tbt_vdo(port);
		dev_resp = get_dev_tbt_vdo(port);

		/*
		 * Bit 2: RE_TIMER_DRIVER
		 * 0 - Re-driver
		 * 1 - Re-timer
		 */
		if (cable_resp.retimer_type == USB_RETIMER)
			set_retimer_con |= BB_RETIMER_RE_TIMER_DRIVER;

		/*
		 * Bit 16: TBT_CONNECTION
		 * 0 - TBT not configured
		 * 1 - TBT configured
		 */
		if (mux_state & USB_PD_MUX_TBT_COMPAT_ENABLED) {
			set_retimer_con |= BB_RETIMER_TBT_CONNECTION;

			/*
			 * Bit 17: TBT_TYPE
			 * 0 - Type-C to Type-C Cable
			 * 1 - Type-C Legacy TBT Adapter
			 */
			if (dev_resp.tbt_adapter == TBT_ADAPTER_TBT2_LEGACY)
				set_retimer_con |= BB_RETIMER_TBT_TYPE;

			/*
			 * Bits 29-28: TBT_GEN_SUPPORT
			 * 00b - 3rd generation TBT (10.3125 and 20.625Gb/s)
			 * 01b - 4th generation TBT (10.00005Gb/s, 10.3125Gb/s,
			 *                           20.0625Gb/s, 20.000Gb/s)
			 * 10..11b - Reserved
			 */
			set_retimer_con |= BB_RETIMER_TBT_CABLE_GENERATION(
					       cable_resp.tbt_rounded);
		}

		/*
		 * Bit 18: CABLE_TYPE
		 * 0 - Electrical cable
		 * 1 - Optical cable
		 */
		if (cable_resp.tbt_cable == TBT_CABLE_OPTICAL)
			set_retimer_con |= BB_RETIMER_TBT_CABLE_TYPE;

		/*
		 * Bit 22: Active/Passive
		 * 0 - Passive cable
		 * 1 - Active cable
		 */
		if (get_usb_pd_cable_type(port) == IDH_PTYPE_ACABLE) {
			set_retimer_con |= BB_RETIMER_ACTIVE_PASSIVE;
			/*
			 * Bit 20: TBT_ACTIVE_LINK_TRAINING
			 * 0 - Active with bi-directional LSRX communication
			 * 1 - Active with uni-directional LSRX communication
			 * Set to "0" when passive cable plug
			 */
			if (cable_resp.lsrx_comm == UNIDIR_LSRX_COMM)
				set_retimer_con |=
					BB_RETIMER_TBT_ACTIVE_LINK_TRAINING;
		}

		/*
		 * Bit 23: USB4 Connection
		 * 0 - USB4 not configured
		 * 1 - USB4 Configured
		 */
		if (mux_state & USB_PD_MUX_USB4_ENABLED)
			set_retimer_con |= BB_RETIMER_USB4_ENABLED;

		/*
		 * Bit 27-25: TBT/USB4 Cable speed
		 * 000b - No functionality
		 * 001b - USB3.1 Gen1 Cable
		 * 010b - 10Gb/s
		 * 011b - 10Gb/s and 20Gb/s
		 * 10..11b - Reserved
		 */
		set_retimer_con |= BB_RETIMER_TBT_CABLE_SPEED_SUPPORT(
						cable_resp.tbt_cable_speed);
	}
	/* Writing the register4 */
	return bb_retimer_write(me, BB_RETIMER_REG_CONNECTION_STATE,
			set_retimer_con);
}

static int retimer_low_power_mode(const struct usb_mux *me)
{
	bb_retimer_power_handle(me, 0);
	return EC_SUCCESS;
}

static int retimer_init(const struct usb_mux *me)
{
	int rv;
	uint32_t data;

	bb_retimer_power_handle(me, 1);

	rv = bb_retimer_read(me, BB_RETIMER_REG_VENDOR_ID, &data);
	if (rv)
		return rv;
	if (data != BB_RETIMER_VENDOR_ID)
		return EC_ERROR_UNKNOWN;

	rv = bb_retimer_read(me, BB_RETIMER_REG_DEVICE_ID, &data);
	if (rv)
		return rv;

	if (data != BB_RETIMER_DEVICE_ID)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

const struct usb_mux_driver bb_usb_retimer = {
	.init = retimer_init,
	.set = retimer_set_state,
	.enter_low_power_mode = retimer_low_power_mode,
};

#ifdef CONFIG_CMD_RETIMER
static int console_command_bb_retimer(int argc, char **argv)
{
	char rw, *e;
	int rv, port, reg, data, val;
	const struct usb_mux *mux;

	if (argc < 4)
		return EC_ERROR_PARAM_COUNT;

	/* Get port number */
	port = strtoi(argv[1], &e, 0);
	if (*e || port < 0 || port > board_get_usb_pd_port_count())
		return EC_ERROR_PARAM1;

	mux = &usb_muxes[port];
	while (mux) {
		if (mux->driver == &bb_usb_retimer)
			break;
		mux = mux->next_mux;
	}

	if (!mux)
		return EC_ERROR_PARAM1;

	/* Validate r/w selection */
	rw = argv[2][0];
	if (rw != 'w' && rw != 'r')
		return EC_ERROR_PARAM2;

	/* Get register address */
	reg = strtoi(argv[3], &e, 0);
	if (*e || reg < 0)
		return EC_ERROR_PARAM3;

	if (rw == 'r')
		rv = bb_retimer_read(mux, reg, &data);
	else {
		/* Get value to be written */
		val = strtoi(argv[4], &e, 0);
		if (*e || val < 0)
			return EC_ERROR_PARAM4;

		rv = bb_retimer_write(mux, reg, val);
		if (rv == EC_SUCCESS) {
			rv = bb_retimer_read(mux, reg, &data);
			if (rv == EC_SUCCESS && data != val)
				rv = EC_ERROR_UNKNOWN;
		}
	}

	if (rv == EC_SUCCESS)
		CPRINTS("register 0x%x [%d] = 0x%x [%d]", reg, reg, data, data);

	return rv;
}
DECLARE_CONSOLE_COMMAND(bb, console_command_bb_retimer,
			"<port> <r/w> <reg> | <val>",
			"Read or write to BB retimer register");
#endif /* CONFIG_CMD_RETIMER */
