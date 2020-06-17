/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* OCPC - One Charger IC Per Type-C module */

#include "battery.h"
#include "charge_state_v2.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "math_util.h"
#include "ocpc.h"
#include "util.h"

/*
 * These constants were chosen by tuning the PID loop to reduce oscillations and
 * minimize overshoot.
 */
#define KP 1
#define KP_DIV 4
#define KI 1
#define KI_DIV 15
#define KD 1
#define KD_DIV 10

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHARGER, outstr)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)
#define CPRINTS_DBG(format, args...) \
do {							\
	if (debug_output)				\
		cprints(CC_CHARGER, format, ## args);	\
} while (0)

static int k_p = KP;
static int k_i = KI;
static int k_d = KD;
static int k_p_div = KP_DIV;
static int k_i_div = KI_DIV;
static int k_d_div = KD_DIV;
static int debug_output;

enum phase {
	PHASE_UNKNOWN = -1,
	PHASE_CC,
	PHASE_CV_TRIP,
	PHASE_CV_COMPLETE,
};

int ocpc_config_secondary_charger(int *desired_input_current,
				  struct ocpc_data *ocpc,
				  int voltage_mv, int current_ma)
{
	int rv = EC_SUCCESS;
	struct batt_params batt;
	const struct battery_info *batt_info;
	struct charger_params charger;
	int vsys_target = 0;
	int drive = 0;
	int i_ma;
	int min_vsys_target;
	int error = 0;
	int derivative = 0;
	static enum phase ph;
	static int prev_limited;
	int chgnum;
	enum ec_error_list result;

	/*
	 * There's nothing to do if we're not using this charger.  Should
	 * there be more than two charger ICs in the future, the following check
	 * should change to ensure that only the active charger IC is acted
	 * upon.
	 */
	chgnum = charge_get_active_chg_chip();
	if (chgnum != SECONDARY_CHARGER)
		return EC_ERROR_INVAL;

	result = charger_set_vsys_compensation(chgnum, ocpc, current_ma,
					       voltage_mv);
	switch (result) {
	case EC_SUCCESS:
		/* No further action required, so we're done here. */
		return EC_SUCCESS;

	case EC_ERROR_UNIMPLEMENTED:
		/* Let's get to work */
		break;

	default:
		/* Something went wrong configuring the auxiliary charger IC. */
		CPRINTS("Failed to set VSYS compensation! (%d) (result: %d)",
			chgnum, result);
		return result;
	}

	if (ocpc->last_vsys == OCPC_UNINIT)
		ph = PHASE_UNKNOWN;

	if (current_ma == 0) {
		vsys_target = voltage_mv;
		goto set_vsys;
	}

	/*
	 * We need to induce a current flow that matches the requested current
	 * by raising VSYS.  Let's start by getting the latest data that we
	 * know of.
	 */
	batt_info = battery_get_info();
	battery_get_params(&batt);
	ocpc_get_adcs(ocpc);
	charger_get_params(&charger);

	/* Set our current target accordingly. */
	if (batt.voltage < batt.desired_voltage) {
		if (ph < PHASE_CV_TRIP)
			ph = PHASE_CC;
		i_ma = batt.desired_current;
	} else{
		/*
		 * Once the battery voltage reaches the desired voltage, we
		 * should note that we've reached the CV step and set VSYS to
		 * the desired CV + offset.
		 */
		i_ma = batt.current;
		ph = ph == PHASE_CC ? PHASE_CV_TRIP : PHASE_CV_COMPLETE;

	}

	/* Ensure our target is not negative. */
	i_ma = MAX(i_ma, 0);

	/*
	 * We'll use our current target and our combined Rsys+Rbatt to seed our
	 * VSYS target.  However, we'll use a PID loop to correct the error and
	 * help drive VSYS to what it _should_ be in order to reach our current
	 * target.  The first time through this function, we won't make any
	 * corrections in order to determine our initial error.
	 */
	if (ocpc->last_vsys != OCPC_UNINIT) {
		error = i_ma - batt.current;
		/* Add some hysteresis. */
		if (ABS(error) < 4)
			error = 0;

		derivative = error - ocpc->last_error;
		ocpc->last_error = error;
		ocpc->integral +=  error;
		if (ocpc->integral > 500)
			ocpc->integral = 500;
	}

	CPRINTS_DBG("phase = %d", ph);
	CPRINTS_DBG("error = %dmA", error);
	CPRINTS_DBG("derivative = %d", derivative);
	CPRINTS_DBG("integral = %d", ocpc->integral);
	CPRINTS_DBG("batt.voltage = %dmV", batt.voltage);
	CPRINTS_DBG("batt.desired_voltage = %dmV", batt.desired_voltage);
	CPRINTS_DBG("batt.desired_current = %dmA", batt.desired_current);
	CPRINTS_DBG("batt.current = %dmA", batt.current);
	CPRINTS_DBG("i_ma = %dmA", i_ma);

	/*
	 * Assuming that our combined Rsys + Rbatt resistance is correct, this
	 * should be enough to reach our desired i_ma.  If it's not, our PID
	 * loop will help us get there.
	 */
	min_vsys_target = (i_ma * ocpc->combined_rsys_rbatt_mo) / 1000;
	min_vsys_target += MIN(batt.voltage, batt.desired_voltage);
	CPRINTS_DBG("min_vsys_target = %d", min_vsys_target);

	/* Obtain the drive from our PID controller. */
	if (ocpc->last_vsys != OCPC_UNINIT) {
		drive = (k_p * error / k_p_div) +
			(k_i * ocpc->integral / k_i_div) +
			(k_d * derivative / k_d_div);
		/*
		 * Let's limit upward transitions to 500mV.  It's okay to reduce
		 * VSYS rather quickly, but we'll be conservative on
		 * increasing VSYS.
		 */
		if (drive > 500)
			drive = 500;
		CPRINTS_DBG("drive = %d", drive);
	}

	/*
	 * Adjust our VSYS target by applying the calculated drive.  Note that
	 * we won't apply our drive the first time through this function such
	 * that we can determine our initial error.
	 */
	if (ocpc->last_vsys != OCPC_UNINIT)
		vsys_target = ocpc->last_vsys + drive;

	/*
	 * Once we're in the CV region, all we need to do is keep VSYS at the
	 * desired voltage.
	 */
	if (ph >= PHASE_CV_TRIP)
		vsys_target = batt.desired_voltage;

	/*
	 * Ensure VSYS is no higher than 1V over the max battery voltage, but
	 * greater than or equal to our minimum VSYS target.
	 */
	vsys_target = CLAMP(vsys_target, min_vsys_target,
			    batt_info->voltage_max+1000);

	/* If we're input current limited, we cannot increase VSYS any more. */
	CPRINTS_DBG("OCPC: Inst. Input Current: %dmA (Limit: %dmA)",
		    ocpc->secondary_ibus_ma, *desired_input_current);
	if ((ocpc->secondary_ibus_ma >= (*desired_input_current * 95 / 100)) &&
	    (vsys_target > ocpc->last_vsys) &&
	    (ocpc->last_vsys != OCPC_UNINIT)) {
		if (!prev_limited)
			CPRINTS("Input limited! Not increasing VSYS");
		prev_limited = 1;
		return rv;
	}
	prev_limited = 0;

set_vsys:
	/* To reduce spam, only print when we change VSYS significantly. */
	if ((ABS(vsys_target - ocpc->last_vsys) > 10) || debug_output)
		CPRINTS("OCPC: Target VSYS: %dmV", vsys_target);
	charger_set_voltage(SECONDARY_CHARGER, vsys_target);
	ocpc->last_vsys = vsys_target;

	return rv;
}

void ocpc_get_adcs(struct ocpc_data *ocpc)
{
	int val;

	val = 0;
	if (!charger_get_vbus_voltage(PRIMARY_CHARGER, &val))
		ocpc->primary_vbus_mv = val;

	val = 0;
	if (!charger_get_vbus_voltage(SECONDARY_CHARGER, &val))
		ocpc->secondary_vbus_mv = val;

	val = 0;
	if (!charger_get_input_current(PRIMARY_CHARGER, &val))
		ocpc->primary_ibus_ma = val;

	val = 0;
	if (!charger_get_input_current(SECONDARY_CHARGER, &val))
		ocpc->secondary_ibus_ma = val;
}

__overridable void ocpc_get_pid_constants(int *kp, int *kp_div,
					  int *ki, int *ki_div,
					  int *kd, int *kd_div)
{
}

static void ocpc_set_pid_constants(void)
{
	ocpc_get_pid_constants(&k_p, &k_p_div, &k_i, &k_i_div, &k_d, &k_d_div);
}
DECLARE_HOOK(HOOK_INIT, ocpc_set_pid_constants, HOOK_PRIO_DEFAULT);

static int command_ocpcdebug(int argc, char **argv)
{
	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	if (!parse_bool(argv[1], &debug_output))
		return EC_ERROR_PARAM1;

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(ocpcdebug, command_ocpcdebug,
			     "<enable/disable>",
			     "enable/disable debug prints for OCPC data");

static int command_ocpcpid(int argc, char **argv)
{
	int *num, *denom;

	if (argc == 4) {
		switch (argv[1][0]) {
		case 'p':
			num = &k_p;
			denom = &k_p_div;
			break;

		case 'i':
			num = &k_i;
			denom = &k_i_div;
			break;

		case 'd':
			num = &k_d;
			denom = &k_d_div;
			break;
		default:
			return EC_ERROR_PARAM1;
		}

		*num = atoi(argv[2]);
		*denom = atoi(argv[3]);
	}

	/* Print the current constants */
	ccprintf("Kp = %d / %d\n", k_p, k_p_div);
	ccprintf("Ki = %d / %d\n", k_i, k_i_div);
	ccprintf("Kd = %d / %d\n", k_d, k_d_div);
	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(ocpcpid, command_ocpcpid,
			     "[<k/p/d> <numerator> <denominator>]",
			     "Show/Set PID constants for OCPC PID loop");
