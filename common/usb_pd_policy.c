/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "charge_manager.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "flash.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "mkbp_event.h"
#include "registers.h"
#include "rsa.h"
#include "sha256.h"
#include "system.h"
#include "task.h"
#include "tcpm.h"
#include "timer.h"
#include "util.h"
#include "usb_api.h"
#include "usb_common.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "version.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#else
#define CPRINTS(format, args...)
#define CPRINTF(format, args...)
#endif

/*
 * This file is currently only used for TCPMv1, and would need changes before
 * being used for TCPMv2. One example: PD_FLAGS_* are TCPMv1 only.
 */
#ifndef CONFIG_USB_PD_TCPMV1
#error This file must only be used with TCPMv1
#endif

static int rw_flash_changed = 1;

__overridable void pd_check_pr_role(int port,
	enum pd_power_role pr_role, int flags)
{
	/*
	 * If partner is dual-role power and dualrole toggling is on, consider
	 * if a power swap is necessary.
	 */
	if ((flags & PD_FLAGS_PARTNER_DR_POWER) &&
	    pd_get_dual_role(port) == PD_DRP_TOGGLE_ON) {
		/*
		 * If we are a sink and partner is not unconstrained, then
		 * swap to become a source. If we are source and partner is
		 * unconstrained, swap to become a sink.
		 */
		int partner_unconstrained = flags & PD_FLAGS_PARTNER_UNCONSTR;

		if ((!partner_unconstrained && pr_role == PD_ROLE_SINK) ||
		     (partner_unconstrained && pr_role == PD_ROLE_SOURCE))
			pd_request_power_swap(port);
	}
}

__overridable void pd_check_dr_role(int port,
	enum pd_data_role dr_role, int flags)
{
	/* If UFP, try to switch to DFP */
	if ((flags & PD_FLAGS_PARTNER_DR_DATA) && dr_role == PD_ROLE_UFP)
		pd_request_data_swap(port);
}

#ifdef CONFIG_MKBP_EVENT
static int dp_alt_mode_entry_get_next_event(uint8_t *data)
{
	return EC_SUCCESS;
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_DP_ALT_MODE_ENTERED,
		     dp_alt_mode_entry_get_next_event);
#endif /* CONFIG_MKBP_EVENT */

#ifdef CONFIG_USB_PD_DUAL_ROLE
/* Last received source cap */
static uint32_t pd_src_caps[CONFIG_USB_PD_PORT_MAX_COUNT][PDO_MAX_OBJECTS];
static uint8_t pd_src_cap_cnt[CONFIG_USB_PD_PORT_MAX_COUNT];

const uint32_t * const pd_get_src_caps(int port)
{
	return pd_src_caps[port];
}

void pd_set_src_caps(int port, int cnt, uint32_t *src_caps)
{
	int i;

	pd_src_cap_cnt[port] = cnt;

	for (i = 0; i < cnt; i++)
		pd_src_caps[port][i] = *src_caps++;
}

uint8_t pd_get_src_cap_cnt(int port)
{
	return pd_src_cap_cnt[port];
}
#endif /* CONFIG_USB_PD_DUAL_ROLE */

static struct pd_cable cable[CONFIG_USB_PD_PORT_MAX_COUNT];

bool is_transmit_msg_sop_prime(int port)
{
	return (IS_ENABLED(CONFIG_USB_PD_DECODE_SOP) &&
		(cable[port].flags & CABLE_FLAGS_SOP_PRIME_ENABLE));
}

bool is_transmit_msg_sop_prime_prime(int port)
{
	return (IS_ENABLED(CONFIG_USB_PD_DECODE_SOP) &&
		(cable[port].flags & CABLE_FLAGS_SOP_PRIME_PRIME_ENABLE));
}

bool consume_sop_prime_repeat_msg(int port, uint8_t msg_id)
{

	if (cable[port].last_sop_p_msg_id != msg_id) {
		cable[port].last_sop_p_msg_id = msg_id;
		return false;
	}
	CPRINTF("C%d SOP Prime repeat msg_id %d\n", port, msg_id);
	return true;
}

bool consume_sop_prime_prime_repeat_msg(int port, uint8_t msg_id)
{

	if (cable[port].last_sop_p_p_msg_id != msg_id) {
		cable[port].last_sop_p_p_msg_id = msg_id;
		return false;
	}
	CPRINTF("C%d SOP Prime Prime repeat msg_id %d\n", port, msg_id);
	return true;
}

__maybe_unused static void disable_transmit_sop_prime(int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_DECODE_SOP))
		cable[port].flags &= ~CABLE_FLAGS_SOP_PRIME_ENABLE;
}

