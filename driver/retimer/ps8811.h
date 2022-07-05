/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PS8811 retimer.
 */

#include "usb_mux.h"

#ifndef __CROS_EC_USB_RETIMER_PS8811_H
#define __CROS_EC_USB_RETIMER_PS8811_H

/*
 * PS8811 uses 7-bit I2C addresses 0x28 to 0x29 (ADDR=LL).
 * Page 0 = 0x28, Page 1 = 0x29.
 * PS8811 uses 7-bit I2C addresses 0x2A to 0x2B (ADDR=LH).
 * Page 0 = 0x2A, Page 1 = 0x2B.
 * PS8811 uses 7-bit I2C addresses 0x70 to 0x71 (ADDR=HL).
 * Page 0 = 0x70, Page 1 = 0x71.
 * PS8811 uses 7-bit I2C addresses 0x72 to 0x73 (ADDR=HH).
 * Page 0 = 0x72, Page 1 = 0x73.
 */
#define PS8811_I2C_ADDR_FLAGS0 0x28
#define PS8811_I2C_ADDR_FLAGS1 0x2A
#define PS8811_I2C_ADDR_FLAGS2 0x70
#define PS8811_I2C_ADDR_FLAGS3 0x72

/*
 * PAGE 1 Register Definitions
 */
#define PS8811_REG_PAGE1 0x01

#define PS8811_REG1_USB_AEQ_LEVEL 0x01
#define PS8811_AEQ_PIN_LEVEL_UP_CONFIG_MASK GENMASK(3, 0)
#define PS8811_AEQ_PIN_LEVEL_UP_SHIFT 0
#define PS8811_AEQ_PIN_LEVEL_UP_9DB 0x00
#define PS8811_AEQ_PIN_LEVEL_UP_10P5DB 0x01
#define PS8811_AEQ_PIN_LEVEL_UP_12DB 0x02
#define PS8811_AEQ_PIN_LEVEL_UP_13DB 0x03
#define PS8811_AEQ_PIN_LEVEL_UP_16DB 0x04
#define PS8811_AEQ_PIN_LEVEL_UP_17DB 0x05
#define PS8811_AEQ_PIN_LEVEL_UP_18DB 0x06
#define PS8811_AEQ_PIN_LEVEL_UP_19DB 0x07
#define PS8811_AEQ_PIN_LEVEL_UP_20DB 0x08
#define PS8811_AEQ_PIN_LEVEL_UP_21DB 0x09
#define PS8811_AEQ_PIN_LEVEL_UP_23DB 0x0A
#define PS8811_AEQ_I2C_LEVEL_UP_CONFIG_MASK GENMASK(7, 4)
#define PS8811_AEQ_I2C_LEVEL_UP_SHIFT 4
#define PS8811_AEQ_I2C_LEVEL_UP_9DB 0x00
#define PS8811_AEQ_I2C_LEVEL_UP_10P5DB 0x01
#define PS8811_AEQ_I2C_LEVEL_UP_12DB 0x02
#define PS8811_AEQ_I2C_LEVEL_UP_13DB 0x03
#define PS8811_AEQ_I2C_LEVEL_UP_16DB 0x04
#define PS8811_AEQ_I2C_LEVEL_UP_17DB 0x05
#define PS8811_AEQ_I2C_LEVEL_UP_18DB 0x06
#define PS8811_AEQ_I2C_LEVEL_UP_19DB 0x07
#define PS8811_AEQ_I2C_LEVEL_UP_20DB 0x08
#define PS8811_AEQ_I2C_LEVEL_UP_21DB 0x09
#define PS8811_AEQ_I2C_LEVEL_UP_23DB 0x0A

#define PS8811_REG1_USB_ADE_CONFIG 0x02
#define PS8811_AEQ_CONFIG_REG_ENABLE BIT(0)
#define PS8811_AEQ_ADAPTIVE_REG_ENABLE BIT(1)
#define PS8811_ADE_PIN_MID_LEVEL_CONFIG_MASK GENMASK(7, 5)
#define PS8811_ADE_PIN_MID_LEVEL_SHIFT 5
#define PS8811_ADE_PIN_MID_LEVEL_0P5DB 0x00
#define PS8811_ADE_PIN_MID_LEVEL_1P5DB 0x01
#define PS8811_ADE_PIN_MID_LEVEL_2DB 0x02
#define PS8811_ADE_PIN_MID_LEVEL_3DB 0x03
#define PS8811_ADE_PIN_MID_LEVEL_3P5DB 0x04
#define PS8811_ADE_PIN_MID_LEVEL_4P5DB 0x05
#define PS8811_ADE_PIN_MID_LEVEL_6DB 0x06
#define PS8811_ADE_PIN_MID_LEVEL_7P5DB 0x07
#define PS8811_ADE_PIN_LOW_LEVEL_CONFIG_MASK GENMASK(4, 2)
#define PS8811_ADE_PIN_LOW_LEVEL_SHIFT 2
#define PS8811_ADE_PIN_LOW_LEVEL_0P5DB 0x00
#define PS8811_ADE_PIN_LOW_LEVEL_1P5DB 0x01
#define PS8811_ADE_PIN_LOW_LEVEL_2DB 0x02
#define PS8811_ADE_PIN_LOW_LEVEL_3DB 0x03
#define PS8811_ADE_PIN_LOW_LEVEL_3P5DB 0x04
#define PS8811_ADE_PIN_LOW_LEVEL_4P5DB 0x05
#define PS8811_ADE_PIN_LOW_LEVEL_6DB 0x06
#define PS8811_ADE_PIN_LOW_LEVEL_7P5DB 0x07

