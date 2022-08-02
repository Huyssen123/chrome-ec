/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_SHIMMED_TASKS_H
#define __CROS_EC_SHIMMED_TASKS_H

#ifdef CONFIG_HAS_TASK_CHARGER
#define HAS_TASK_CHARGER 1
#endif /* CONFIG_HAS_TASK_CHARGER */

#ifdef CONFIG_HAS_TASK_CHG_RAMP
#define HAS_TASK_CHG_RAMP 1
#endif /* CONFIG_HAS_TASK_CHG_RAMP */

#ifdef CONFIG_HAS_TASK_CHIPSET
#define HAS_TASK_CHIPSET 1
#endif /* CONFIG_HAS_TASK_CHIPSET */

#ifdef CONFIG_HAS_TASK_DPS
#define HAS_TASK_DPS 1
#endif /* CONFIG_HAS_TASK_DPS */

#ifdef CONFIG_HAS_TASK_HOSTCMD
#define HAS_TASK_HOSTCMD 1
#define CONFIG_HOSTCMD_EVENTS
#endif /* CONFIG_HAS_TASK_HOSTCMD */

#ifdef CONFIG_HAS_TASK_KEYSCAN
#define HAS_TASK_KEYSCAN 1
#endif /* CONFIG_HAS_TASK_KEYSCAN */

#ifdef CONFIG_HAS_TASK_KEYPROTO
#define HAS_TASK_KEYPROTO 1
#endif /* CONFIG_HAS_TASK_KEYPROTO */

#ifdef CONFIG_HAS_TASK_MOTIONSENSE
#define HAS_TASK_MOTIONSENSE 1
#endif /* CONFIG_HAS_TASK_MOTIONSENSE */

#ifdef CONFIG_HAS_TASK_POWERBTN
#define HAS_TASK_POWERBTN 1
#endif /* CONFIG_HAS_TASK_POWERBTN */

#ifdef CONFIG_PLATFORM_EC_USB_MUX_TASK
#define HAS_TASK_USB_MUX 1
#endif /* CONFIG_PLATFORM_EC_USB_MUX_TASK */

#endif /* __CROS_EC_SHIMMED_TASKS_H */
