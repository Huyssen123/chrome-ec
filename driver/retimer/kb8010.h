/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Driver for Kandou KB8001 USB-C 40 Gb/s multiprotocol switch.
 */

#ifndef __CROS_EC_KB8010_H
#define __CROS_EC_KB8010_H

#include "compile_time_macros.h"

#define KB8010_I2C_ADDR0_FLAGS 0x08
#define KB8010_I2C_ADDR1_FLAGS 0x0C

/* Set the protocol */
#define KB8010_REG_PROTOCOL 0x0001
#define KB8010_PROTOCOL_USB3 0x0
#define KB8010_PROTOCOL_DPMF 0x1
#define KB8010_PROTOCOL_DP 0x2
#define KB8010_PROTOCOL_USB4 0x3

/* Configure the lane orientaitons */
#define KB8010_REG_ORIENTATION 0x0002
#define KB8010_CABLE_TYPE_PASSIVE 0x10
#define KB8010_CABLE_TYPE_ACTIVE_UNIDIR 0x20
#define KB8010_CABLE_TYPE_ACTIVE_BIDIR 0x30

#define KB8010_REG_RESET 0x0006
#define KB8010_RESET_FSM BIT(0)
#define KB8010_RESET_MM BIT(1)
#define KB8010_RESET_SERDES BIT(2)
#define KB8010_RESET_COM BIT(3)
#define KB8010_RESET_MASK GENMASK(3, 0)

/* XBAR */
#define KB8010_REG_XBAR_OVR 0x5040

/* Registers to configure the elastic buffer input connection */
#define KB8010_REG_XBAR_EB1SEL 0x5044
#define KB8010_REG_XBAR_EB2SEL 0x5045
#define KB8010_REG_XBAR_EB4SEL 0x5046
#define KB8010_REG_XBAR_EB5SEL 0x5047
#define KB8010_REG_XBAR_TXASEL 0x5048
#define KB8010_REG_XBAR_TXBSEL 0x5049
#define KB8010_REG_XBAR_TXCSEL 0x504A
#define KB8010_REG_XBAR_TXDSEL 0x504B

/* Registers to configure SBU LSX/AUX flip */
#define KB8010_REG_XBAR_SBU_CFG 0x504C

/* DP */
#define KB8010_REG_DP_D_IEEE_OUI 0x603D

/* TODO: b:280629671
 * Following two registers don't have names yet,
 * need to work with Kandou to get names and update
 */
#define KB8010_REG_DP_D_FUNC_1 0x603E
#define KB8010_REG_DP_D_FUNC_2 0x603F
#define KB8010_REG_DP_L_EQ_CFG 0X6314
#define KB8010_REG_DFP_REPLY_TIMEOUT 0X6393

/* CIO */
#define KB8010_REG_CIO_CFG_WAKEUP_IGN_LS_DET 0X811F

/* SerDes broadcast */
#define KB8010_REG_SBBR_COMRX_CH_0_LINK_CTRL_RUN3 0XF02D
#define KB8010_REG_SBBR_COMRX_CH_1_LINK_CTRL_RUN3 0XF063
#define KB8010_REG_SBBR_COMRX_CH_SHARED_LINK_CTRL_RUN_OFFSET 0XF0A0
#define KB8010_REG_SBBR_COMRX_CH_SHARED_LINK_CTRL_RUN_POST_CDR_OFFSET 0XF0A9
#define KB8010_REG_SBBR_COMRX_AZC_CTRL_CTLE_OC_BW_STG1 0xF172
#define KB8010_REG_SBBR_COMRX_AZC_CTRL_CTLE_OC_BW_STG2 0xF173
#define KB8010_REG_SBBR_COMRX_AZC_CTRL_CTLE_OC_BW_STG3 0xF174
#define KB8010_REG_SBBR_COMRX_LFPS_LFPS_CTRL 0XF242
#define KB8010_REG_SBBR_COMTX_OUTPUT_DRIVER_MISC_OVR_EN 0XF2CC
#define KB8010_REG_SBBR_BR_RX_CAL_OFFSET_EYE_BG_SAT_OVF 0XFECA
#define KB8010_REG_SBBR_BR_RX_CAL_VGA2_GXR 0XFFB9

#endif /* __CROS_EC_KB8010_H  */