__maybe_unused static void disable_transmit_sop_prime_prime(int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_DECODE_SOP))
		cable[port].flags &= ~CABLE_FLAGS_SOP_PRIME_PRIME_ENABLE;
}

__maybe_unused static uint8_t is_sop_prime_ready(int port)
{
	/*
	 * Ref: USB PD 3.0 sec 2.5.4: When an Explicit Contract is in place the
	 * VCONN Source (either the DFP or the UFP) can communicate with the
	 * Cable Plug(s) using SOP’/SOP’’ Packets
	 *
	 * Ref: USB PD 2.0 sec 2.4.4: When an Explicit Contract is in place the
	 * DFP (either the Source or the Sink) can communicate with the
	 * Cable Plug(s) using SOP’/SOP” Packets.
	 * Sec 3.6.11 : Before communicating with a Cable Plug a Port Should
	 * ensure that it is the Vconn Source
	 */
	return (pd_get_vconn_state(port) && (IS_ENABLED(CONFIG_USB_PD_REV30)
		|| (pd_get_data_role(port) == PD_ROLE_DFP)));
}

enum pd_msg_type pd_msg_tx_type(int port)
{
	if (is_transmit_msg_sop_prime(port))
		return PD_MSG_SOP_PRIME;
	if (is_transmit_msg_sop_prime_prime(port))
		return PD_MSG_SOP_PRIME_PRIME;

	return PD_MSG_SOP;
}

void reset_pd_cable(int port)
{
	memset(&cable[port], 0, sizeof(cable[port]));
	cable[port].last_sop_p_msg_id = INVALID_MSG_ID_COUNTER;
	cable[port].last_sop_p_p_msg_id = INVALID_MSG_ID_COUNTER;
}

union tbt_mode_resp_cable get_cable_tbt_vdo(int port)
{
	/*
	 * Return Discover mode SOP prime response for Thunderbolt-compatible
	 * mode SVDO.
	 */
	return cable[port].cable_mode_resp;
}

union tbt_mode_resp_device get_dev_tbt_vdo(int port)
{
	/*
	 * Return Discover mode SOP response for Thunderbolt-compatible
	 * mode SVDO.
	 */
	return cable[port].dev_mode_resp;
}

enum tbt_compat_cable_speed get_tbt_cable_speed(int port)
{
	/* tbt_cable_speed is zero when uninitialized */
	return cable[port].cable_mode_resp.tbt_cable_speed;
}

enum tbt_compat_rounded_support get_tbt_rounded_support(int port)
{
	/* tbt_rounded_support is zero when uninitialized */
	return cable[port].cable_mode_resp.tbt_rounded;
}

uint32_t get_enter_usb_msg_payload(int port)
{
	/*
	 * Ref: USB Power Delivery Specification Revision 3.0, Version 2.0
	 * Table 6-47 Enter_USB Data Object
	 */
	union enter_usb_data_obj eudo;

	if (!IS_ENABLED(CONFIG_USB_PD_USB4))
		return 0;

	eudo.mode = USB_PD_40;
	eudo.usb4_drd_cap = IS_ENABLED(CONFIG_USB_PD_USB4);
	eudo.usb3_drd_cap = IS_ENABLED(CONFIG_USB_PD_USB32);
	eudo.cable_speed = get_usb4_cable_speed(port);

	if ((cable[port].rev == PD_REV30) &&
	    (get_usb_pd_cable_type(port) == IDH_PTYPE_ACABLE)) {
		eudo.cable_type = (cable[port].attr2.a2_rev30.active_elem ==
			ACTIVE_RETIMER) ? CABLE_TYPE_ACTIVE_RETIMER :
			CABLE_TYPE_ACTIVE_REDRIVER;
	/* TODO: Add eudo.cable_type for Revisiosn 2 active cables */
	} else {
		eudo.cable_type = CABLE_TYPE_PASSIVE;
	}

	switch (cable[port].attr.p_rev20.vbus_cur) {
	case USB_VBUS_CUR_3A:
		eudo.cable_current = USB4_CABLE_CURRENT_3A;
		break;
	case USB_VBUS_CUR_5A:
		eudo.cable_current = USB4_CABLE_CURRENT_5A;
		break;
	default:
		eudo.cable_current = USB4_CABLE_CURRENT_INVALID;
		break;
	}
	eudo.pcie_supported = IS_ENABLED(CONFIG_USB_PD_PCIE_TUNNELING);
	eudo.dp_supported = IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP);
	eudo.tbt_supported = IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE);
	eudo.host_present = 1;

	return eudo.raw_value;
}

bool should_enter_usb4_mode(int port)
{
	return IS_ENABLED(CONFIG_USB_PD_USB4) &&
		cable[port].flags & CABLE_FLAGS_ENTER_USB_MODE;
}

