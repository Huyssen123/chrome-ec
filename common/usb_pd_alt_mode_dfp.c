/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Alternate Mode Downstream Facing Port (DFP) USB-PD module.
 */

#include "chipset.h"
#include "console.h"
#include "task.h"
#include "task_id.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"
#include "util.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#else
#define CPRINTS(format, args...)
#define CPRINTF(format, args...)
#endif

#ifndef PORT_TO_HPD
#define PORT_TO_HPD(port) ((port) ? GPIO_USB_C1_DP_HPD : GPIO_USB_C0_DP_HPD)
#endif /* PORT_TO_HPD */

/* Tracker for which task is waiting on sysjump prep to finish */
static volatile task_id_t sysjump_task_waiting = TASK_ID_INVALID;

/*
 * timestamp of the next possible toggle to ensure the 2-ms spacing
 * between IRQ_HPD.  Since this is used in overridable functions, this
 * has to be global.
 */
uint64_t svdm_hpd_deadline[CONFIG_USB_PD_PORT_MAX_COUNT];

int dp_flags[CONFIG_USB_PD_PORT_MAX_COUNT];

uint32_t dp_status[CONFIG_USB_PD_PORT_MAX_COUNT];

__overridable const struct svdm_response svdm_rsp = {
	.identity = NULL,
	.svids = NULL,
	.modes = NULL,
};

static int pd_get_mode_idx(int port, uint16_t svid)
{
	int i;
	/* TODO(b/150611251): Support SOP' */
	struct pd_discovery *disc = pd_get_am_discovery(port, TCPC_TX_SOP);

	for (i = 0; i < PD_AMODE_COUNT; i++) {
		if (disc->amodes[i].fx &&
		    (disc->amodes[i].fx->svid == svid))
			return i;
	}
	return -1;
}

static int pd_allocate_mode(int port, uint16_t svid)
{
	int i, j;
	struct svdm_amode_data *modep;
	int mode_idx = pd_get_mode_idx(port, svid);
	/* TODO(b/150611251): Support SOP' and SOP'' */
	struct pd_discovery *disc = pd_get_am_discovery(port, TCPC_TX_SOP);

	if (mode_idx != -1)
		return mode_idx;

	/* There's no space to enter another mode */
	if (disc->amode_idx == PD_AMODE_COUNT) {
		CPRINTF("ERR:NO AMODE SPACE\n");
		return -1;
	}

	/* Allocate ...  if SVID == 0 enter default supported policy */
	for (i = 0; i < supported_modes_cnt; i++) {
		for (j = 0; j < disc->svid_cnt; j++) {
			struct svid_mode_data *svidp = &disc->svids[j];

			if ((svidp->svid != supported_modes[i].svid) ||
			    (svid && (svidp->svid != svid)))
				continue;

			modep = &disc->amodes[disc->amode_idx];
			modep->fx = &supported_modes[i];
			modep->data = &disc->svids[j];
			disc->amode_idx++;
			return disc->amode_idx - 1;
		}
	}
	return -1;
}

static int validate_mode_request(struct svdm_amode_data *modep,
				 uint16_t svid, int opos)
{
	if (!modep->fx)
		return 0;

	if (svid != modep->fx->svid) {
		CPRINTF("ERR:svid r:0x%04x != c:0x%04x\n",
			svid, modep->fx->svid);
		return 0;
	}

	if (opos != modep->opos) {
		CPRINTF("ERR:opos r:%d != c:%d\n",
			opos, modep->opos);
		return 0;
	}

	return 1;
}

void pd_prepare_sysjump(void)
{
	int i;

	/* Exit modes before sysjump so we can cleanly enter again later */
	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		/*
		 * If the port is not capable of Alternate mode no need to
		 * send the event.
		 */
		if (!pd_alt_mode_capable(i))
			continue;

		sysjump_task_waiting = task_get_current();
		task_set_event(PD_PORT_TO_TASK_ID(i), PD_EVENT_SYSJUMP, 0);
		task_wait_event_mask(TASK_EVENT_SYSJUMP_READY, -1);
		sysjump_task_waiting = TASK_ID_INVALID;
	}
}

/*
 * This algorithm defaults to choosing higher pin config over lower ones in
 * order to prefer multi-function if desired.
 *
 *  NAME | SIGNALING | OUTPUT TYPE | MULTI-FUNCTION | PIN CONFIG
 * -------------------------------------------------------------
 *  A    |  USB G2   |  ?          | no             | 00_0001
 *  B    |  USB G2   |  ?          | yes            | 00_0010
 *  C    |  DP       |  CONVERTED  | no             | 00_0100
 *  D    |  PD       |  CONVERTED  | yes            | 00_1000
 *  E    |  DP       |  DP         | no             | 01_0000
 *  F    |  PD       |  DP         | yes            | 10_0000
 *
 * if UFP has NOT asserted multi-function preferred code masks away B/D/F
 * leaving only A/C/E.  For single-output dongles that should leave only one
 * possible pin config depending on whether its a converter DP->(VGA|HDMI) or DP
 * output.  If UFP is a USB-C receptacle it may assert C/D/E/F.  The DFP USB-C
 * receptacle must always choose C/D in those cases.
 */
