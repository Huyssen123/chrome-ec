/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Register map */

#ifndef __CROS_EC_REGISTERS_H
#define __CROS_EC_REGISTERS_H

#include "common.h"
#include "compile_time_macros.h"

#define UNIMPLEMENTED_GPIO_BANK 0

#define SCP_REG_BASE			0x70000000

/* clock control */
#define SCP_CLK_CTRL_BASE		(SCP_REG_BASE + 0x21000)
/* clock gate */
#define SCP_SET_CLK_CG			REG32(SCP_CLK_CTRL_BASE + 0x0030)
#define   CG_TIMER_MCLK			BIT(0)
#define   CG_TIMER_BCLK			BIT(1)
#define   CG_MAD_MCLK			BIT(2)
#define   CG_I2C_MCLK			BIT(3)
#define   CG_I2C_BCLK			BIT(4)
#define   CG_GPIO_MCLK			BIT(5)
#define   CG_AP2P_MCLK			BIT(6)
#define   CG_UART0_MCLK			BIT(7)
#define   CG_UART0_BCLK			BIT(8)
#define   CG_UART0_RST			BIT(9)
#define   CG_UART1_MCLK			BIT(10)
#define   CG_UART1_BCLK			BIT(11)
#define   CG_UART1_RST			BIT(12)
#define   CG_SPI0			BIT(13)
#define   CG_SPI1			BIT(14)
#define   CG_SPI2			BIT(15)
#define   CG_DMA_CH0			BIT(16)
#define   CG_DMA_CH1			BIT(17)
#define   CG_DMA_CH2			BIT(18)
#define   CG_DMA_CH3			BIT(19)
#define   CG_I3C0			BIT(21)
#define   CG_I3C1			BIT(22)
#define   CG_DMA2_CH0			BIT(23)
#define   CG_DMA2_CH1			BIT(24)
#define   CG_DMA2_CH2			BIT(25)
#define   CG_DMA2_CH3			BIT(26)
/* UART clock select */
#define SCP_UART_CK_SEL			REG32(SCP_CLK_CTRL_BASE + 0x0044)
#define   UART0_CK_SEL_SHIFT		0
#define   UART0_CK_SEL_MASK		(0x3 << UART0_CK_SEL_SHIFT)
#define   UART0_CK_SEL_VAL(v)		((v) & UART0_CK_SEL_MASK)
#define   UART0_CK_SW_STATUS_MASK	(0xf << 8)
#define   UART0_CK_SW_STATUS_VAL(v)	((v) & UART0_CK_SW_STATUS_MASK)
#define   UART1_CK_SEL_SHIFT		16
#define   UART1_CK_SEL_MASK		(0x3 << UART1_CK_SEL_SHIFT)
#define   UART1_CK_SEL_VAL(v)		((v) & UART1_CK_SEL_MASK)
#define   UART1_CK_SW_STATUS_MASK	(0xf << 24)
#define   UART1_CK_SW_STATUS_VAL(v)	((v) & UART1_CK_SW_STATUS_MASK)
#define     UART_CK_SEL_26M		0
#define     UART_CK_SEL_32K		1
#define     UART_CK_SEL_ULPOSC		2
#define     UART_CK_SW_STATUS_26M	BIT(0)
#define     UART_CK_SW_STATUS_32K	BIT(1)
#define     UART_CK_SW_STATUS_ULPOS	BIT(2)

/* IPC */
#define SCP_SCP2APMCU_IPC_SET		REG32(SCP_REG_BASE + 0x24080)
#define SCP_SCP2SPM_IPC_SET		REG32(SCP_REG_BASE + 0x24090)
#define   IPC_SCP2HOST			BIT(0)
#define SCP_GIPC_IN_SET			REG32(SCP_REG_BASE + 0x24098)
#define SCP_GIPC_IN_CLR			REG32(SCP_REG_BASE + 0x2409C)
#define   GIPC_IN(n)			BIT(n)

