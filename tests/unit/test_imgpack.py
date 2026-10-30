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

import os
import stat
import struct
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
TOOLS = REPO_ROOT / "tools"

IMAGE_SIZE_OFFSET = 8
IMAGE_VERSION_OFFSET = 20
MAX_IMAGE_SIZE = 16 * 1024 * 1024  # core/image_verify.c

posix_only = pytest.mark.skipif(
    os.name != "posix", reason="needs POSIX symlinks and permission bits")


def _pack(
    tmp_path,
    version,
    *,
    load_addr="0x08010000",
    entry_addr="0x08010010",
    firmware=b"\x5A" * 256,
):
    (tmp_path / "fw.bin").write_bytes(firmware)
    return subprocess.run(
        [sys.executable, str(TOOLS / "imgpack.py"),
         "--input", str(tmp_path / "fw.bin"),
         "--output", str(tmp_path / "fw.eimg"),
         "--load-addr", load_addr, "--entry-addr", entry_addr,
         # `--version=X`, not `--version X`: a value such as "-1.0.0" is a
         # separate token in the second form, and whether argparse reads it
         # as a value or as an unknown option depends on the interpreter --
         # 3.10 refuses it with "expected one argument" before imgpack.py
         # runs, 3.13+ accepts it. The test is about imgpack's own check,
         # which only the joined form reaches on every version.
         f"--version={version}"],
        capture_output=True, text=True)


def _pack_paths(input_path, output_path):
    return subprocess.run(
        [sys.executable, str(TOOLS / "imgpack.py"),
         "--input", str(input_path), "--output", str(output_path),
         "--version=1.2.3", "--load-addr", "0x08010000",
         "--entry-addr", "0x08010010"],
        capture_output=True, text=True,
    )


def _leftover_temporaries(directory):
    return sorted(p.name for p in directory.iterdir()
                  if p.name.startswith(".") and p.name.endswith(".tmp"))


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


@pytest.mark.parametrize(
    ("load_addr", "entry_addr", "reason"),
    [
        ("0x08010000", "0x08010100", "outside the image range"),  # exclusive image end
        ("0x08010000", "0x0800FFFF", "outside the image range"),  # before the image
        ("0x08010000", "0x100000000", "entry address"),  # entry does not fit uint32
        # Only the load address is wrong here, so the rejection can come from
        # nothing else.
        ("0x100000000", "0x08010010", "load address"),  # load does not fit uint32
        ("0xFFFFFFF0", "0xFFFFFFF0", "overflows uint32"),  # load + 256-byte image wraps
    ],
)
def test_invalid_runtime_layout_is_rejected_before_output(
    tmp_path, load_addr, entry_addr, reason
):
    r = _pack(
        tmp_path, "1.2.3", load_addr=load_addr, entry_addr=entry_addr
    )
    assert r.returncode != 0
    assert not (tmp_path / "fw.eimg").exists()
    assert "Error:" in r.stderr
    assert reason in r.stderr


def test_empty_firmware_is_rejected(tmp_path):
    r = _pack(tmp_path, "1.2.3", firmware=b"")
    assert r.returncode != 0
    assert not (tmp_path / "fw.eimg").exists()
    assert "image size" in r.stderr


def test_image_of_exactly_16_mib_is_accepted(tmp_path):
    r = _pack(tmp_path, "1.2.3", firmware=b"\x5A" * MAX_IMAGE_SIZE)
    assert r.returncode == 0, r.stderr
    data = (tmp_path / "fw.eimg").read_bytes()
    assert struct.unpack_from("<I", data, IMAGE_SIZE_OFFSET)[0] == MAX_IMAGE_SIZE


def test_image_one_byte_over_16_mib_is_rejected(tmp_path):
    r = _pack(tmp_path, "1.2.3", firmware=b"\x5A" * (MAX_IMAGE_SIZE + 1))
    assert r.returncode != 0
    assert not (tmp_path / "fw.eimg").exists()
    assert "image size" in r.stderr