void enable_enter_usb4_mode(int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_USB4))
		cable[port].flags |= CABLE_FLAGS_ENTER_USB_MODE;
}

void disable_enter_usb4_mode(int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_USB4))
		cable[port].flags &= ~CABLE_FLAGS_ENTER_USB_MODE;
}

#ifdef CONFIG_USB_PD_ALT_MODE

#ifdef CONFIG_USB_PD_ALT_MODE_DFP

static struct pd_discovery discovery[CONFIG_USB_PD_PORT_MAX_COUNT];

static void enable_transmit_sop_prime(int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_DECODE_SOP))
		cable[port].flags |= CABLE_FLAGS_SOP_PRIME_ENABLE;
}

static void enable_transmit_sop_prime_prime(int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_DECODE_SOP))
		cable[port].flags |= CABLE_FLAGS_SOP_PRIME_PRIME_ENABLE;
}

static bool is_tbt_compat_enabled(int port)
{
	return (IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE) &&
	       (cable[port].flags & CABLE_FLAGS_TBT_COMPAT_ENABLE));
}

static void enable_tbt_compat_mode(int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE))
		cable[port].flags |= CABLE_FLAGS_TBT_COMPAT_ENABLE;
}

static inline void disable_tbt_compat_mode(int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE))
		cable[port].flags &= ~CABLE_FLAGS_TBT_COMPAT_ENABLE;
}

static inline void limit_tbt_cable_speed(int port)
{
	/* Cable flags are cleared when cable reset is called */
	cable[port].flags |= CABLE_FLAGS_TBT_COMPAT_LIMIT_SPEED;
}

static inline bool is_limit_tbt_cable_speed(int port)
{
	return !!(cable[port].flags & CABLE_FLAGS_TBT_COMPAT_LIMIT_SPEED);
}

static inline bool is_usb4_mode_enabled(int port)
{
	return (IS_ENABLED(CONFIG_USB_PD_USB4) &&
	       (cable[port].flags & CABLE_FLAGS_USB4_CAPABLE));
}

static inline void enable_usb4_mode(int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_USB4))
		cable[port].flags |= CABLE_FLAGS_USB4_CAPABLE;
}

static inline void disable_usb4_mode(int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_USB4))
		cable[port].flags &= ~CABLE_FLAGS_USB4_CAPABLE;
}

/*
 * For Cable rev 3.0: USB4 cable speed is set according to speed supported by
 * the port and the response received from the cable, whichever is least.
 *
 * For Cable rev 2.0: Since board_is_tbt_usb4_port() should not enabled if the
 * port supports speed less than USB_R20_SS_U31_GEN1_GEN2, USB4 cable speed is
 * set according to the cable response.
 */
static void set_max_usb4_cable_speed(int port)
{
	/*
	 * Converting Thunderbolt-Compatible board speed to equivalent USB4
	 * speed.
	 */
	enum usb_rev30_ss max_usb4_speed =
			board_get_max_tbt_speed(port) == TBT_SS_TBT_GEN3 ?
			USB_R30_SS_U40_GEN3 : USB_R30_SS_U32_U40_GEN2;

	if (max_usb4_speed < cable[port].attr.p_rev30.ss)
		cable[port].attr.p_rev30.ss = max_usb4_speed;
}

/*
 * Ref: USB Type-C Cable and Connector Specification
 * Figure 5-1 USB4 Discovery and Entry Flow Model.
 *
 * Note: USB Type-C Cable and Connector Specification
 * doesn't include details for Revision 2 cables.
 *
 *                         Passive Cable
 *                                |
 *                -----------------------------------
 *                |                                 |
 *           Revision 2                        Revision 3
 *          USB Signalling                   USB Signalling
 *             |                                     |
 *     ------------------            -------------------------
 *     |       |        |            |       |       |       |
 * USB2.0   USB3.1    USB3.1       USB3.2   USB4   USB3.2   USB2
 *   |      Gen1      Gen1 Gen2    Gen2     Gen3   Gen1       |
 *   |       |          |           |        |       |       Exit
 *   --------           ------------         --------        USB4
 *      |                    |                  |          Discovery.
 *    Exit          Is DFP Gen3 Capable?     Enter USB4
 *    USB4                  |                with respective
 *   Discovery.   --- No ---|--- Yes ---     cable speed.
 *                |                    |
 *    Enter USB4 with             Is Cable TBT3
 *    respective cable                 |
 *    speed.                 --- No ---|--- Yes ---
 *                           |                    |
 *                   Enter USB4 with        Enter USB4 with
 *                   TBT Gen2 passive       TBT Gen3 passive
 *                   cable.                 cable.
 *
 */