/* UART */
#define SCP_UART_COUNT			2
#define UART_TX_IRQ(n)			CONCAT3(SCP_IRQ_UART, n, _TX)
#define UART_RX_IRQ(n)			CONCAT3(SCP_IRQ_UART, n, _RX)
#define SCP_UART0_BASE			(SCP_REG_BASE + 0x26000)
#define SCP_UART1_BASE			(SCP_REG_BASE + 0x27000)
#define SCP_UART_BASE(n)		CONCAT3(SCP_UART, n, _BASE)
#define UART_REG(n, offset)		REG32_ADDR(SCP_UART_BASE(n))[offset]

/* WDT */
#define SCP_CORE0_WDT_IRQ		REG32(SCP_REG_BASE + 0x30030)
#define SCP_CORE0_WDT_CFG		REG32(SCP_REG_BASE + 0x30034)
#define   WDT_FREQ			33825 /* 0xFFFFF / 31 */
#define   WDT_MAX_PERIOD		0xFFFFF /* 31 seconds */
#define   WDT_PERIOD(ms)		(WDT_FREQ * (ms) / 1000)
#define   WDT_EN			BIT(31)
#define SCP_CORE0_WDT_KICK		REG32(SCP_REG_BASE + 0x30038)
#define SCP_CORE0_WDT_CUR_VAL		REG32(SCP_REG_BASE + 0x3003C)

/* INTC */
#define SCP_INTC_IRQ_POL0		0xef001f20
#define SCP_INTC_IRQ_POL1		0x0800001d
#define SCP_INTC_IRQ_POL2		0x00000020
#define SCP_INTC_WORD(irq)		((irq) >> 5) /* word length = 2^5 */
#define SCP_INTC_BIT(irq)		((irq) & 0x1F) /* bit shift =LSB[0:4] */
#define SCP_INTC_GRP_COUNT		15
#define SCP_INTC_GRP_LEN		3
#define SCP_INTC_GRP_GAP		4
#define SCP_INTC_IRQ_COUNT		96
#define SCP_CORE0_INTC_IRQ_BASE		(SCP_REG_BASE + 0x32000)
#define SCP_CORE0_INTC_IRQ_STA(w) \
		REG32_ADDR(SCP_CORE0_INTC_IRQ_BASE + 0x0010)[(w)]
#define SCP_CORE0_INTC_IRQ_EN(w) \
		REG32_ADDR(SCP_CORE0_INTC_IRQ_BASE + 0x0020)[(w)]
#define SCP_CORE0_INTC_IRQ_POL(w) \
		REG32_ADDR(SCP_CORE0_INTC_IRQ_BASE + 0x0040)[(w)]
#define SCP_CORE0_INTC_IRQ_GRP(g, w) \
		REG32_ADDR(SCP_CORE0_INTC_IRQ_BASE + 0x0050 + \
			   ((g) << SCP_INTC_GRP_GAP))[(w)]
#define SCP_CORE0_INTC_IRQ_GRP_STA(g, w) \
		REG32_ADDR(SCP_CORE0_INTC_IRQ_BASE + 0x0150 + \
			   ((g) << SCP_INTC_GRP_GAP))[(w)]
#define SCP_CORE0_INTC_SLP_WAKE_EN(w) \
		REG32_ADDR(SCP_CORE0_INTC_IRQ_BASE + 0x0240)[(w)]
#define SCP_CORE0_INTC_IRQ_OUT		REG32(SCP_CORE0_INTC_IRQ_BASE + 0x0250)
/* UART */
#define SCP_CORE0_INTC_UART0_RX_IRQ	REG32(SCP_CORE0_INTC_IRQ_BASE + 0x0258)
#define SCP_CORE0_INTC_UART1_RX_IRQ	REG32(SCP_CORE0_INTC_IRQ_BASE + 0x025C)
#define SCP_CORE0_INTC_UART_RX_IRQ(n)	CONCAT3(SCP_CORE0_INTC_UART, n, _RX_IRQ)

