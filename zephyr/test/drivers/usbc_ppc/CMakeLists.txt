# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Add source files
target_sources(app PRIVATE
  src/usbc_ppc.c
  src/ppc_shim.c
)