static bool is_cable_ready_to_enter_usb4(int port, int cnt)
{
	/* TODO: USB4 enter mode for Active cables */

	if (IS_ENABLED(CONFIG_USB_PD_USB4) &&
	   (get_usb_pd_cable_type(port) == IDH_PTYPE_PCABLE) &&
	    is_vdo_present(cnt, VDO_INDEX_PTYPE_CABLE1)) {
		switch (cable[port].rev) {
		case PD_REV30:
			set_max_usb4_cable_speed(port);
			switch (cable[port].attr.p_rev30.ss) {
			case USB_R30_SS_U40_GEN3:
			case USB_R30_SS_U32_U40_GEN1:
				return true;
			case USB_R30_SS_U32_U40_GEN2:
				/* Check if DFP is Gen 3 capable */
				if (IS_ENABLED(CONFIG_USB_PD_TBT_GEN3_CAPABLE))
					return false;
				return true;
			default:
				disable_usb4_mode(port);
				return false;
			}
		case PD_REV20:
			switch (cable[port].attr.p_rev20.ss) {
			case USB_R20_SS_U31_GEN1_GEN2:
				/* Check if DFP is Gen 3 capable */
				if (IS_ENABLED(CONFIG_USB_PD_TBT_GEN3_CAPABLE))
					return false;
				return true;
			default:
				disable_usb4_mode(port);
				return false;
		}
		default:
			disable_usb4_mode(port);
		}
	}
	return false;
}

static bool is_usb4_vdo(int port, int cnt, uint32_t *payload)
{
	enum idh_ptype ptype = PD_IDH_PTYPE(payload[VDO_I(IDH)]);

	if (IS_PD_IDH_UFP_PTYPE(ptype)) {
		/*
		 * Ref: USB Type-C Cable and Connector Specification
		 * Figure 5-1 USB4 Discovery and Entry Flow Model
		 * Device USB4 VDO detection.
		 */
		return IS_ENABLED(CONFIG_USB_PD_USB4) &&
			is_vdo_present(cnt, VDO_INDEX_PTYPE_UFP1_VDO) &&
			PD_PRODUCT_IS_USB4(payload[VDO_INDEX_PTYPE_UFP1_VDO]);
	}
	return false;
}

void pd_dfp_discovery_init(int port)
{
	memset(&discovery[port], 0, sizeof(struct pd_discovery));
}

static int dfp_discover_ident(uint32_t *payload)
{
	payload[0] = VDO(USB_SID_PD, 1, CMD_DISCOVER_IDENT);
	return 1;
}

static int dfp_discover_svids(uint32_t *payload)
{
	payload[0] = VDO(USB_SID_PD, 1, CMD_DISCOVER_SVID);
	return 1;
}

struct pd_discovery *pd_get_am_discovery(int port, enum tcpm_transmit_type type)
{
	/*
	 * TCPMv2 separates discovered data by partner (SOP vs. SOP'); TCPMv1
	 * depends on both types being in the same structure.
	 */
	return &discovery[port];
}

/* Note: Enter mode flag is not needed by TCPMv1 */
void pd_set_dfp_enter_mode_flag(int port, bool set)
{
}

struct pd_cable *pd_get_cable_attributes(int port)
{
	return &cable[port];
}

__overridable bool board_is_tbt_usb4_port(int port)
{
	return true;
}

static enum tcpm_transmit_type get_tcpm_transmit_msg_type(int port)
{
	if (is_transmit_msg_sop_prime(port))
		return TCPC_TX_SOP_PRIME;

	if (is_transmit_msg_sop_prime_prime(port))
		return TCPC_TX_SOP_PRIME_PRIME;

	return TCPC_TX_SOP;
}

static void usb_pd_limit_cable_speed(int port)
{
	enum tbt_compat_cable_speed max_tbt_speed =
				board_get_max_tbt_speed(port);

	/* Cable does not have Intel SVID for Discover SVID */
	if (is_limit_tbt_cable_speed(port))
		cable[port].cable_mode_resp.tbt_cable_speed =
				TBT_SS_U32_GEN1_GEN2;

	if (cable[port].cable_mode_resp.tbt_cable_speed > max_tbt_speed)
		cable[port].cable_mode_resp.tbt_cable_speed = max_tbt_speed;
}