int pd_dfp_dp_get_pin_mode(int port, uint32_t status)
{
	struct svdm_amode_data *modep =
				pd_get_amode_data(port, USB_SID_DISPLAYPORT);
	uint32_t mode_caps;
	uint32_t pin_caps;

	if (!modep)
		return 0;

	mode_caps = modep->data->mode_vdo[modep->opos - 1];

	/* TODO(crosbug.com/p/39656) revisit with DFP that can be a sink */
	pin_caps = PD_DP_PIN_CAPS(mode_caps);

	/* if don't want multi-function then ignore those pin configs */
	if (!PD_VDO_DPSTS_MF_PREF(status))
		pin_caps &= ~MODE_DP_PIN_MF_MASK;

	/* TODO(crosbug.com/p/39656) revisit if DFP drives USB Gen 2 signals */
	pin_caps &= ~MODE_DP_PIN_BR2_MASK;

	/* if C/D present they have precedence over E/F for USB-C->USB-C */
	if (pin_caps & (MODE_DP_PIN_C | MODE_DP_PIN_D))
		pin_caps &= ~(MODE_DP_PIN_E | MODE_DP_PIN_F);

	/* get_next_bit returns undefined for zero */
	if (!pin_caps)
		return 0;

	return 1 << get_next_bit(&pin_caps);
}

struct svdm_amode_data *pd_get_amode_data(int port, uint16_t svid)
{
	int idx = pd_get_mode_idx(port, svid);
	/* TODO(b/150611251): Support SOP' */
	struct pd_discovery *disc = pd_get_am_discovery(port, TCPC_TX_SOP);

	return (idx == -1) ? NULL : &disc->amodes[idx];
}

/*
 * Enter default mode ( payload[0] == 0 ) or attempt to enter mode via svid &
 * opos
 */
uint32_t pd_dfp_enter_mode(int port, uint16_t svid, int opos)
{
	int mode_idx = pd_allocate_mode(port, svid);
	/* TODO(b/150611251): Support SOP' */
	struct pd_discovery *disc = pd_get_am_discovery(port, TCPC_TX_SOP);
	struct svdm_amode_data *modep;
	uint32_t mode_caps;

	if (mode_idx == -1)
		return 0;
	modep = &disc->amodes[mode_idx];

	if (!opos) {
		/* choose the lowest as default */
		modep->opos = 1;
	} else if (opos <= modep->data->mode_cnt) {
		modep->opos = opos;
	} else {
		CPRINTF("opos error\n");
		return 0;
	}

	mode_caps = modep->data->mode_vdo[modep->opos - 1];
	if (modep->fx->enter(port, mode_caps) == -1)
		return 0;

	pd_set_dfp_enter_mode_flag(port, true);

	/* SVDM to send to UFP for mode entry */
	return VDO(modep->fx->svid, 1, CMD_ENTER_MODE | VDO_OPOS(modep->opos));
}

int pd_dfp_exit_mode(int port, uint16_t svid, int opos)
{
	struct svdm_amode_data *modep;
	/* TODO(b/150611251): Support SOP' */
	struct pd_discovery *disc = pd_get_am_discovery(port, TCPC_TX_SOP);
	int idx;

	/*
	 * Empty svid signals we should reset DFP VDM state by exiting all
	 * entered modes then clearing state.  This occurs when we've
	 * disconnected or for hard reset.
	 */
	if (!svid) {
		for (idx = 0; idx < PD_AMODE_COUNT; idx++)
			if (disc->amodes[idx].fx)
				disc->amodes[idx].fx->exit(port);

		pd_dfp_discovery_init(port);
		return 0;
	}

	/*
	 * TODO(crosbug.com/p/33946) : below needs revisited to allow multiple
	 * mode exit.  Additionally it should honor OPOS == 7 as DFP's request
	 * to exit all modes.  We currently don't have any UFPs that support
	 * multiple modes on one SVID.
	 */
	modep = pd_get_amode_data(port, svid);
	if (!modep || !validate_mode_request(modep, svid, opos))
		return 0;

	/* call DFPs exit function */
	modep->fx->exit(port);

	pd_set_dfp_enter_mode_flag(port, false);

	/* exit the mode */
	modep->opos = 0;
	return 1;
}

void dfp_consume_attention(int port, uint32_t *payload)
{
	uint16_t svid = PD_VDO_VID(payload[0]);
	int opos = PD_VDO_OPOS(payload[0]);
	struct svdm_amode_data *modep = pd_get_amode_data(port, svid);

	if (!modep || !validate_mode_request(modep, svid, opos))
		return;

	if (modep->fx->attention)
		modep->fx->attention(port, payload);
}

