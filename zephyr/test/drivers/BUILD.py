# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Register zmake project for drivers test."""

register_host_test(
    "drivers",
    dts_overlays=[
        "overlay.dts",
        here / "led_driver/led_pins.dts",
        here / "led_driver/led_policy.dts",
    ],
    kconfig_files=[
        here / "led_driver/prj.conf",
    ],
    test_args=["-flash={test_temp_dir}/flash.bin"],
)
