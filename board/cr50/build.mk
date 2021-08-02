# -*- makefile -*-
# Copyright 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board-specific build requirements

# Define the SoC used by this board
CHIP:=g
CHIP_FAMILY:=cr50
CHIP_VARIANT ?= cr50_fpga

# This file is included twice by the Makefile, once to determine the CHIP info
# and then again after defining all the CONFIG_ and HAS_TASK variables. We use
# a guard so that recipe definitions and variable extensions only happen the
# second time.
ifeq ($(BOARD_MK_INCLUDED_ONCE),)

# List of variables which can be defined in the environment or set in the make
# command line.
ENV_VARS := CR50_DEV CRYPTO_TEST H1_RED_BOARD

ifneq ($(CRYPTO_TEST),)
CPPFLAGS += -DCRYPTO_TEST_SETUP
endif


BOARD_MK_INCLUDED_ONCE=1
SIG_EXTRA = --cros
else

# Need to generate a .hex file
all: hex

# The simulator components have their own subdirectory
CFLAGS += -I$(realpath $(BDIR)/dcrypto)
CFLAGS += -I$(realpath $(BDIR)/tpm2)
dirs-y += $(BDIR)/dcrypto
dirs-y += $(BDIR)/tpm2

# Objects that we need to build
board-y =  board.o
board-y += ap_state.o
board-y += closed_source_set1.o
board-y += ec_state.o
board-y += int_ap_extension.o
board-y += power_button.o
board-y += servo_state.o
board-y += ap_uart_state.o
board-y += factory_mode.o
board-${CONFIG_RDD} += rdd.o
board-${CONFIG_USB_SPI_V2} += usb_spi.o
board-${CONFIG_USB_I2C} += usb_i2c.o
board-y += recovery_button.o

# TODO(mruthven): add cryptoc the fips boundary
fips-y=
fips-y += fips.o
fips-y += fips_rand.o
fips-$(CONFIG_U2F) += u2f.o
fips-${CONFIG_DCRYPTO_BOARD} += dcrypto/aes.o
fips-${CONFIG_DCRYPTO_BOARD} += dcrypto/app_cipher.o
fips-${CONFIG_DCRYPTO_BOARD} += dcrypto/app_key.o
fips-${CONFIG_DCRYPTO_BOARD} += dcrypto/bn.o
fips-${CONFIG_DCRYPTO_BOARD} += dcrypto/dcrypto_bn.o
fips-${CONFIG_DCRYPTO_BOARD} += dcrypto/dcrypto_p256.o
fips-${CONFIG_DCRYPTO_BOARD} += dcrypto/compare.o
fips-${CONFIG_DCRYPTO_BOARD} += dcrypto/dcrypto_runtime.o
fips-${CONFIG_DCRYPTO_BOARD} += dcrypto/hkdf.o
fips-${CONFIG_DCRYPTO_BOARD} += dcrypto/hmac.o
fips-${CONFIG_DCRYPTO_BOARD} += dcrypto/hmac_drbg.o
fips-${CONFIG_DCRYPTO_BOARD} += dcrypto/key_ladder.o
fips-${CONFIG_DCRYPTO_BOARD} += dcrypto/p256.o
fips-${CONFIG_DCRYPTO_BOARD} += dcrypto/p256_ec.o
fips-${CONFIG_DCRYPTO_BOARD} += dcrypto/rsa.o
fips-${CONFIG_DCRYPTO_BOARD} += dcrypto/sha1.o
fips-${CONFIG_DCRYPTO_BOARD} += dcrypto/sha256.o
ifeq ($(CONFIG_UPTO_SHA512),y)
fips-${CONFIG_DCRYPTO_BOARD} += dcrypto/sha384.o
ifeq ($(CONFIG_DCRYPTO_SHA512),y)
fips-${CONFIG_DCRYPTO_BOARD} += dcrypto/dcrypto_sha512.o
else
fips-${CONFIG_DCRYPTO_BOARD} += dcrypto/sha512.o
endif
endif
fips-${CONFIG_DCRYPTO_BOARD} += dcrypto/x509.o
fips-${CONFIG_DCRYPTO_BOARD} += dcrypto/trng.o
fips-${CONFIG_FIPS_UTIL} += dcrypto/util.o

custom-board-ro_objs-${CONFIG_FIPS_UTIL} = $(BDIR)/dcrypto/util.o