void dfp_consume_identity(int port, int cnt, uint32_t *payload)
{
	int ptype = PD_IDH_PTYPE(payload[VDO_I(IDH)]);
	struct pd_discovery *disc = pd_get_am_discovery(port, TCPC_TX_SOP);
	size_t identity_size = MIN(sizeof(union disc_ident_ack),
				   (cnt - 1) * sizeof(uint32_t));
	/* Note: only store VDOs, not the VDM header */
	memcpy(disc->identity.raw_value, payload + 1, identity_size);

	switch (ptype) {
	case IDH_PTYPE_AMA:
		/* Leave vbus ON if the following macro is false */
		if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE) &&
			IS_ENABLED(CONFIG_USBC_VCONN_SWAP)) {
			/* Adapter is requesting vconn, try to supply it */
			if (PD_VDO_AMA_VCONN_REQ(payload[VDO_I(AMA)]))
				pd_try_vconn_src(port);

			/* Only disable vbus if vconn was requested */
			if (PD_VDO_AMA_VCONN_REQ(payload[VDO_I(AMA)]) &&
				!PD_VDO_AMA_VBUS_REQ(payload[VDO_I(AMA)]))
				pd_power_supply_reset(port);
		}
		break;
	default:
		break;
	}
	pd_set_identity_discovery(port, TCPC_TX_SOP, PD_DISC_COMPLETE);
}

void dfp_consume_svids(int port, enum tcpm_transmit_type type, int cnt,
		uint32_t *payload)
{
	int i;
	uint32_t *ptr = payload + 1;
	int vdo = 1;
	uint16_t svid0, svid1;
	struct pd_discovery *disc = pd_get_am_discovery(port, type);

	for (i = disc->svid_cnt; i < disc->svid_cnt + 12; i += 2) {
		if (i >= SVID_DISCOVERY_MAX) {
			CPRINTF("ERR:SVIDCNT\n");
			break;
		}
		/*
		 * Verify we're still within the valid packet (count will be one
		 * for the VDM header + xVDOs)
		 */
		if (vdo >= cnt)
			break;

		svid0 = PD_VDO_SVID_SVID0(*ptr);
		if (!svid0)
			break;
		disc->svids[i].svid = svid0;
		disc->svid_cnt++;

		svid1 = PD_VDO_SVID_SVID1(*ptr);
		if (!svid1)
			break;
		disc->svids[i + 1].svid = svid1;
		disc->svid_cnt++;
		ptr++;
		vdo++;
	}
	/* TODO(tbroch) need to re-issue discover svids if > 12 */
	if (i && ((i % 12) == 0))
		CPRINTF("ERR:SVID+12\n");

	pd_set_svids_discovery(port, type, PD_DISC_COMPLETE);
}

void dfp_consume_modes(int port, enum tcpm_transmit_type type, int cnt,
		uint32_t *payload)
{
	int svid_idx;
	struct svid_mode_data *mode_discovery = NULL;
	struct pd_discovery *disc = pd_get_am_discovery(port, type);
	uint16_t response_svid = (uint16_t) PD_VDO_VID(payload[0]);

	for (svid_idx = 0; svid_idx < disc->svid_cnt; ++svid_idx) {
		uint16_t svid = disc->svids[svid_idx].svid;

		if (svid == response_svid) {
			mode_discovery = &disc->svids[svid_idx];
			break;
		}
	}
	if (!mode_discovery) {
		const struct svid_mode_data *requested_mode_data =
			pd_get_next_mode(port, type);
		CPRINTF("C%d: Mode response for undiscovered SVID %x, but TCPM "
				"requested SVID %x\n",
				port, response_svid, requested_mode_data->svid);
		/*
		 * Although SVIDs discovery seemed like it succeeded before, the
		 * partner is now responding with undiscovered SVIDs. Discovery
		 * cannot reasonably continue under these circumstances.
		 */
		pd_set_modes_discovery(port, type, requested_mode_data->svid,
				PD_DISC_FAIL);
		return;
	}

	mode_discovery->mode_cnt = cnt - 1;
	if (mode_discovery->mode_cnt < 1) {
		CPRINTF("ERR:NOMODE\n");
		pd_set_modes_discovery(port, type, mode_discovery->svid,
				PD_DISC_FAIL);
		return;
	}

	memcpy(mode_discovery->mode_vdo, &payload[1],
			sizeof(uint32_t) * mode_discovery->mode_cnt);
	disc->svid_idx++;
	pd_set_modes_discovery(port, type, mode_discovery->svid,
			PD_DISC_COMPLETE);
}

/*
 * TODO(b/152417597): Move this function to usb_pd_policy.c after TCPMv2 stops
 * using it.
 */
int dfp_discover_modes(int port, uint32_t *payload)
{
	struct pd_discovery *disc = pd_get_am_discovery(port, TCPC_TX_SOP);
	uint16_t svid = disc->svids[disc->svid_idx].svid;

	if (disc->svid_idx >= disc->svid_cnt)
		return 0;

	payload[0] = VDO(svid, 1, CMD_DISCOVER_MODES);

	return 1;
}

int pd_alt_mode(int port, uint16_t svid)
{
	struct svdm_amode_data *modep = pd_get_amode_data(port, svid);

	return (modep) ? modep->opos : -1;
}

void pd_set_identity_discovery(int port, enum tcpm_transmit_type type,
			       enum pd_discovery_state disc)
{
	struct pd_discovery *pd = pd_get_am_discovery(port, type);

	pd->identity_discovery = disc;
}

enum pd_discovery_state pd_get_identity_discovery(int port,
						  enum tcpm_transmit_type type)
{
	struct pd_discovery *disc = pd_get_am_discovery(port, type);

	return disc->identity_discovery;
}