def test_zero_load_address_skips_entry_range_check(tmp_path):
    # core/image_verify.c treats load_addr 0 as unset and skips the entry
    # range check (XIP). The packer must accept what the bootloader accepts.
    r = _pack(tmp_path, "1.2.3", load_addr="0x0", entry_addr="0x08010000")
    assert r.returncode == 0, r.stderr
    assert (tmp_path / "fw.eimg").exists()


def test_input_at_old_temporary_name_is_left_intact(tmp_path):
    firmware = tmp_path / "fw.eimg.tmp"
    firmware.write_bytes(b"\x5A" * 256)
    r = _pack_paths(firmware, tmp_path / "fw.eimg")
    assert r.returncode == 0, r.stderr
    assert firmware.read_bytes() == b"\x5A" * 256
    assert (tmp_path / "fw.eimg").read_bytes().endswith(b"\x5A" * 256)
    assert _leftover_temporaries(tmp_path) == []


@posix_only
def test_symlink_at_old_temporary_name_is_not_followed(tmp_path):
    (tmp_path / "fw.bin").write_bytes(b"\x5A" * 256)
    victim = tmp_path / "victim.txt"
    victim.write_bytes(b"keep me")
    (tmp_path / "fw.eimg.tmp").symlink_to(victim)
    r = _pack_paths(tmp_path / "fw.bin", tmp_path / "fw.eimg")
    assert r.returncode == 0, r.stderr
    assert victim.read_bytes() == b"keep me"
    assert (tmp_path / "fw.eimg.tmp").is_symlink()
    assert (tmp_path / "fw.eimg").read_bytes().endswith(b"\x5A" * 256)


@posix_only
def test_repack_keeps_existing_output_permissions(tmp_path):
    (tmp_path / "fw.bin").write_bytes(b"\x5A" * 256)
    output = tmp_path / "fw.eimg"
    output.write_bytes(b"old image")
    output.chmod(0o600)
    r = _pack_paths(tmp_path / "fw.bin", output)
    assert r.returncode == 0, r.stderr
    assert stat.S_IMODE(output.stat().st_mode) == 0o600
    assert output.read_bytes().endswith(b"\x5A" * 256)


@posix_only
def test_new_output_gets_plain_create_permissions(tmp_path):
    (tmp_path / "fw.bin").write_bytes(b"\x5A" * 256)
    output = tmp_path / "fw.eimg"
    umask = os.umask(0o022)
    try:
        r = _pack_paths(tmp_path / "fw.bin", output)
    finally:
        os.umask(umask)
    assert r.returncode == 0, r.stderr
    assert stat.S_IMODE(output.stat().st_mode) == 0o644


@posix_only
def test_symlinked_output_is_written_through(tmp_path):
    (tmp_path / "fw.bin").write_bytes(b"\x5A" * 256)
    real = tmp_path / "images" / "real.eimg"
    real.parent.mkdir()
    real.write_bytes(b"old image")
    link = tmp_path / "fw.eimg"
    link.symlink_to(real)
    r = _pack_paths(tmp_path / "fw.bin", link)
    assert r.returncode == 0, r.stderr
    assert link.is_symlink()
    assert real.read_bytes().endswith(b"\x5A" * 256)
    assert _leftover_temporaries(tmp_path) == []
    assert _leftover_temporaries(real.parent) == []


def test_cli_rejects_input_equal_to_output(tmp_path):
    same_path = tmp_path / "firmware.bin"
    same_path.write_bytes(b"\x5A" * 256)
    r = subprocess.run(
        [sys.executable, str(TOOLS / "imgpack.py"),
         "--input", str(same_path), "--output", str(same_path),
         "--version=1.2.3", "--load-addr", "0x08010000",
         "--entry-addr", "0x08010010"],
        capture_output=True, text=True,
    )
    assert r.returncode != 0
    assert same_path.read_bytes() == b"\x5A" * 256
    assert "must be different" in r.stderr