/* XGPT (general purpose timer) */
#define NUM_TIMERS			6
#define SCP_CORE0_TIMER_BASE(n)		(SCP_REG_BASE + 0x33000 + (0x10 * (n)))
#define SCP_CORE0_TIMER_EN(n)		REG32(SCP_CORE0_TIMER_BASE(n) + 0x0000)
#define   TIMER_EN			BIT(0)
#define   TIMER_CLK_SRC_32K		(0 << 4)
#define   TIMER_CLK_SRC_26M		(1 << 4)
#define   TIMER_CLK_SRC_BCLK		(2 << 4)
#define   TIMER_CLK_SRC_MCLK		(3 << 4)
#define   TIMER_CLK_SRC_MASK		(3 << 4)
#define SCP_CORE0_TIMER_RST_VAL(n)	REG32(SCP_CORE0_TIMER_BASE(n) + 0x0004)
#define SCP_CORE0_TIMER_CUR_VAL(n)	REG32(SCP_CORE0_TIMER_BASE(n) + 0x0008)
#define SCP_CORE0_TIMER_IRQ_CTRL(n)	REG32(SCP_CORE0_TIMER_BASE(n) + 0x000C)
#define   TIMER_IRQ_EN			BIT(0)
#define   TIMER_IRQ_STATUS		BIT(4)
#define   TIMER_IRQ_CLR			BIT(5)
#define SCP_IRQ_TIMER(n)		CONCAT2(SCP_IRQ_TIMER, n)

/* memory remap */
#define SCP_R_REMAP_0X0123		REG32(SCP_REG_BASE + 0xA5060)
#define SCP_R_REMAP_0X4567		REG32(SCP_REG_BASE + 0xA5064)
#define SCP_R_REMAP_0X89AB		REG32(SCP_REG_BASE + 0xA5068)
#define SCP_R_REMAP_0XCDEF		REG32(SCP_REG_BASE + 0xA506C)

/* external address: AP */
#define AP_REG_BASE			0x60000000 /* 0x10000000 remap to 0x6 */
/* AP GPIO */
#define AP_GPIO_BASE			(AP_REG_BASE + 0x5000)
#define AP_GPIO_MODE11_SET		REG32(AP_GPIO_BASE + 0x03B4)
#define AP_GPIO_MODE11_CLR		REG32(AP_GPIO_BASE + 0x03B8)
#define AP_GPIO_MODE20_SET		REG32(AP_GPIO_BASE + 0x0444)
#define AP_GPIO_MODE20_CLR		REG32(AP_GPIO_BASE + 0x0448)