const union disc_ident_ack *pd_get_identity_response(int port,
					       enum tcpm_transmit_type type)
{
	if (type >= DISCOVERY_TYPE_COUNT)
		return NULL;

	return &pd_get_am_discovery(port, type)->identity;
}

uint16_t pd_get_identity_vid(int port)
{
	const union disc_ident_ack *resp = pd_get_identity_response(port,
								TCPC_TX_SOP);

	return resp->idh.usb_vendor_id;
}

uint16_t pd_get_identity_pid(int port)
{
	const union disc_ident_ack *resp = pd_get_identity_response(port,
								TCPC_TX_SOP);

	return resp->product.product_id;
}

uint8_t pd_get_product_type(int port)
{
	const union disc_ident_ack *resp = pd_get_identity_response(port,
								TCPC_TX_SOP);

	return resp->idh.product_type;
}

void pd_set_svids_discovery(int port, enum tcpm_transmit_type type,
			       enum pd_discovery_state disc)
{
	struct pd_discovery *pd = pd_get_am_discovery(port, type);

	pd->svids_discovery = disc;
}

enum pd_discovery_state pd_get_svids_discovery(int port,
		enum tcpm_transmit_type type)
{
	struct pd_discovery *disc = pd_get_am_discovery(port, type);

	return disc->svids_discovery;
}

int pd_get_svid_count(int port, enum tcpm_transmit_type type)
{
	struct pd_discovery *disc = pd_get_am_discovery(port, type);

	return disc->svid_cnt;
}

uint16_t pd_get_svid(int port, uint16_t svid_idx, enum tcpm_transmit_type type)
{
	struct pd_discovery *disc = pd_get_am_discovery(port, type);

	return disc->svids[svid_idx].svid;
}

void pd_set_modes_discovery(int port, enum tcpm_transmit_type type,
		uint16_t svid, enum pd_discovery_state disc)
{
	struct pd_discovery *pd = pd_get_am_discovery(port, type);
	int svid_idx;

	for (svid_idx = 0; svid_idx < pd->svid_cnt; ++svid_idx) {
		struct svid_mode_data *mode_data = &pd->svids[svid_idx];

		if (mode_data->svid != svid)
			continue;

		mode_data->discovery = disc;
		return;
	}
}

enum pd_discovery_state pd_get_modes_discovery(int port,
		enum tcpm_transmit_type type)
{
	const struct svid_mode_data *mode_data = pd_get_next_mode(port, type);

	/*
	 * If there are no SVIDs for which to discover modes, mode discovery is
	 * trivially complete.
	 */
	if (!mode_data)
		return PD_DISC_COMPLETE;

	return mode_data->discovery;
}

struct svid_mode_data *pd_get_next_mode(int port,
		enum tcpm_transmit_type type)
{
	struct pd_discovery *disc = pd_get_am_discovery(port, type);
	int svid_idx;

	for (svid_idx = 0; svid_idx < disc->svid_cnt; ++svid_idx) {
		struct svid_mode_data *mode_data = &disc->svids[svid_idx];

		if (mode_data->discovery == PD_DISC_COMPLETE)
			continue;

		return mode_data;
	}

	return NULL;
}

uint32_t *pd_get_mode_vdo(int port, uint16_t svid_idx,
		enum tcpm_transmit_type type)
{
	struct pd_discovery *disc = pd_get_am_discovery(port, type);

	return disc->svids[svid_idx].mode_vdo;
}

void notify_sysjump_ready(void)
{
	/*
	 * If event was set from pd_prepare_sysjump, wake the
	 * task waiting on us to complete.
	 */
	if (sysjump_task_waiting != TASK_ID_INVALID)
		task_set_event(sysjump_task_waiting,
				TASK_EVENT_SYSJUMP_READY, 0);
}

/*
 * Before entering into alternate mode, state of the USB-C MUX
 * needs to be in safe mode.
 * Ref: USB Type-C Cable and Connector Specification
 * Section E.2.2 Alternate Mode Electrical Requirements
 */
void usb_mux_set_safe_mode(int port)
{
	if (IS_ENABLED(CONFIG_USBC_SS_MUX)) {
		usb_mux_set(port, IS_ENABLED(CONFIG_USB_MUX_VIRTUAL) ?
			USB_PD_MUX_SAFE_MODE : USB_PD_MUX_NONE,
			USB_SWITCH_CONNECT, pd_get_polarity(port));
	}

	/* Isolate the SBU lines. */
	if (IS_ENABLED(CONFIG_USBC_PPC_SBU))
		ppc_set_sbu(port, 0);
}

bool is_vdo_present(int cnt, int index)
{
	return cnt > index;
}

/*
 * ############################################################################
 *
 * Cable communication functions
 *
 * ############################################################################
 */
enum idh_ptype get_usb_pd_cable_type(int port)
{
	struct pd_cable *cable = pd_get_cable_attributes(port);

	return cable->type;
}

bool is_usb2_cable_support(int port)
{
	struct pd_cable *cable = pd_get_cable_attributes(port);

	return cable->type == IDH_PTYPE_PCABLE ||
		cable->attr2.a2_rev30.usb_20_support == USB2_SUPPORTED;
}

