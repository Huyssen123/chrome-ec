#!/usr/bin/env vpython3

# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
This script is a wrapper for invoking Twister, the Zephyr test runner, using
default parameters for the ChromiumOS EC project. For an overview of CLI
parameters that may be used, please consult the Twister documentation.
"""

# [VPYTHON:BEGIN]
# python_version: "3.8"
# wheel: <
#   name: "infra/python/wheels/anytree-py2_py3"
#   version: "version:2.8.0"
# >
# wheel: <
#   name: "infra/python/wheels/colorama-py3"
#   version: "version:0.4.1"
# >
# wheel: <
#   name: "infra/python/wheels/docopt-py2_py3"
#   version: "version:0.6.2"
# >
# wheel: <
#   name: "infra/python/wheels/packaging-py2_py3"
#   version: "version:16.8"
# >
# wheel: <
#   name: "infra/python/wheels/ply-py2_py3"
#   version: "version:3.11"
# >
# wheel: <
#   name: "infra/python/wheels/psutil/${vpython_platform}"
#   version: "version:5.8.0.chromium.3"
# >
# wheel: <
#   name: "infra/python/wheels/pyelftools-py2_py3"
#   version: "version:0.29"
# >
# wheel: <
#   name: "infra/python/wheels/pykwalify-py2_py3"
#   version: "version:1.8.0"
# >
# wheel: <
#   name: "infra/python/wheels/pyparsing-py3"
#   version: "version:3.0.7"
# >
# wheel: <
#   name: "infra/python/wheels/pyserial-py2_py3"
#   version: "version:3.4"
# >
# wheel: <
#   name: "infra/python/wheels/python-dateutil-py2_py3"
#   version: "version:2.8.1"
# >
# wheel: <
#   name: "infra/python/wheels/pyyaml-py3"
#   version: "version:5.3.1"
# >
# wheel: <
#   name: "infra/python/wheels/ruamel_yaml_clib/${vpython_platform}"
#   version: "version:0.2.6"
# >
# wheel: <
#   name: "infra/python/wheels/ruamel_yaml-py3"
#   version: "version:0.17.16"
# >
# wheel: <
#   name: "infra/python/wheels/six-py2_py3"
#   version: "version:1.16.0"
# >
# wheel: <
#   name: "infra/python/wheels/west-py3"
#   version: "version:0.14.0"
# >
# [VPYTHON:END]

import argparse
import json
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys
import time
from pathlib import Path
from shutil import which


def find_checkout() -> Path:
    """Find the location of the source checkout or return None."""
    cros_checkout = os.environ.get("CROS_WORKON_SRCROOT")
    if cros_checkout is not None:
        return Path(cros_checkout)

    # Attempt to locate checkout location relatively if being run outside of
    # chroot.
    try:
        cros_checkout = Path(__file__).resolve().parents[4]
        assert (cros_checkout / "src").exists()
        return cros_checkout
    except (IndexError, AssertionError):
        # Not in the chroot or matching directory structure
        return None


def find_paths():
    """Find EC base, Zephyr base, and Zephyr modules paths and return as a 3-tuple."""

    # Determine where the source tree is checked out. Will be None if operating outside
    # of the chroot (e.g. Gitlab builds). In this case, additional paths need to be
    # passed in through environment variables.
    cros_checkout = find_checkout()

    if cros_checkout:
        ec_base = cros_checkout / "src" / "platform" / "ec"
        zephyr_base = cros_checkout / "src" / "third_party" / "zephyr" / "main"
        zephyr_modules_dir = cros_checkout / "src" / "third_party" / "zephyr"
    else:
        try:
            ec_base = Path(os.environ["EC_DIR"]).resolve()
        except KeyError as err:
            raise RuntimeError(
                "EC_DIR unspecified. Please pass as env var or use chroot."
            ) from err

        try:
            zephyr_base = Path(os.environ["ZEPHYR_BASE"]).resolve()
        except KeyError as err:
            raise RuntimeError(
                "ZEPHYR_BASE unspecified. Please pass as env var or use chroot."
            ) from err

        try:
            zephyr_modules_dir = Path(os.environ["MODULES_DIR"]).resolve()
        except KeyError as err:
            raise RuntimeError(
                "MODULES_DIR unspecified. Please pass as env var or use chroot."
            ) from err

    return (ec_base, zephyr_base, zephyr_modules_dir)


def find_modules(mod_dir: Path) -> list:
    """Find Zephyr modules in the given directory `dir`."""

    modules = []
    for child in mod_dir.iterdir():
        if child.is_dir() and (child / "zephyr" / "module.yml").exists():
            modules.append(child.resolve())
    return modules


def is_tool(name):
    """Check if 'name' is on PATH and marked executable."""
    return which(name) is not None


def is_rdb_login():
    """Checks if user is logged into rdb"""
    cmd = ["rdb", "auth-info"]
    ret = subprocess.run(cmd, capture_output=True, text=True, check=False)

    if ret.returncode == 0:
        print("\nrdb auth-info: " + ret.stdout.split("\n")[0])
    else:
        print("\nrdb auth-info: " + ret.stderr)

    return ret.returncode == 0


def upload_results(ec_base, outdir):
    """Uploads Zephyr Test results to ResultDB"""
    flag = False

    if is_rdb_login():
        json_path = pathlib.Path(outdir) / "twister.json"

        if json_path.exists():
            cmd = [
                "rdb",
                "stream",
                "-new",
                "-realm",
                "chromium:public",
                "--",
                "vpython3",
                str(ec_base / "util/zephyr_to_resultdb.py"),
                "--result=" + str(json_path),
                "--upload=True",
            ]

            start_time = time.time()
            ret = subprocess.run(
                cmd, capture_output=True, text=True, check=True
            )
            end_time = time.time()

            # Extract URL to test report from captured output
            rdb_url = re.search(
                r"(?P<url>https?://[^\s]+)", ret.stderr.split("\n")[0]
            ).group("url")
            print(f"\nTEST RESULTS ({end_time - start_time:.3f}s): {rdb_url}\n")
            flag = ret.returncode == 0
    else:
        print("Unable to upload test results, please run 'rdb auth-login'\n")

    return flag


def check_for_skipped_tests(outdir):
    """Checks Twister json test report for skipped tests"""
    found_skipped = False
    json_path = pathlib.Path(outdir) / "twister.json"
    if json_path.exists():
        with open(json_path) as file:
            data = json.load(file)

            for testsuite in data["testsuites"]:
                for testcase in testsuite["testcases"]:
                    if testcase["status"] == "skipped":
                        tc_name = testcase["identifier"]
                        print(f"TEST SKIPPED: {tc_name}")
                        found_skipped = True

            file.close()

    return found_skipped


def append_cmake_compiler(cmdline, cmake_var, exe_options):
    """Picks the first available exe from exe_options and adds a cmake variable
    to cmdline."""
    for exe in exe_options:
        exe_path = shutil.which(exe)
        if exe_path:
            cmdline.append(f"-x={cmake_var}={exe_path}")
            return


def main():
    """Run Twister using defaults for the EC project."""

    # Get paths for the build.
    ec_base, zephyr_base, zephyr_modules_dir = find_paths()

    zephyr_modules = find_modules(zephyr_modules_dir)

    # Add the EC dir as a module if not already included (resolve all paths to
    # account for symlinked or relative paths)
    if ec_base.resolve() not in zephyr_modules:
        zephyr_modules.append(ec_base)

    # Twister CLI args
    # TODO(b/239165779): Reduce or remove the usage of label properties
    # Zephyr upstream has deprecated the label property. We need to allow
    # warnings during twister runs until all the label properties are removed
    # from all board and test overlays.
    twister_cli = [
        sys.executable,
        str(zephyr_base / "scripts" / "twister"),  # Executable path
        "--ninja",
        "--disable-warnings-as-errors",
        f"-x=DTS_ROOT={str( ec_base / 'zephyr')}",
        f"-x=SYSCALL_INCLUDE_DIRS={str(ec_base / 'zephyr' / 'include' / 'drivers')}",
        f"-x=ZEPHYR_BASE={zephyr_base}",
        f"-x=ZEPHYR_MODULES={';'.join([str(p) for p in zephyr_modules])}",
    ]
    is_in_chroot = Path("/etc/cros_chroot_version").is_file()

    # `-T` flags (used for specifying test directories to build and run)
    # require special handling. When run without `-T` flags, Twister will
    # search for tests in `zephyr_base`. This is undesirable and we want
    # Twister to look in the EC tree by default, instead. Use argparse to
    # intercept `-T` flags and pass in a new default if none are found. If
    # user does pass their own `-T` flags, pass them through instead. Do the
    # same with verbosity. Other arguments get passed straight through,
    # including -h/--help so that Twister's own help text gets displayed.
    parser = argparse.ArgumentParser(add_help=False, allow_abbrev=False)
    parser.add_argument("-T", "--testsuite-root", action="append")
    parser.add_argument("-p", "--platform", action="append")
    parser.add_argument("-v", "--verbose", action="count", default=0)
    parser.add_argument("--gcov-tool")
    parser.add_argument(
        "--no-upload-cros-rdb", dest="upload_cros_rdb", action="store_false"
    )
    parser.add_argument(
        "-O",
        "--outdir",
        default=os.path.join(os.getcwd(), "twister-out"),
    )
    parser.add_argument(
        "--toolchain",
        default=os.environ.get(
            "ZEPHYR_TOOLCHAIN_VARIANT",
            "llvm" if is_in_chroot else "host",
        ),
    )
    parser.add_argument(
        "--gcc", dest="toolchain", action="store_const", const="host"
    )
    parser.add_argument(
        "--llvm",
        "--clang",
        dest="toolchain",
        action="store_const",
        const="llvm",
    )

    intercepted_args, other_args = parser.parse_known_args()

    for _ in range(intercepted_args.verbose):
        # Pass verbosity setting through to twister
        twister_cli.append("-v")

    if intercepted_args.testsuite_root:
        # Pass user-provided -T args when present.
        for arg in intercepted_args.testsuite_root:
            twister_cli.extend(["-T", arg])
    else:
        # Use this set of test suite roots when no -T args are present. This
        # should encompass all current Zephyr EC tests. The paths are meant to
        # be as specific as possible to limit Twister's search scope. This saves
        # significant time when starting Twister.
        twister_cli.extend(["-T", str(ec_base / "common")])
        twister_cli.extend(["-T", str(ec_base / "zephyr/test")])
        twister_cli.extend(["-T", str(zephyr_base / "tests/subsys/shell")])

    if intercepted_args.platform:
        # Pass user-provided -p args when present.
        for arg in intercepted_args.platform:
            twister_cli.extend(["-p", arg])
    else:
        # posix_native and unit_testing when nothing was requested by user.
        twister_cli.extend(["-p", "native_posix"])
        twister_cli.extend(["-p", "unit_testing"])

    twister_cli.extend(["--outdir", intercepted_args.outdir])

    # Prepare environment variables for export to Twister. Inherit the parent
    # process's environment, but set some default values if not already set.
    twister_env = dict(os.environ)
    extra_env_vars = {
        "TOOLCHAIN_ROOT": os.environ.get(
            "TOOLCHAIN_ROOT",
            str(ec_base / "zephyr") if is_in_chroot else str(zephyr_base),
        ),
        "ZEPHYR_TOOLCHAIN_VARIANT": intercepted_args.toolchain,
    }
    gcov_tool = None
    if intercepted_args.toolchain == "host":
        gcov_tool = "gcov"
    elif intercepted_args.toolchain == "llvm":
        gcov_tool = str(ec_base / "util" / "llvm-gcov.sh")
    else:
        print("Unknown toolchain specified:", intercepted_args.toolchain)
    if intercepted_args.gcov_tool:
        gcov_tool = intercepted_args.gcov_tool
    if gcov_tool:
        twister_cli.extend(["--gcov-tool", gcov_tool])

    twister_env.update(extra_env_vars)

    # Append additional user-supplied args
    twister_cli.extend(other_args)

    # Print exact CLI args and environment variables depending on verbosity.
    if intercepted_args.verbose > 0:
        print("Calling:", " ".join(shlex.quote(str(x)) for x in twister_cli))
        print(
            "With environment overrides:",
            " ".join(
                f"{name}={shlex.quote(val)}"
                for name, val in extra_env_vars.items()
            ),
        )
        sys.stdout.flush()

    # Invoke Twister and wait for it to exit.
    result = subprocess.run(
        twister_cli,
        env=twister_env,
        check=False,
        close_fds=False,  # For GNUMakefile jobserver
    )

    if check_for_skipped_tests(intercepted_args.outdir):
        result.returncode = 1

    if result.returncode == 0:
        print("TEST EXECUTION SUCCESSFUL")
    else:
        print("TEST EXECUTION FAILED")

    if is_tool("rdb") and intercepted_args.upload_cros_rdb:
        upload_results(ec_base, intercepted_args.outdir)

    sys.exit(result.returncode)


if __name__ == "__main__":
    main()