static int process_am_discover_svids(int port, int cnt, uint32_t *payload)
{
	int prev_svid_cnt = discovery[port].svid_cnt;

	/*
	 * The pd_discovery structure stores SOP and SOP' discovery results
	 * separately, but TCPMv1 depends on one-dimensional storage of SVIDs
	 * and modes. Therefore, always use TCPC_TX_SOP in TCPMv1.
	 */
	dfp_consume_svids(port, TCPC_TX_SOP, cnt, payload);

	/*
	 * Ref: USB Type-C Cable and Connector Specification,
	 * figure F-1: TBT3 Discovery Flow
	 *
	 * For USB4 mode if device or cable doesn't have Intel SVID,
	 * disable Thunderbolt-Compatible mode directly enter USB4 mode.
	 *
	 * For Thunderbolt-compatible, check if 0x8087 is received for
	 * Discover SVID SOP. If not, disable Thunderbolt-compatible mode
	 *
	 * If 0x8087 is not received for Discover SVID SOP' limit to TBT
	 * passive Gen 2 cable.
	 */
	if (is_tbt_compat_enabled(port)) {
		bool intel_svid = is_intel_svid(port, prev_svid_cnt);

		if (!intel_svid) {
			if (is_usb4_mode_enabled(port)) {
				disable_tbt_compat_mode(port);
				limit_tbt_cable_speed(port);
				usb_pd_limit_cable_speed(port);
				enable_enter_usb4_mode(port);
				usb_mux_set_safe_mode(port);
				if (is_transmit_msg_sop_prime(port))
					disable_transmit_sop_prime(port);
				return 0;
			}

			if (is_transmit_msg_sop_prime(port))
				limit_tbt_cable_speed(port);
			else
				disable_tbt_compat_mode(port);
		} else if (!is_transmit_msg_sop_prime(port)) {
			enable_transmit_sop_prime(port);
			return dfp_discover_svids(payload);
		}

		disable_transmit_sop_prime(port);
	}

	return dfp_discover_modes(port, payload);
}

static int process_tbt_compat_discover_modes(int port,
				enum tcpm_transmit_type sop, uint32_t *payload)
{
	int rsize;

	/*
	 * For active cables, Enter mode: SOP', SOP'', SOP
	 * Ref: USB Type-C Cable and Connector Specification, figure F-1: TBT3
	 * Discovery Flow and Section F.2.7 TBT3 Cable Enter Mode Command.
	 */
	if (is_transmit_msg_sop_prime(port)) {
		/* Store Discover Mode SOP' response */
		cable[port].cable_mode_resp.raw_value = payload[1];

		/* Limits cable speed if applicable */
		usb_pd_limit_cable_speed(port);

		/*
		 * Enter Mode SOP' (Cable Enter Mode) and Enter USB SOP' is
		 * skipped for passive cables.
		 */
		if (get_usb_pd_cable_type(port) == IDH_PTYPE_PCABLE)
			disable_transmit_sop_prime(port);

		if (is_usb4_mode_enabled(port)) {
			/*
			 * If Cable is not Thunderbolt Gen 3
			 * capable or Thunderbolt Gen1_Gen2
			 * capable, disable USB4 mode and
			 * continue flow for
			 * Thunderbolt-compatible mode
			 */
			if (cable_supports_tbt_speed(port)) {
				enable_enter_usb4_mode(port);
				usb_mux_set_safe_mode(port);
				return 0;
			}
			disable_usb4_mode(port);
		}
		rsize = enter_tbt_compat_mode(port, sop, payload);
	} else {
		/* Store Discover Mode SOP response */
		cable[port].dev_mode_resp.raw_value = payload[1];

		if (is_limit_tbt_cable_speed(port)) {
			/*
			 * Passive cable has Nacked for Discover SVID.
			 * No need to do Discover modes of cable. Assign the
			 * cable discovery attributes and enter into device
			 * Thunderbolt-compatible mode.
			 */
			cable[port].cable_mode_resp.tbt_cable_speed =
				(cable[port].rev == PD_REV30 &&
				cable[port].attr.p_rev30.ss >
					USB_R30_SS_U32_U40_GEN2) ?
				TBT_SS_U32_GEN1_GEN2 :
				cable[port].attr.p_rev30.ss;

			rsize = enter_tbt_compat_mode(port, sop, payload);
		} else {
			/* Discover modes for SOP' */
			discovery[port].svid_idx--;
			rsize = dfp_discover_modes(port, payload);
			enable_transmit_sop_prime(port);
		}
	}

	return rsize;
}