bool is_cable_speed_gen2_capable(int port)
{
	struct pd_cable *cable = pd_get_cable_attributes(port);

	switch (cable->rev) {
	case PD_REV20:
		return cable->attr.p_rev20.ss == USB_R20_SS_U31_GEN1_GEN2;

	case PD_REV30:
		return cable->attr.p_rev30.ss == USB_R30_SS_U32_U40_GEN2 ||
			cable->attr.p_rev30.ss == USB_R30_SS_U40_GEN3;
	default:
		return false;
	}
}

bool is_active_cable_element_retimer(int port)
{
	struct pd_cable *cable = pd_get_cable_attributes(port);

	/* Ref: USB PD Spec 2.0 Table 6-29 Active Cable VDO
	 * Revision 2 Active cables do not have Active element support.
	 */
	return cable->rev & PD_REV30 &&
		get_usb_pd_cable_type(port) == IDH_PTYPE_ACABLE &&
		cable->attr2.a2_rev30.active_elem == ACTIVE_RETIMER;
}

/*
 * TODO(b/152417597): Support SOP and SOP'; eliminate redundant code for port
 * partner and cable identity discovery.
 */
void dfp_consume_cable_response(int port, int cnt, uint32_t *payload,
				uint16_t head)
{
	struct pd_cable *cable = pd_get_cable_attributes(port);
	struct pd_discovery *disc =
		pd_get_am_discovery(port, TCPC_TX_SOP_PRIME);
	size_t identity_size = MIN(sizeof(union disc_ident_ack),
				   (cnt - 1) * sizeof(uint32_t));

	if (!IS_ENABLED(CONFIG_USB_PD_DECODE_SOP))
		return;

	/* Note: only store VDOs, not the VDM header */
	memcpy(disc->identity.raw_value, payload + 1, identity_size);

	pd_set_identity_discovery(port, TCPC_TX_SOP_PRIME, PD_DISC_COMPLETE);

	/* Get cable rev */
	cable->rev = PD_HEADER_REV(head);

	/* TODO: Move cable references to use discovery response */
	if (is_vdo_present(cnt, VDO_INDEX_IDH)) {
		cable->type = PD_IDH_PTYPE(payload[VDO_INDEX_IDH]);

		if (is_vdo_present(cnt, VDO_INDEX_PTYPE_CABLE1))
			cable->attr.raw_value =
					payload[VDO_INDEX_PTYPE_CABLE1];

		/*
		 * Ref USB PD Spec 3.0  Pg 145. For active cable there are two
		 * VDOs. Hence storing the second VDO.
		 */
		if (is_vdo_present(cnt, VDO_INDEX_PTYPE_CABLE2))
			cable->attr2.raw_value =
					payload[VDO_INDEX_PTYPE_CABLE2];

		cable->is_identified = 1;
	}
}

/*
 * ############################################################################
 *
 * Thunderbolt-Compatible functions
 *
 * ############################################################################
 */

/* TODO (b/148528713): Need to enable Thunderbolt-compatible mode on TCPMv2 */
void set_tbt_compat_mode_ready(int port)
{
	if (IS_ENABLED(CONFIG_USBC_SS_MUX) &&
	    IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE)) {
		/* Connect the SBU and USB lines to the connector. */
		if (IS_ENABLED(CONFIG_USBC_PPC_SBU))
			ppc_set_sbu(port, 1);

		/* Set usb mux to Thunderbolt-compatible mode */
		usb_mux_set(port, USB_PD_MUX_TBT_COMPAT_ENABLED,
			USB_SWITCH_CONNECT, pd_get_polarity(port));
	}
}

/*
 * Ref: USB Type-C Cable and Connector Specification
 * Figure F-1 TBT3 Discovery Flow
 */
bool is_tbt_cable_superspeed(int port)
{
	struct pd_cable *cable;

	if (!IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE) ||
	    !IS_ENABLED(CONFIG_USB_PD_DECODE_SOP))
		return false;

	cable = pd_get_cable_attributes(port);

	/* Product type is Active cable, hence don't check for speed */
	if (cable->type == IDH_PTYPE_ACABLE)
		return true;

	if (cable->type != IDH_PTYPE_PCABLE)
		return false;

	if (IS_ENABLED(CONFIG_USB_PD_REV30) && cable->rev == PD_REV30)
		return cable->attr.p_rev30.ss == USB_R30_SS_U32_U40_GEN1 ||
			cable->attr.p_rev30.ss == USB_R30_SS_U32_U40_GEN2 ||
			cable->attr.p_rev30.ss == USB_R30_SS_U40_GEN3;

	return cable->attr.p_rev20.ss == USB_R20_SS_U31_GEN1 ||
		cable->attr.p_rev20.ss == USB_R20_SS_U31_GEN1_GEN2;
}

bool is_modal(int port, int cnt, const uint32_t *payload)
{
	return is_vdo_present(cnt, VDO_INDEX_IDH) &&
		PD_IDH_IS_MODAL(payload[VDO_INDEX_IDH]);
}

