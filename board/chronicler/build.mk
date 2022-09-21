# -*- makefile -*-
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

CHIP:=npcx
CHIP_FAMILY:=npcx7
CHIP_VARIANT:=npcx7m7fc
BASEBOARD:=volteer

board-y=board.o
board-y+=battery.o
board-y+=cbi.o
board-y+=led.o
board-y+=keyboard.o
board-y+=usbc_config.o
