# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EoS Project

"""The configure-time gate on anti-rollback without a monotonic counter.

eos_rollback_verify() fails closed when the board has no monotonic counter
-- which today is every board under boards/, none of which implements
monotonic_read. The permissive behaviour survives only as the explicit
opt-in EBLDR_ALLOW_NO_ROLLBACK_COUNTER, for builds with no counter by
design (host tests, sim, bring-up). These tests run real CMake configures
against the repository and pin what the gate does:

  * a release-shaped build of a real board refuses to configure with the
    opt-in ON, and says why, before anything else goes wrong;
  * a Debug build of a real board, and a host build (board "none"), may
    select it;
  * without the opt-in, a release-shaped build of a real board configures
    -- the fail-closed is at runtime, not configure time;
  * the opt-in defaults to OFF.

Configure only; nothing is compiled.
"""

import re
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]

GATE_MESSAGE = "EBLDR_ALLOW_NO_ROLLBACK_COUNTER=ON"

pytestmark = pytest.mark.skipif(
    shutil.which("cmake") is None, reason="cmake is not on PATH")


def configure(*defs):
    build = Path(tempfile.mkdtemp(prefix="eboot-rollback-gate-"))
    try:
        result = subprocess.run(
            ["cmake", "-S", str(REPO_ROOT), "-B", str(build), *defs],
            capture_output=True, text=True)
        cache = build / "CMakeCache.txt"
        cache_text = cache.read_text() if cache.exists() else ""
        return result, cache_text
    finally:
        shutil.rmtree(build, ignore_errors=True)


def first_error(stderr):
    """The first 'CMake Error' block, so ordering can be asserted."""
    blocks = re.split(r"(?=CMake Error)", stderr)
    return next((b for b in blocks if b.startswith("CMake Error")), "")


# Every build type that is not Debug is release-shaped: the optimised types
# under every spelling CMake accepts, and no build type at all -- a cross
# build still gets -Os from CMakeLists.txt, and a multi-config generator has
# no type at configure time. Same list as test_production_key_gate.py.
RELEASE_SHAPED = ["Release", "RelWithDebInfo", "MinSizeRel",
                  "release", "RELEASE", None]


@pytest.mark.parametrize("build_type", RELEASE_SHAPED,
                         ids=[t or "unset" for t in RELEASE_SHAPED])
def test_release_shaped_board_build_rejects_no_counter_opt_in(build_type):
    # EBLDR_ALLOW_DEV_KEY=ON satisfies the production-key gate, so the
    # rollback gate is the one that stops the configure.
    defs = ["-DEBLDR_BOARD=stm32f4", "-DEBLDR_ALLOW_DEV_KEY=ON",
            "-DEBLDR_ALLOW_NO_ROLLBACK_COUNTER=ON"]
    if build_type is not None:
        defs.append("-DCMAKE_BUILD_TYPE=" + build_type)
    result, _ = configure(*defs)
    assert result.returncode != 0
    assert GATE_MESSAGE in result.stderr
    # The gate, not a missing toolchain or a board port, is what stops it.
    assert GATE_MESSAGE in first_error(result.stderr), result.stderr
    # CMake wraps the message, so the phrase may break across lines.
    assert "Debug build is exempt" in result.stderr


@pytest.mark.parametrize("build_type", ["Debug", "debug", "DEBUG"])
def test_debug_board_build_is_the_one_exemption(build_type):
    result, _ = configure("-DCMAKE_BUILD_TYPE=" + build_type,
                          "-DEBLDR_BOARD=stm32f4",
                          "-DEBLDR_ALLOW_NO_ROLLBACK_COUNTER=ON")
    assert result.returncode == 0, result.stderr
    assert GATE_MESSAGE not in result.stderr


def test_host_build_is_not_gated():
    # No board, so nothing reaches a device: no counter, no opt-out, no
    # gate message. This is the build the host unit tests use.
    result, _ = configure("-DCMAKE_BUILD_TYPE=Release",
                          "-DEBLDR_ALLOW_NO_ROLLBACK_COUNTER=ON")
    assert result.returncode == 0, result.stderr
    assert GATE_MESSAGE not in result.stderr


def test_release_board_build_without_opt_in_configures():
    # The fail-closed is at runtime (verify refuses), not at configure
    # time: a release-shaped build of a real board without the opt-in still
    # configures. Only the gate message's absence is asserted, as in
    # test_production_key_gate.py -- a full cross configure may need a
    # toolchain this host does not have.
    result, _ = configure("-DCMAKE_BUILD_TYPE=Release",
                          "-DEBLDR_BOARD=stm32f4",
                          "-DEBLDR_ALLOW_DEV_KEY=ON")
    assert GATE_MESSAGE not in result.stderr


def test_opt_in_defaults_off():
    result, cache = configure("-DEBLDR_BOARD=none")
    assert result.returncode == 0, result.stderr
    assert "EBLDR_ALLOW_NO_ROLLBACK_COUNTER:BOOL=OFF" in cache