bool is_intel_svid(int port, int prev_svid_cnt)
{
	int i;
	/* TODO(b/148528713): Use TCPMv2's separate storage for SOP'. */
	struct pd_discovery *disc = pd_get_am_discovery(port, TCPC_TX_SOP);

	/*
	 * Ref: USB Type-C cable and connector specification, Table F-9
	 * Check if SVID0 = USB_VID_INTEL. However,
	 * errata: All the Thunderbolt certified cables and docks tested have
	 * SVID1 = 0x8087.
	 * Hence, check all the SVIDs for Intel SVID, if the response presents
	 * SVIDs in any order.
	 */
	if (IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE)) {
		for (i = prev_svid_cnt;
				i < pd_get_svid_count(port, TCPC_TX_SOP); i++) {
			if (disc->svids[i].svid == USB_VID_INTEL)
				return true;
		}
	}
	return false;
}

bool is_tbt_compat_mode(int port, int cnt, const uint32_t *payload)
{
	/*
	 * Ref: USB Type-C cable and connector specification
	 * F.2.5 TBT3 Device Discover Mode Responses
	 */
	return is_vdo_present(cnt, VDO_INDEX_IDH) &&
		PD_VDO_RESP_MODE_INTEL_TBT(payload[VDO_INDEX_IDH]);
}

bool cable_supports_tbt_speed(int port)
{
	struct pd_cable *cable = pd_get_cable_attributes(port);

	return (cable->cable_mode_resp.tbt_cable_speed == TBT_SS_TBT_GEN3 ||
		cable->cable_mode_resp.tbt_cable_speed == TBT_SS_U32_GEN1_GEN2);
}

/*
 * Enter Thunderbolt-compatible mode
 * Reference: USB Type-C cable and connector specification, Release 2.0
 */
int enter_tbt_compat_mode(int port, enum tcpm_transmit_type sop,
			uint32_t *payload)
{
	union tbt_dev_mode_enter_cmd enter_dev_mode = { .raw_value = 0 };
	struct pd_cable *cable = pd_get_cable_attributes(port);

	/* Table F-12 TBT3 Cable Enter Mode Command */
	payload[0] = pd_dfp_enter_mode(port, USB_VID_INTEL, 0) |
					VDO_SVDM_VERS(VDM_VER20);

	/* For TBT3 Cable Enter Mode Command, number of Objects is 1 */
	if ((sop == TCPC_TX_SOP_PRIME) ||
	    (sop == TCPC_TX_SOP_PRIME_PRIME))
		return 1;

	usb_mux_set_safe_mode(port);

	/* Table F-13 TBT3 Device Enter Mode Command */
	enter_dev_mode.vendor_spec_b1 =
				cable->dev_mode_resp.vendor_spec_b1;
	enter_dev_mode.vendor_spec_b0 =
				cable->dev_mode_resp.vendor_spec_b0;
	enter_dev_mode.intel_spec_b0 = cable->dev_mode_resp.intel_spec_b0;
	enter_dev_mode.cable =
		get_usb_pd_cable_type(port) == IDH_PTYPE_PCABLE ?
			TBT_ENTER_PASSIVE_CABLE : TBT_ENTER_ACTIVE_CABLE;

	if (cable->cable_mode_resp.tbt_cable_speed == TBT_SS_TBT_GEN3) {
		enter_dev_mode.lsrx_comm =
			cable->cable_mode_resp.lsrx_comm;
		enter_dev_mode.retimer_type =
			cable->cable_mode_resp.retimer_type;
		enter_dev_mode.tbt_cable =
			cable->cable_mode_resp.tbt_cable;
		enter_dev_mode.tbt_rounded =
			cable->cable_mode_resp.tbt_rounded;
		enter_dev_mode.tbt_cable_speed =
			cable->cable_mode_resp.tbt_cable_speed;
	} else {
		enter_dev_mode.tbt_cable_speed = TBT_SS_U32_GEN1_GEN2;
	}
	enter_dev_mode.tbt_alt_mode = TBT_ALTERNATE_MODE;

	payload[1] = enter_dev_mode.raw_value;

	/* For TBT3 Device Enter Mode Command, number of Objects are 2 */
	return 2;
}

/* Return the current cable speed received from Cable Discover Mode command */
__overridable enum tbt_compat_cable_speed board_get_max_tbt_speed(int port)
{
	struct pd_cable *cable = pd_get_cable_attributes(port);

	return cable->cable_mode_resp.tbt_cable_speed;
}

/*
 * ############################################################################
 *
 * USB4 functions
 *
 * ############################################################################
 */

enum usb_rev30_ss get_usb4_cable_speed(int port)
{
	struct pd_cable *cable = pd_get_cable_attributes(port);

	/*
	 * TODO: Return USB4 cable speed for USB3.2 Gen 2 cables if DFP isn't
	 * Gen 3 capable.
	 */
	if ((cable->rev == PD_REV30) &&
	    (get_usb_pd_cable_type(port) == IDH_PTYPE_PCABLE) &&
	   ((cable->attr.p_rev30.ss != USB_R30_SS_U32_U40_GEN2) ||
	    !IS_ENABLED(CONFIG_USB_PD_TBT_GEN3_CAPABLE))) {
		return cable->attr.p_rev30.ss;
	}