#define PS8811_REG1_USB_BEQ_LEVEL 0x05
#define PS8811_BEQ_PIN_LEVEL_UP_CONFIG_MASK GENMASK(3, 0)
#define PS8811_BEQ_PIN_LEVEL_UP_SHIFT 0
#define PS8811_BEQ_PIN_LEVEL_UP_9DB 0x00
#define PS8811_BEQ_PIN_LEVEL_UP_10P5DB 0x01
#define PS8811_BEQ_PIN_LEVEL_UP_12DB 0x02
#define PS8811_BEQ_PIN_LEVEL_UP_13DB 0x03
#define PS8811_BEQ_PIN_LEVEL_UP_16DB 0x04
#define PS8811_BEQ_PIN_LEVEL_UP_17DB 0x05
#define PS8811_BEQ_PIN_LEVEL_UP_18DB 0x06
#define PS8811_BEQ_PIN_LEVEL_UP_19DB 0x07
#define PS8811_BEQ_PIN_LEVEL_UP_20DB 0x08
#define PS8811_BEQ_PIN_LEVEL_UP_21DB 0x09
#define PS8811_BEQ_PIN_LEVEL_UP_23DB 0x0A
#define PS8811_BEQ_I2C_LEVEL_UP_CONFIG_MASK GENMASK(7, 4)
#define PS8811_BEQ_I2C_LEVEL_UP_SHIFT 4
#define PS8811_BEQ_I2C_LEVEL_UP_9DB 0x00
#define PS8811_BEQ_I2C_LEVEL_UP_10P5DB 0x01
#define PS8811_BEQ_I2C_LEVEL_UP_12DB 0x02
#define PS8811_BEQ_I2C_LEVEL_UP_13DB 0x03
#define PS8811_BEQ_I2C_LEVEL_UP_16DB 0x04
#define PS8811_BEQ_I2C_LEVEL_UP_17DB 0x05
#define PS8811_BEQ_I2C_LEVEL_UP_18DB 0x06
#define PS8811_BEQ_I2C_LEVEL_UP_19DB 0x07
#define PS8811_BEQ_I2C_LEVEL_UP_20DB 0x08
#define PS8811_BEQ_I2C_LEVEL_UP_21DB 0x09
#define PS8811_BEQ_I2C_LEVEL_UP_23DB 0x0A

#define PS8811_REG1_USB_BDE_CONFIG 0x06
#define PS8811_BEQ_CONFIG_REG_ENABLE BIT(0)
#define PS8811_BEQ_ADAPTIVE_REG_ENABLE BIT(1)
#define PS8811_BDE_PIN_MID_LEVEL_CONFIG_MASK GENMASK(7, 5)
#define PS8811_BDE_PIN_MID_LEVEL_SHIFT 5
#define PS8811_BDE_PIN_MID_LEVEL_0P5DB 0x00
#define PS8811_BDE_PIN_MID_LEVEL_1P5DB 0x01
#define PS8811_BDE_PIN_MID_LEVEL_2DB 0x02
#define PS8811_BDE_PIN_MID_LEVEL_3DB 0x03
#define PS8811_BDE_PIN_MID_LEVEL_3P5DB 0x04
#define PS8811_BDE_PIN_MID_LEVEL_4P5DB 0x05
#define PS8811_BDE_PIN_MID_LEVEL_6DB 0x06
#define PS8811_BDE_PIN_MID_LEVEL_7P5DB 0x07
#define PS8811_BDE_PIN_LOW_LEVEL_CONFIG_MASK GENMASK(4, 2)
#define PS8811_BDE_PIN_LOW_LEVEL_SHIFT 2
#define PS8811_BDE_PIN_LOW_LEVEL_0P5DB 0x00
#define PS8811_BDE_PIN_LOW_LEVEL_1P5DB 0x01
#define PS8811_BDE_PIN_LOW_LEVEL_2DB 0x02
#define PS8811_BDE_PIN_LOW_LEVEL_3DB 0x03
#define PS8811_BDE_PIN_LOW_LEVEL_3P5DB 0x04
#define PS8811_BDE_PIN_LOW_LEVEL_4P5DB 0x05
#define PS8811_BDE_PIN_LOW_LEVEL_6DB 0x06
#define PS8811_BDE_PIN_LOW_LEVEL_7P5DB 0x07