static int obj_cnt_enter_tbt_compat_mode(int port, enum tcpm_transmit_type sop,
			uint32_t *payload)
{
	/* Enter mode SOP' for active cables */
	if (is_transmit_msg_sop_prime(port)) {
		disable_transmit_sop_prime(port);
		/* Check if the cable has a SOP'' controller */
		if (cable[port].attr.a_rev20.sop_p_p)
			enable_transmit_sop_prime_prime(port);
		return enter_tbt_compat_mode(port, sop, payload);
	}

	/* Enter Mode SOP'' for active cables with SOP'' controller */
	if (is_transmit_msg_sop_prime_prime(port)) {
		disable_transmit_sop_prime_prime(port);
		return enter_tbt_compat_mode(port, sop, payload);
	}

	/* Update Mux state to Thunderbolt-compatible mode. */
	set_tbt_compat_mode_ready(port);
	/* No response once device (and cable) acks */
	return 0;
}
#endif /* CONFIG_USB_PD_ALT_MODE_DFP */

int pd_svdm(int port, int cnt, uint32_t *payload, uint32_t **rpayload,
		uint16_t head)
{
	int cmd = PD_VDO_CMD(payload[0]);
	int cmd_type = PD_VDO_CMDT(payload[0]);
	int (*func)(int port, uint32_t *payload) = NULL;

	int rsize = 1; /* VDM header at a minimum */

	payload[0] &= ~VDO_CMDT_MASK;
	*rpayload = payload;

	if (cmd_type == CMDT_INIT) {
		switch (cmd) {
		case CMD_DISCOVER_IDENT:
			func = svdm_rsp.identity;
			break;
		case CMD_DISCOVER_SVID:
			func = svdm_rsp.svids;
			break;
		case CMD_DISCOVER_MODES:
			func = svdm_rsp.modes;
			break;
		case CMD_ENTER_MODE:
			func = svdm_rsp.enter_mode;
			break;
		case CMD_DP_STATUS:
			if (svdm_rsp.amode)
				func = svdm_rsp.amode->status;
			break;
		case CMD_DP_CONFIG:
			if (svdm_rsp.amode)
				func = svdm_rsp.amode->config;
			break;
		case CMD_EXIT_MODE:
			func = svdm_rsp.exit_mode;
			break;
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
		case CMD_ATTENTION:
			/*
			 * attention is only SVDM with no response
			 * (just goodCRC) return zero here.
			 */
			dfp_consume_attention(port, payload);
			return 0;
#endif
		default:
			CPRINTF("ERR:CMD:%d\n", cmd);
			rsize = 0;
		}
		if (func)
			rsize = func(port, payload);
		else /* not supported : NACK it */
			rsize = 0;
		if (rsize >= 1)
			payload[0] |= VDO_CMDT(CMDT_RSP_ACK);
		else if (!rsize) {
			payload[0] |= VDO_CMDT(CMDT_RSP_NAK);
			rsize = 1;
		} else {
			payload[0] |= VDO_CMDT(CMDT_RSP_BUSY);
			rsize = 1;
		}
		payload[0] |= VDO_SVDM_VERS(pd_get_vdo_ver(port, TCPC_TX_SOP));
	} else if (cmd_type == CMDT_RSP_ACK) {
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
		struct svdm_amode_data *modep;

		modep = pd_get_amode_data(port, PD_VDO_VID(payload[0]));
#endif
		switch (cmd) {
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
		case CMD_DISCOVER_IDENT:
			/* Received a SOP' Discover Ident msg */
			if (is_transmit_msg_sop_prime(port)) {
				/* Store cable type */
				dfp_consume_cable_response(port, cnt, payload,
						       head);

				/*
				 * Enter USB4 mode if the cable supports USB4
				 * operation and has USB4 VDO.
				 */
				if (is_usb4_mode_enabled(port) &&
				    is_cable_ready_to_enter_usb4(port, cnt)) {
					enable_enter_usb4_mode(port);
					usb_mux_set_safe_mode(port);
					disable_transmit_sop_prime(port);
					/*
					 * To change the mode of operation from
					 * USB4 the port needs to be
					 * reconfigured.
					 * Ref: USB Type-C Cable and Connectot
					 * Specification section 5.4.4.
					 *
					 */
					disable_tbt_compat_mode(port);
					rsize = 0;
					break;
				}

				/*
				 * Disable Thunderbolt-compatible mode if the
				 * cable does not support superspeed
				 */
				if (is_tbt_compat_enabled(port) &&
					!is_tbt_cable_superspeed(port)) {
					disable_tbt_compat_mode(port);
				}

				rsize = dfp_discover_svids(payload);

				disable_transmit_sop_prime(port);
			/* Received a SOP Discover Ident Message */
			} else if (IS_ENABLED(CONFIG_USB_PD_DECODE_SOP) &&
				is_sop_prime_ready(port) &&
				board_is_tbt_usb4_port(port)) {
				pd_dfp_discovery_init(port);
				dfp_consume_identity(port, cnt, payload);

				/* Enable USB4 mode if USB4 VDO present
				 * and port partner supports USB Rev 3.0.
				 */
				if (is_usb4_vdo(port, cnt, payload) &&
				    PD_HEADER_REV(head)	== PD_REV30) {
					enable_usb4_mode(port);
				}

				/*
				 * Enable Thunderbolt-compatible mode
				 * if the modal operation is supported
				 */
				if (is_modal(port, cnt, payload))
					enable_tbt_compat_mode(port);

				if (is_modal(port, cnt, payload) ||
				    is_usb4_vdo(port, cnt, payload)) {
					rsize = dfp_discover_ident(payload);
					enable_transmit_sop_prime(port);
				} else {
					rsize = dfp_discover_svids(payload);
				}
			} else {
				pd_dfp_discovery_init(port);
				dfp_consume_identity(port, cnt, payload);
				rsize = dfp_discover_svids(payload);
			}
#ifdef CONFIG_CHARGE_MANAGER
			if (pd_charge_from_device(pd_get_identity_vid(port),
						  pd_get_identity_pid(port)))
				charge_manager_update_dualrole(port,
							       CAP_DEDICATED);
#endif
			break;
		case CMD_DISCOVER_SVID:
			rsize = process_am_discover_svids(port, cnt, payload);
			break;
		case CMD_DISCOVER_MODES:
			/*
			 * The pd_discovery structure stores SOP and SOP'
			 * discovery results separately, but TCPMv1 depends on
			 * one-dimensional storage of SVIDs and modes.
			 * Therefore, always use TCPC_TX_SOP in TCPMv1.
			 */
			dfp_consume_modes(port, TCPC_TX_SOP, cnt, payload);
			if (is_tbt_compat_enabled(port) &&
				is_tbt_compat_mode(port, cnt, payload)) {
				rsize = process_tbt_compat_discover_modes(
					      port,
					      get_tcpm_transmit_msg_type(port),
					      payload);
				break;
			}

			rsize = dfp_discover_modes(port, payload);
			/* enter the default mode for DFP */
			if (!rsize) {
				/*
				 * Disabling Thunderbolt-Compatible mode if
				 * discover mode response doesn't include Intel
				 * SVID.
				 */
				disable_tbt_compat_mode(port);
				payload[0] = pd_dfp_enter_mode(port, 0, 0);
				if (payload[0])
					rsize = 1;
			}
			break;
		case CMD_ENTER_MODE:
			if (is_tbt_compat_enabled(port)) {
				rsize = obj_cnt_enter_tbt_compat_mode(port,
					      get_tcpm_transmit_msg_type(port),
					      payload);
			/*
			 * Continue with PD flow if Thunderbolt-compatible mode
			 * is disabled.
			 */
			} else if (!modep) {
				rsize = 0;
			} else {
				if (!modep->opos)
					pd_dfp_enter_mode(port, 0, 0);

				if (modep->opos) {
					rsize = modep->fx->status(port,
								  payload);
					payload[0] |= PD_VDO_OPOS(modep->opos);
				}
			}
			break;
		case CMD_DP_STATUS:
			/* DP status response & UFP's DP attention have same
			   payload */
			dfp_consume_attention(port, payload);
			if (modep && modep->opos)
				rsize = modep->fx->config(port, payload);
			else
				rsize = 0;
			break;
		case CMD_DP_CONFIG:
			if (modep && modep->opos && modep->fx->post_config)
				modep->fx->post_config(port);
			/* no response after DFPs ack */
			rsize = 0;
			break;
		case CMD_EXIT_MODE:
			/* no response after DFPs ack */
			rsize = 0;
			break;
#endif
		case CMD_ATTENTION:
			/* no response after DFPs ack */
			rsize = 0;
			break;
		default:
			CPRINTF("ERR:CMD:%d\n", cmd);
			rsize = 0;
		}

		payload[0] |= VDO_CMDT(CMDT_INIT);
		payload[0] |= VDO_SVDM_VERS(pd_get_vdo_ver(port, TCPC_TX_SOP));
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
	} else if (cmd_type == CMDT_RSP_BUSY) {
		switch (cmd) {
		case CMD_DISCOVER_IDENT:
		case CMD_DISCOVER_SVID:
		case CMD_DISCOVER_MODES:
			/* resend if its discovery */
			rsize = 1;
			break;
		case CMD_ENTER_MODE:
			/* Error */
			CPRINTF("ERR:ENTBUSY\n");
			rsize = 0;
			break;
		case CMD_EXIT_MODE:
			rsize = 0;
			break;
		default:
			rsize = 0;
		}
	} else if (cmd_type == CMDT_RSP_NAK) {
		/* Passive cable Nacked for Discover SVID */
		if (cmd == CMD_DISCOVER_SVID && is_tbt_compat_enabled(port) &&
		    is_transmit_msg_sop_prime(port) &&
		    get_usb_pd_cable_type(port) == IDH_PTYPE_PCABLE) {
			limit_tbt_cable_speed(port);
			rsize = dfp_discover_modes(port, payload);
			disable_transmit_sop_prime(port);
		} else {
			rsize = 0;
		}
#endif /* CONFIG_USB_PD_ALT_MODE_DFP */
	} else {
		CPRINTF("ERR:CMDT:%d\n", cmd);
		/* do not answer */
		rsize = 0;
	}
	return rsize;
}