	/*
	 * Converting Thunderolt-Compatible cable speed to equivalent USB4 cable
	 * speed.
	 */
	return cable->cable_mode_resp.tbt_cable_speed == TBT_SS_TBT_GEN3 ?
	       USB_R30_SS_U40_GEN3 : USB_R30_SS_U32_U40_GEN2;
}

__overridable void svdm_safe_dp_mode(int port)
{
	/* make DP interface safe until configure */
	dp_flags[port] = 0;
	dp_status[port] = 0;

	usb_mux_set_safe_mode(port);
}

__overridable int svdm_enter_dp_mode(int port, uint32_t mode_caps)
{
	/*
	 * Don't enter the mode if the SoC is off.
	 *
	 * There's no need to enter the mode while the SoC is off; we'll
	 * actually enter the mode on the chipset resume hook.  Entering DP Alt
	 * Mode twice will confuse some monitors and require and unplug/replug
	 * to get them to work again.  The DP Alt Mode on USB-C spec says that
	 * if we don't need to maintain HPD connectivity info in a low power
	 * mode, then we shall exit DP Alt Mode.  (This is why we don't enter
	 * when the SoC is off as opposed to suspend where adding a display
	 * could cause a wake up.)
	 */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		return -1;

	/* Only enter mode if device is DFP_D capable */
	if (mode_caps & MODE_DP_SNK) {
		svdm_safe_dp_mode(port);

		if (IS_ENABLED(CONFIG_MKBP_EVENT) &&
		    chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
			/*
			 * Wake the system up since we're entering DP AltMode.
			 */
			pd_notify_dp_alt_mode_entry();

		return 0;
	}

	return -1;
}

__overridable int svdm_dp_status(int port, uint32_t *payload)
{
	int opos = pd_alt_mode(port, USB_SID_DISPLAYPORT);

	payload[0] = VDO(USB_SID_DISPLAYPORT, 1,
			 CMD_DP_STATUS | VDO_OPOS(opos));
	payload[1] = VDO_DP_STATUS(0, /* HPD IRQ  ... not applicable */
				   0, /* HPD level ... not applicable */
				   0, /* exit DP? ... no */
				   0, /* usb mode? ... no */
				   0, /* multi-function ... no */
				   (!!(dp_flags[port] & DP_FLAGS_DP_ON)),
				   0, /* power low? ... no */
				   (!!DP_FLAGS_DP_ON));
	return 2;
};

__overridable uint8_t get_dp_pin_mode(int port)
{
	return pd_dfp_dp_get_pin_mode(port, dp_status[port]);
}

__overridable int svdm_dp_config(int port, uint32_t *payload)
{
	int opos = pd_alt_mode(port, USB_SID_DISPLAYPORT);
	int mf_pref = PD_VDO_DPSTS_MF_PREF(dp_status[port]);
	uint8_t pin_mode = get_dp_pin_mode(port);
	mux_state_t mux_mode;

	if (!pin_mode)
		return 0;

	/*
	 * Multi-function operation is only allowed if that pin config is
	 * supported.
	 */
	mux_mode = ((pin_mode & MODE_DP_PIN_MF_MASK) && mf_pref) ?
		USB_PD_MUX_DOCK : USB_PD_MUX_DP_ENABLED;
	CPRINTS("pin_mode: %x, mf: %d, mux: %d", pin_mode, mf_pref, mux_mode);

	/* Connect the SBU and USB lines to the connector. */
	if (IS_ENABLED(CONFIG_USBC_PPC_SBU))
		ppc_set_sbu(port, 1);
	usb_mux_set(port, mux_mode, USB_SWITCH_CONNECT, pd_get_polarity(port));

	payload[0] = VDO(USB_SID_DISPLAYPORT, 1,
			 CMD_DP_CONFIG | VDO_OPOS(opos));
	payload[1] = VDO_DP_CFG(pin_mode,      /* pin mode */
				1,	       /* DPv1.3 signaling */
				2);	       /* UFP connected */
	return 2;
};

__overridable void svdm_dp_post_config(int port)
{
	dp_flags[port] |= DP_FLAGS_DP_ON;
	if (!(dp_flags[port] & DP_FLAGS_HPD_HI_PENDING))
		return;

#ifdef CONFIG_USB_PD_DP_HPD_GPIO
	gpio_set_level(PORT_TO_HPD(port), 1);

	/* set the minimum time delay (2ms) for the next HPD IRQ */
	svdm_hpd_deadline[port] = get_time().val + HPD_USTREAM_DEBOUNCE_LVL;
#endif /* CONFIG_USB_PD_DP_HPD_GPIO */

	usb_mux_hpd_update(port, 1, 0);

#ifdef USB_PD_PORT_TCPC_MST
	if (port == USB_PD_PORT_TCPC_MST)
		baseboard_mst_enable_control(port, 1);
#endif
}

__overridable int svdm_dp_attention(int port, uint32_t *payload)
{
	int lvl = PD_VDO_DPSTS_HPD_LVL(payload[1]);
	int irq = PD_VDO_DPSTS_HPD_IRQ(payload[1]);
#ifdef CONFIG_USB_PD_DP_HPD_GPIO
	enum gpio_signal hpd = PORT_TO_HPD(port);
	int cur_lvl = gpio_get_level(hpd);
#endif /* CONFIG_USB_PD_DP_HPD_GPIO */

	dp_status[port] = payload[1];

	if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND) &&
	    (irq || lvl))
		/*
		 * Wake up the AP.  IRQ or level high indicates a DP sink is now
		 * present.
		 */
		if (IS_ENABLED(CONFIG_MKBP_EVENT))
			pd_notify_dp_alt_mode_entry();

	/* Its initial DP status message prior to config */
	if (!(dp_flags[port] & DP_FLAGS_DP_ON)) {
		if (lvl)
			dp_flags[port] |= DP_FLAGS_HPD_HI_PENDING;
		return 1;
	}

