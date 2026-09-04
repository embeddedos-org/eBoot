# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EoS Project

"""Tests for imgpack.py version encoding.

The bootloader compares image_version as a plain uint32 for anti-rollback
(eos_image_check_version / eos_image_check_rollback), using the layout from
EOS_VERSION_MAKE in include/eos_types.h: 8-bit major, 8-bit minor, 16-bit
patch. A component that does not fit its field must be rejected: silently
masking or carrying it produces a header that misorders against other images
(e.g. '1.256.0' used to encode identically to '2.0.0', and '1.0.65536'
identically to '1.0.0' — numerically *older* than 1.0.1).
"""

import struct
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
TOOLS = REPO_ROOT / "tools"

IMAGE_VERSION_OFFSET = 20


def _pack(tmp_path, version):
    (tmp_path / "fw.bin").write_bytes(b"\x5A" * 256)
    return subprocess.run(
        [sys.executable, str(TOOLS / "imgpack.py"),
         "--input", str(tmp_path / "fw.bin"),
         "--output", str(tmp_path / "fw.eimg"),
         "--load-addr", "0x08010000", "--entry-addr", "0x08010100",
         "--version", version],
        capture_output=True, text=True)


def test_in_range_version_encodes_correctly(tmp_path):
    r = _pack(tmp_path, "1.2.3")
    assert r.returncode == 0, r.stderr
    data = (tmp_path / "fw.eimg").read_bytes()
    encoded = struct.unpack_from("<I", data, IMAGE_VERSION_OFFSET)[0]
    assert encoded == (1 << 24) | (2 << 16) | 3


def test_max_components_encode_correctly(tmp_path):
    r = _pack(tmp_path, "255.255.65535")
    assert r.returncode == 0, r.stderr
    data = (tmp_path / "fw.eimg").read_bytes()
    encoded = struct.unpack_from("<I", data, IMAGE_VERSION_OFFSET)[0]
    assert encoded == 0xFFFFFFFF


@pytest.mark.parametrize("version", [
    "256.0.0",      # major does not fit 8 bits
    "1.256.0",      # minor carries into major: used to equal 2.0.0
    "1.0.65536",    # patch truncated to 0: used to encode older than 1.0.1
    "-1.0.0",       # negative major
    "1.0.-1",       # negative patch: used to encode as patch 65535
])
def test_out_of_range_version_is_rejected(tmp_path, version):
    r = _pack(tmp_path, version)
    assert r.returncode != 0, (
        f"version '{version}' was accepted:\n{r.stdout}")
    assert not (tmp_path / "fw.eimg").exists(), (
        f"output image was written for rejected version '{version}'")
    assert "out of range" in r.stderr
