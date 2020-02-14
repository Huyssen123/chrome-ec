/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_IT8XXX2_H
#define __CROS_EC_CONFIG_CHIP_IT8XXX2_H

/* CPU core BFD configuration */
#include "core/riscv-rv32i/config_core.h"

 /* RISCV core */
#define CHIP_CORE_RISCV
#define CHIP_ILM_DLM_ORDER
/* The base address of EC interrupt controller registers. */
#define CHIP_EC_INTC_BASE           0x00F03F00

/****************************************************************************/
/* Memory mapping */

#define CHIP_ILM_BASE               0x80000000
#define CHIP_H2RAM_BASE             0x80081000 /* 0x80081000~0x80081FFF */
#define CHIP_RAMCODE_BASE           0x80082000 /* 0x80082000~0x80082FFF */
#define CHIP_EXTRA_STACK_SPACE      128
/* We reserve 12KB space for ramcode, h2ram, and immu sections. */
#define CHIP_RAM_SPACE_RESERVED     0x3000

#define CONFIG_RAM_BASE             0x80080000
#define CONFIG_RAM_SIZE             0x00010000

#define CONFIG_PROGRAM_MEMORY_BASE  (CHIP_ILM_BASE)

#if defined(CHIP_VARIANT_IT83202BX)
/* TODO(b/133460224): enable properly chip config option. */
#define CONFIG_FLASH_SIZE           0x00080000
/*
 * ADC control pin order change:
 * ADC13 control pin GPL0               GPL1
 * ADC14 control pin GPL1  change to    GPL2
 * ADC15 control pin GPL2  --------->   GPL3
 * ADC16 control pin GPL3               GPL0
 */
#define IT83XX_CHIP_ADC_PIN_ORDER_CHANGE
/* Embedded flash is KGD */
#define IT83XX_CHIP_FLASH_IS_KGD
/* Don't let internal flash go into deep power down mode. */
#define IT83XX_CHIP_FLASH_NO_DEEP_POWER_DOWN
/* chip id is 3 bytes */
#define IT83XX_CHIP_ID_3BYTES
/*
 * Disable eSPI pad, then PLL change
 * (include EC clock frequency) is succeed even CS# is low.
 */
#define IT83XX_ESPI_INHIBIT_CS_BY_PAD_DISABLED
/* The slave frequency is adjustable (bit[2-0] at register IT83XX_ESPI_GCAC1) */
#define IT83XX_ESPI_SLAVE_MAX_FREQ_CONFIGURABLE
/* Watchdog reset supports hardware reset. */
#define IT83XX_ETWD_HW_RESET_SUPPORT
/*
 * More GPIOs can be set as 1.8v input.
 * Please refer to gpio_1p8v_sel[] for 1.8v GPIOs.
 */
#define IT83XX_GPIO_1P8V_PIN_EXTENDED
/* All GPIOs support interrupt on rising, falling, and either edge. */
#define IT83XX_GPIO_INT_FLEXIBLE
/* Enable detect type-c plug in interrupt. */
#define IT83XX_INTC_PLUG_IN_SUPPORT
/* Chip IT83202BX actually has TCPC physical port count. */
#define IT83XX_USBPD_PHY_PORT_COUNT    3
#else
#error "Unsupported chip variant!"
#endif

#endif  /* __CROS_EC_CONFIG_CHIP_IT8XXX2_H */