#ifdef CONFIG_USB_PD_DP_HPD_GPIO
	if (irq & !lvl) {
		/*
		 * IRQ can only be generated when the level is high, because
		 * the IRQ is signaled by a short low pulse from the high level.
		 */
		CPRINTF("ERR:HPD:IRQ&LOW\n");
		return 0; /* nak */
	}

	if (irq & cur_lvl) {
		uint64_t now = get_time().val;
		/* wait for the minimum spacing between IRQ_HPD if needed */
		if (now < svdm_hpd_deadline[port])
			usleep(svdm_hpd_deadline[port] - now);

		/* generate IRQ_HPD pulse */
		gpio_set_level(hpd, 0);
		usleep(HPD_DSTREAM_DEBOUNCE_IRQ);
		gpio_set_level(hpd, 1);
	} else {
		gpio_set_level(hpd, lvl);
	}

	/* set the minimum time delay (2ms) for the next HPD IRQ */
	svdm_hpd_deadline[port] = get_time().val + HPD_USTREAM_DEBOUNCE_LVL;
#endif /* CONFIG_USB_PD_DP_HPD_GPIO */

	usb_mux_hpd_update(port, lvl, irq);

#ifdef USB_PD_PORT_TCPC_MST
	if (port == USB_PD_PORT_TCPC_MST)
		baseboard_mst_enable_control(port, lvl);
#endif

	/* ack */
	return 1;
}

__overridable void svdm_exit_dp_mode(int port)
{
	svdm_safe_dp_mode(port);
#ifdef CONFIG_USB_PD_DP_HPD_GPIO
	gpio_set_level(PORT_TO_HPD(port), 0);
#endif /* CONFIG_USB_PD_DP_HPD_GPIO */
	usb_mux_hpd_update(port, 0, 0);
#ifdef USB_PD_PORT_TCPC_MST
	if (port == USB_PD_PORT_TCPC_MST)
		baseboard_mst_enable_control(port, 0);
#endif
}

__overridable int svdm_enter_gfu_mode(int port, uint32_t mode_caps)
{
	/* Always enter GFU mode */
	return 0;
}

__overridable void svdm_exit_gfu_mode(int port)
{
}

__overridable int svdm_gfu_status(int port, uint32_t *payload)
{
	/*
	 * This is called after enter mode is successful, send unstructured
	 * VDM to read info.
	 */
	pd_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_READ_INFO, NULL, 0);
	return 0;
}

__overridable int svdm_gfu_config(int port, uint32_t *payload)
{
	return 0;
}

__overridable int svdm_gfu_attention(int port, uint32_t *payload)
{
	return 0;
}

#ifdef CONFIG_USB_PD_TBT_COMPAT_MODE
__overridable int svdm_tbt_compat_enter_mode(int port, uint32_t mode_caps)
{
	return 0;
}

__overridable void svdm_tbt_compat_exit_mode(int port)
{
}

__overridable int svdm_tbt_compat_status(int port, uint32_t *payload)
{
	return 0;
}

__overridable int svdm_tbt_compat_config(int port, uint32_t *payload)
{
	return 0;
}

__overridable int svdm_tbt_compat_attention(int port, uint32_t *payload)
{
	return 0;
}
#endif /* CONFIG_USB_PD_TBT_COMPAT_MODE */

const struct svdm_amode_fx supported_modes[] = {
	{
		.svid = USB_SID_DISPLAYPORT,
		.enter = &svdm_enter_dp_mode,
		.status = &svdm_dp_status,
		.config = &svdm_dp_config,
		.post_config = &svdm_dp_post_config,
		.attention = &svdm_dp_attention,
		.exit = &svdm_exit_dp_mode,
	},

	{
		.svid = USB_VID_GOOGLE,
		.enter = &svdm_enter_gfu_mode,
		.status = &svdm_gfu_status,
		.config = &svdm_gfu_config,
		.attention = &svdm_gfu_attention,
		.exit = &svdm_exit_gfu_mode,
	},
#ifdef CONFIG_USB_PD_TBT_COMPAT_MODE
	{
		.svid = USB_VID_INTEL,
		.enter = &svdm_tbt_compat_enter_mode,
		.status = &svdm_tbt_compat_status,
		.config = &svdm_tbt_compat_config,
		.attention = &svdm_tbt_compat_attention,
		.exit = &svdm_tbt_compat_exit_mode,
	},
#endif /* CONFIG_USB_PD_TBT_COMPAT_MODE */
};
const int supported_modes_cnt = ARRAY_SIZE(supported_modes);
