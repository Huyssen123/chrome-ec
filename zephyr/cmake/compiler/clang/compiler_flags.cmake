# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

include("${ZEPHYR_BASE}/cmake/compiler/clang/compiler_flags.cmake")

# Disable -fno-freestanding.
set_compiler_property(PROPERTY hosted)

# Disable position independent code.
if ("${ARCH}" STREQUAL "posix" OR "${ARCH}" STREQUAL "unit_testing")
	add_compile_options(-fPIC)
else()
	add_compile_options(-fno-PIC)
endif()

# When testing, look for stack smashing
add_compile_option_ifdef(CONFIG_ZTEST -fstack-protector-all)

check_set_compiler_property(APPEND PROPERTY warning_extended -Wunused-variable
	-Werror=unused-variable -Werror=missing-braces
	-Werror=sometimes-uninitialized -Werror=unused-function
	-Werror=array-bounds)

# clang flags for coverage generation
set_property(TARGET compiler PROPERTY coverage --coverage -fno-inline)
