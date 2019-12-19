# -*- makefile -*-
# Copyright 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# NPCX chip specific files build
#

# NPCX SoC has a Cortex-M4F ARM core
CORE:=cortex-m
# Allow the full Cortex-M4 instruction set
CFLAGS_CPU+=-march=armv7e-m -mcpu=cortex-m4

# Required chip modules
chip-y=header.o clock.o gpio.o hwtimer.o jtag.o system.o uart.o

# Optional chip modules
chip-$(CONFIG_ADC)+=adc.o
chip-$(CONFIG_FANS)+=fan.o
chip-$(CONFIG_FLASH)+=flash.o
chip-$(CONFIG_I2C)+=i2c.o
chip-$(CONFIG_LPC)+=lpc.o
chip-$(CONFIG_PECI)+=peci.o
chip-$(CONFIG_SHI)+=shi.o
# pwm functions are implemented with the fan functions
chip-$(CONFIG_PWM)+=pwm.o fan.o
chip-$(CONFIG_SPI)+=spi.o
chip-$(CONFIG_WATCHDOG)+=watchdog.o
chip-$(HAS_TASK_KEYSCAN)+=keyboard_raw.o

# spi flash program fw for openocd
npcx-flash-fw=chip/npcx/spiflashfw/ec_npcxflash
npcx-flash-fw-bin=${out}/$(npcx-flash-fw).bin
PROJECT_EXTRA+=${npcx-flash-fw-bin}
