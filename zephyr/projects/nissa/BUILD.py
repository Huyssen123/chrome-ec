# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for nissa."""

# Nivviks and Craask has NPCX993F, Nereid has ITE81302


def register_nissa_project(
    project_name,
    chip="it8xxx2",
    extra_dts_overlays=(),
    extra_kconfig_files=(),
):
    """Register a variant of nissa."""
    register_func = register_binman_project
    if chip.startswith("npcx"):
        register_func = register_npcx_project

    return register_func(
        project_name=project_name,
        zephyr_board=chip,
        dts_overlays=["cbi.dts", *extra_dts_overlays],
        kconfig_files=[here / "prj.conf", *extra_kconfig_files],
    )


nivviks = register_nissa_project(
    project_name="nivviks",
    chip="npcx9m3f",
    extra_dts_overlays=[
        here / "nivviks_generated.dts",
        here / "nivviks_cbi.dts",
        here / "nivviks_overlay.dts",
        here / "nivviks_motionsense.dts",
        here / "nivviks_keyboard.dts",
        here / "nivviks_power_signals.dts",
        here / "nivviks_pwm_leds.dts",
    ],
    extra_kconfig_files=[here / "prj_nivviks.conf"],
)

nereid = register_nissa_project(
    project_name="nereid",
    chip="it8xxx2",
    extra_dts_overlays=[
        here / "nereid_generated.dts",
        here / "nereid_overlay.dts",
        here / "nereid_motionsense.dts",
        here / "nereid_keyboard.dts",
        here / "nereid_power_signals.dts",
        here / "nereid_pwm_leds.dts",
    ],
    extra_kconfig_files=[here / "prj_nereid.conf"],
)

craask = register_nissa_project(
    project_name="craask",
    chip="npcx9m3f",
    extra_dts_overlays=[
        here / "craask_generated.dts",
        here / "craask_overlay.dts",
        here / "craask_motionsense.dts",
        here / "craask_power_signals.dts",
        here / "craask_pwm_leds.dts",
    ],
    extra_kconfig_files=[here / "prj_craask.conf"],
)
