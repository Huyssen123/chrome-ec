/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CEC_CHIP_H
#define __CROS_EC_CEC_CHIP_H

/* APB1 frequency. Store divided by 10k to avoid some runtime divisions */
extern uint32_t apb1_freq_div_10k;

/* Time in us to timer clock ticks */
#define CEC_US_TO_TICKS(t) ((t)*apb1_freq_div_10k / 100)
#if DEBUG_CEC
/* Timer clock ticks to us */
#define CEC_TICKS_TO_US(ticks) (100 * (ticks) / apb1_freq_div_10k)
#endif

#endif /* __CROS_EC_CEC_CHIP_H */