board-y += tpm2/NVMem.o
board-y += tpm2/aes.o
board-y += tpm2/ecc.o
board-y += tpm2/endorsement.o
board-y += tpm2/hash.o
board-y += tpm2/hash_data.o
board-y += tpm2/hkdf.o
board-y += tpm2/manufacture.o
board-y += tpm2/nvmem_ops.o
board-y += tpm2/platform.o
board-y += tpm2/rsa.o
board-y += tpm2/stubs.o
board-y += tpm2/tpm_mode.o
board-y += tpm2/tpm_state.o
board-y += tpm2/trng.o
board-y += tpm2/virtual_nvmem.o
board-y += tpm_nvmem_ops.o
board-y += wp.o

ifneq ($(H1_RED_BOARD),)
CPPFLAGS += -DH1_RED_BOARD=$(EMPTY)
endif

# Build fips code separately
ifneq ($(fips-y),)
RW_BD_OUT=$(out)/RW/$(BDIR)
FIPS_MODULE=dcrypto/fips_module.o
FIPS_LD_SCRIPT=$(BDIR)/dcrypto/fips_module.ld
RW_FIPS_OBJS=$(patsubst %.o, $(RW_BD_OUT)/%.o, $(fips-y))

$(RW_BD_OUT)/$(FIPS_MODULE): $(RW_FIPS_OBJS)
	@echo "  LD      $(notdir $@)"
	$(Q)$(CC) $(CFLAGS) --static -Wl,--relocatable\
		-Wl,-T $(FIPS_LD_SCRIPT) -Wl,-Map=$@.map -o $@ $^
	$(Q)$(OBJDUMP) -th $@ > $@.sym

board-y+= $(FIPS_MODULE)
endif

# Build and link with an external library
EXTLIB := $(realpath ../../third_party/tpm2)
CFLAGS += -I$(EXTLIB)

# For the benefit of the tpm2 library.
INCLUDE_ROOT := $(abspath ./include)
CFLAGS += -I$(INCLUDE_ROOT)
CPPFLAGS += -I$(abspath ./builtin)
CPPFLAGS += -I$(abspath ./chip/$(CHIP))
# For core includes
CPPFLAGS += -I$(abspath .)
CPPFLAGS += -I$(abspath $(BDIR))
CPPFLAGS += -I$(abspath ./fuzz)
CPPFLAGS += -I$(abspath ./test)
ifeq ($(CONFIG_UPTO_SHA512),y)
CPPFLAGS += -DSHA512_SUPPORT
endif

# Make sure the context of the software sha512 implementation fits. If it ever
# increases, a compile time assert will fire in tpm2/hash.c.
ifeq ($(CONFIG_UPTO_SHA512),y)
CFLAGS += -DUSER_MIN_HASH_STATE_SIZE=208
else
CFLAGS += -DUSER_MIN_HASH_STATE_SIZE=112
endif
# Configure TPM2 headers accordingly.
CFLAGS += -DEMBEDDED_MODE=1
# Configure cryptoc headers to handle unaligned accesses.
CFLAGS += -DSUPPORT_UNALIGNED=1

# Use absolute path as the destination to ensure that TPM2 makefile finds the
# place for output.
outdir := $(realpath $(out))/tpm2
cmd_tpm2_base = $(MAKE) obj=$(outdir) EMBEDDED_MODE=1 \
		-C $(EXTLIB) --no-print-directory

TPM2_OBJS := $(shell $(cmd_tpm2_base) list_copied_objs)

TPM2_TARGET := $(outdir)/.copied_objs

# Add dependencies on that library
$(out)/RW/ec.RW.elf $(out)/RW/ec.RW_B.elf: LDFLAGS_EXTRA += $(TPM2_OBJS)
$(out)/RW/ec.RW.elf $(out)/RW/ec.RW_B.elf: $(TPM2_TARGET)

cmd_tpm2lib = $(cmd_tpm2_base) $(TPM2_TARGET)

tpm2lib_check_clean = $(cmd_tpm2lib) -q && echo clean

ifneq ($(shell $(tpm2lib_check_clean)),clean)
# Force the external build only if it is needed.
.PHONY: $(TPM2_TARGET)
endif

$(TPM2_TARGET):
	$(call quiet,tpm2lib,TPM2   )

endif   # BOARD_MK_INCLUDED_ONCE is nonempty

board-$(CONFIG_PINWEAVER)+=pinweaver_tpm_imports.o
