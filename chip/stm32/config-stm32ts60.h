/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Memory mapping */
#define CONFIG_FLASH_BASE       0x08000000
#define CONFIG_FLASH_PHYSICAL_SIZE 0x0010000 /* Actually 0x8000 */
#define CONFIG_FLASH_BANK_SIZE  0x1000  /* TODO */
#define CONFIG_FLASH_ERASE_SIZE 0x0400  /* TODO erase bank size */
#define CONFIG_FLASH_WRITE_SIZE 0x0002  /* TODO minimum write size */

/* No page mode on STM32F, so no benefit to larger write sizes */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE 0x0002

#define CONFIG_RAM_BASE         0x20000000
#define CONFIG_RAM_SIZE         0x00002800

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT 59

/* Reduced history because of limited RAM */
#undef CONFIG_CONSOLE_HISTORY
#define CONFIG_CONSOLE_HISTORY 3

/* Only USART2 support */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 2

/* USB packet ram config */
#define CONFIG_USB_RAM_BASE        0x40006000
#define CONFIG_USB_RAM_SIZE        512
#define CONFIG_USB_RAM_ACCESS_TYPE uint32_t
#define CONFIG_USB_RAM_ACCESS_SIZE 4