/* IRQ numbers */
#define SCP_IRQ_GIPC_IN0		0
#define SCP_IRQ_GIPC_IN1		1
#define SCP_IRQ_GIPC_IN2		2
#define SCP_IRQ_GIPC_IN3		3
/* 4 */
#define SCP_IRQ_SPM			4
#define SCP_IRQ_AP_CIRQ			5
#define SCP_IRQ_EINT			6
#define SCP_IRQ_PMIC			7
/* 8 */
#define SCP_IRQ_UART0_TX		8
#define SCP_IRQ_UART1_TX		9
#define SCP_IRQ_I2C0			10
#define SCP_IRQ_I2C1_0			11
/* 12 */
#define SCP_IRQ_BUS_DBG_TRACKER		12
#define SCP_IRQ_CLK_CTRL		13
#define SCP_IRQ_VOW			14
#define SCP_IRQ_TIMER0			15
/* 16 */
#define SCP_IRQ_TIMER1			16
#define SCP_IRQ_TIMER2			17
#define SCP_IRQ_TIMER3			18
#define SCP_IRQ_TIMER4			19
/* 20 */
#define SCP_IRQ_TIMER5			20
#define SCP_IRQ_OS_TIMER		21
#define SCP_IRQ_UART0_RX		22
#define SCP_IRQ_UART1_RX		23
/* 24 */
#define SCP_IRQ_GDMA			24
#define SCP_IRQ_AUDIO			25
#define SCP_IRQ_MD_DSP			26
#define SCP_IRQ_ADSP			27
/* 28 */
#define SCP_IRQ_CPU_TICK		28
#define SCP_IRQ_SPI0			29
#define SCP_IRQ_SPI1			30
#define SCP_IRQ_SPI2			31
/* 32 */
#define SCP_IRQ_NEW_INFRA_SYS_CIRQ	32
#define SCP_IRQ_DBG			33
#define SCP_IRQ_CCIF0			34
#define SCP_IRQ_CCIF1			35
/* 36 */
#define SCP_IRQ_CCIF2			36
#define SCP_IRQ_WDT			37
#define SCP_IRQ_USB0			38
#define SCP_IRQ_USB1			39
/* 40 */
#define SCP_IRQ_DPMAIF			40
#define SCP_IRQ_INFRA			41
#define SCP_IRQ_CLK_CTRL_CORE		42
#define SCP_IRQ_CLK_CTRL2_CORE		43
/* 44 */
#define SCP_IRQ_CLK_CTRL2		44
#define SCP_IRQ_GIPC_IN4		45 /* HALT */
#define SCP_IRQ_PERIBUS_TIMEOUT		46
#define SCP_IRQ_INFRABUS_TIMEOUT	47
/* 48 */
#define SCP_IRQ_MET0			48
#define SCP_IRQ_MET1			49
#define SCP_IRQ_MET2			50
#define SCP_IRQ_MET3			51
/* 52 */
#define SCP_IRQ_AP_WDT			52
#define SCP_IRQ_L2TCM_SEC_VIO		53
#define SCP_IRQ_CPU_TICK1		54
#define SCP_IRQ_MAD_DATAIN		55
/* 56 */
#define SCP_IRQ_I3C0_IBI_WAKE		56
#define SCP_IRQ_I3C1_IBI_WAKE		57
#define SCP_IRQ_I3C2_IBI_WAKE		58
#define SCP_IRQ_APU_ENGINE		59
/* 60 */
#define SCP_IRQ_MBOX0			60
#define SCP_IRQ_MBOX1			61
#define SCP_IRQ_MBOX2			62
#define SCP_IRQ_MBOX3			63
/* 64 */
#define SCP_IRQ_MBOX4			64
#define SCP_IRQ_SYS_CLK_REQ		65
#define SCP_IRQ_BUS_REQ			66
#define SCP_IRQ_APSRC_REQ		67
/* 68 */
#define SCP_IRQ_APU_MBOX		68
#define SCP_IRQ_DEVAPC_SECURE_VIO	69
/* 72 */
/* 76 */
#define SCP_IRQ_I2C1_2			78
#define SCP_IRQ_I2C2			79
/* 80 */
#define SCP_IRQ_AUD2AUDIODSP		80
#define SCP_IRQ_AUD2AUDIODSP_2		81
#define SCP_IRQ_CONN2ADSP_A2DPOL	82
#define SCP_IRQ_CONN2ADSP_BTCVSD	83
/* 84 */
#define SCP_IRQ_CONN2ADSP_BLEISO	84
#define SCP_IRQ_PCIE2ADSP		85
#define SCP_IRQ_APU2ADSP_ENGINE		86
#define SCP_IRQ_APU2ADSP_MBOX		87
/* 88 */
#define SCP_IRQ_CCIF3			88
#define SCP_IRQ_I2C_DMA0		89
#define SCP_IRQ_I2C_DMA1		90
#define SCP_IRQ_I2C_DMA2		91
/* 92 */
#define SCP_IRQ_I2C_DMA3		92

#endif /* __CROS_EC_REGISTERS_H */