#else

int pd_svdm(int port, int cnt, uint32_t *payload, uint32_t **rpayload,
		uint16_t head)
{
	return 0;
}

#endif /* CONFIG_USB_PD_ALT_MODE */

#define FW_RW_END (CONFIG_EC_WRITABLE_STORAGE_OFF + \
		   CONFIG_RW_STORAGE_OFF + CONFIG_RW_SIZE)

uint8_t *flash_hash_rw(void)
{
	static struct sha256_ctx ctx;

	/* re-calculate RW hash when changed as its time consuming */
	if (rw_flash_changed) {
		rw_flash_changed = 0;
		SHA256_init(&ctx);
		SHA256_update(&ctx, (void *)CONFIG_PROGRAM_MEMORY_BASE +
			      CONFIG_RW_MEM_OFF,
			      CONFIG_RW_SIZE - RSANUMBYTES);
		return SHA256_final(&ctx);
	} else {
		return ctx.buf;
	}
}

void pd_get_info(uint32_t *info_data)
{
	void *rw_hash = flash_hash_rw();

	/* copy first 20 bytes of RW hash */
	memcpy(info_data, rw_hash, 5 * sizeof(uint32_t));
	/* copy other info into data msg */
#if defined(CONFIG_USB_PD_HW_DEV_ID_BOARD_MAJOR) && \
	defined(CONFIG_USB_PD_HW_DEV_ID_BOARD_MINOR)
	info_data[5] = VDO_INFO(CONFIG_USB_PD_HW_DEV_ID_BOARD_MAJOR,
				CONFIG_USB_PD_HW_DEV_ID_BOARD_MINOR,
				ver_get_num_commits(system_get_image_copy()),
				(system_get_image_copy() != EC_IMAGE_RO));
#else
	info_data[5] = 0;
#endif
}