#define PS8811_REG1_USB_CHAN_A_SWING 0x66
#define PS8811_CHAN_A_SWING_MASK GENMASK(6, 4)
#define PS8811_CHAN_A_SWING_SHIFT 4

#define PS8811_REG1_50OHM_ADJUST_CHAN_B 0x73
#define PS8811_50OHM_ADJUST_CHAN_B_CONFIG_MASK GENMASK(3, 1)
#define PS8811_50OHM_ADJUST_CHAN_B_SHIFT 1
#define PS8811_50OHM_ADJUST_CHAN_B_DEFAULT 0x00
#define PS8811_50OHM_ADJUST_CHAN_B_MINUS_6PCT 0x01
#define PS8811_50OHM_ADJUST_CHAN_B_MINUS_9PCT 0x02
#define PS8811_50OHM_ADJUST_CHAN_B_MINUS_14PCT 0x03
#define PS8811_50OHM_ADJUST_CHAN_B_PLUS_7PCT 0x04
#define PS8811_50OHM_ADJUST_CHAN_B_PLUS_11PCT 0x05
#define PS8811_50OHM_ADJUST_CHAN_B_PLUS_20PCT 0x06

#define PS8811_BDE_PIN_MID_LEVEL_1P5DB 0x01
#define PS8811_BDE_PIN_MID_LEVEL_2DB 0x02
#define PS8811_BDE_PIN_MID_LEVEL_3DB 0x03
#define PS8811_BDE_PIN_MID_LEVEL_3P5DB 0x04
#define PS8811_BDE_PIN_MID_LEVEL_4P5DB 0x05
#define PS8811_BDE_PIN_MID_LEVEL_6DB 0x06
#define PS8811_BDE_PIN_MID_LEVEL_7P5DB 0x07

#define PS8811_REG1_USB_CHAN_B_SWING 0xA4
#define PS8811_CHAN_B_SWING_MASK GENMASK(2, 0)
#define PS8811_CHAN_B_SWING_SHIFT 0

/* De-emphasis -2.2 dB, Pre-shoot 1.2 dB */
#define PS8811_CHAN_B_DE_2_2_PS_1_2_LSB 0x1
#define PS8811_CHAN_B_DE_2_2_PS_1_2_MSB 0x13

/* De-emphasis -3.5 dB, Pre-shoot 0 dB */
#define PS8811_CHAN_B_DE_3_5_PS_0_LSB 0x0
#define PS8811_CHAN_B_DE_3_5_PS_0_MSB 0x5

/* De-emphasis -4.5 dB, Pre-shoot 0 dB */
#define PS8811_CHAN_B_DE_4_5_PS_0_LSB 0x0
#define PS8811_CHAN_B_DE_4_5_PS_0_MSB 0x6

/* De-emphasis -6 dB, Pre-shoot 1.5 dB */
#define PS8811_CHAN_B_DE_6_PS_1_5_LSB 0x2
#define PS8811_CHAN_B_DE_6_PS_1_5_MSB 0x16

/* De-emphasis -6 dB, Pre-shoot 3 dB */
#define PS8811_CHAN_B_DE_6_PS_3_LSB 0x4
#define PS8811_CHAN_B_DE_6_PS_3_MSB 0x16

#define PS8811_REG1_USB_CHAN_B_DE_PS_LSB 0xA5
#define PS8811_CHAN_B_DE_PS_LSB_MASK GENMASK(2, 0)

#define PS8811_REG1_USB_CHAN_B_DE_PS_MSB 0xA6
#define PS8811_CHAN_B_DE_PS_MSB_MASK GENMASK(5, 0)

int ps8811_i2c_read(const struct usb_mux *me, int page, int offset, int *data);
int ps8811_i2c_write(const struct usb_mux *me, int page, int offset, int data);
int ps8811_i2c_field_update(const struct usb_mux *me, int page, int offset,
			    uint8_t field_mask, uint8_t set_value);

#endif /* __CROS_EC_USB_RETIMER_PS8802_H */
