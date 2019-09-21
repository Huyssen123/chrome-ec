# -*- makefile -*-
# Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Andestar v3m architecture core OS files build
#

# Select Andes bare-metal toolchain
$(call set-option,CROSS_COMPILE,$(CROSS_COMPILE_nds32),/opt/coreboot-sdk/bin/nds32le-elf-)

# CPU specific compilation flags
CFLAGS_CPU+=-march=v3m -Os

core-y=cpu.o init.o panic.o task.o switch.o __muldi3.o math.o