int pd_custom_flash_vdm(int port, int cnt, uint32_t *payload)
{
	static int flash_offset;
	int rsize = 1; /* default is just VDM header returned */

	switch (PD_VDO_CMD(payload[0])) {
	case VDO_CMD_VERSION:
		memcpy(payload + 1, &current_image_data.version, 24);
		rsize = 7;
		break;
	case VDO_CMD_REBOOT:
		/* ensure the power supply is in a safe state */
		pd_power_supply_reset(0);
		system_reset(0);
		break;
	case VDO_CMD_READ_INFO:
		/* copy info into response */
		pd_get_info(payload + 1);
		rsize = 7;
		break;
	case VDO_CMD_FLASH_ERASE:
		/* do not kill the code under our feet */
		if (system_get_image_copy() != EC_IMAGE_RO)
			break;
		pd_log_event(PD_EVENT_ACC_RW_ERASE, 0, 0, NULL);
		flash_offset = CONFIG_EC_WRITABLE_STORAGE_OFF +
			       CONFIG_RW_STORAGE_OFF;
		flash_physical_erase(CONFIG_EC_WRITABLE_STORAGE_OFF +
				     CONFIG_RW_STORAGE_OFF, CONFIG_RW_SIZE);
		rw_flash_changed = 1;
		break;
	case VDO_CMD_FLASH_WRITE:
		/* do not kill the code under our feet */
		if ((system_get_image_copy() != EC_IMAGE_RO) ||
		    (flash_offset < CONFIG_EC_WRITABLE_STORAGE_OFF +
				    CONFIG_RW_STORAGE_OFF))
			break;
		flash_physical_write(flash_offset, 4*(cnt - 1),
				     (const char *)(payload+1));
		flash_offset += 4*(cnt - 1);
		rw_flash_changed = 1;
		break;
	case VDO_CMD_ERASE_SIG:
		/* this is not touching the code area */
		{
			uint32_t zero = 0;
			int offset;
			/* zeroes the area containing the RSA signature */
			for (offset = FW_RW_END - RSANUMBYTES;
			     offset < FW_RW_END; offset += 4)
				flash_physical_write(offset, 4,
						     (const char *)&zero);
		}
		break;
	default:
		/* Unknown : do not answer */
		return 0;
	}
	return rsize;
}
