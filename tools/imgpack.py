# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EoS Project

#!/usr/bin/env python3
"""
imgpack.py — eBootloader Image Packer

Packs a raw firmware binary into an eBootloader image with
a proper header (magic, version, CRC32, addresses).

Usage:
    python imgpack.py --input firmware.bin --output firmware.eimg \
        --version 1.0.0 --load-addr 0x08010000 --entry-addr 0x08010000

Input limits (the same rules core/image_verify.c applies at boot):
    - The input must be a regular file of 1 to 16 MiB (16777216 bytes).
      Its size is checked before it is read.
    - --load-addr, --entry-addr and --flags are hexadecimal and must fit
      32 bits.
    - With a non-zero load address, load address + size must not pass
      0xFFFFFFFF, and the entry address must lie in
      [load address, load address + size).
    - A load address of 0 means "unset": the entry-address range check is
      skipped, as the bootloader skips it.

The output is written to a new temporary file in the output's directory
and then renamed over the output, so a failed run leaves no partial image.
If the output is a symlink, its target is replaced and the link is kept.
An existing output keeps its permission bits.
"""

import argparse
import os
import shutil
import struct
import sys
import tempfile
import zlib
from pathlib import Path


EOS_IMG_MAGIC = 0x454F5349
EOS_IMAGE_HDR_VERSION = 1
EOS_HASH_SIZE = 32
EOS_SIG_MAX_SIZE = 64
UINT32_MAX = 0xFFFFFFFF
MAX_IMAGE_SIZE = 16 * 1024 * 1024

# Signature types
SIG_NONE = 0
SIG_CRC32 = 1


def parse_version(version_str: str) -> int:
    """Parse 'major.minor.patch' into uint32 encoding.

    Matches EOS_VERSION_MAKE in include/eos_types.h: 8-bit major, 8-bit
    minor, 16-bit patch. The bootloader compares the encoded uint32
    numerically for anti-rollback, so an out-of-range component must be an
    error — silently masking or carrying it would misorder versions
    (e.g. '1.256.0' would encode identically to '2.0.0', and a truncated
    patch would encode as an *older* image).
    """
    parts = version_str.split('.')
    if len(parts) != 3:
        raise ValueError(f"Version must be major.minor.patch, got '{version_str}'")
    major, minor, patch = int(parts[0]), int(parts[1]), int(parts[2])
    if not (0 <= major <= 0xFF and 0 <= minor <= 0xFF and 0 <= patch <= 0xFFFF):
        raise ValueError(
            f"Version components out of range in '{version_str}' "
            f"(major/minor: 0-255, patch: 0-65535)")
    return (major << 24) | (minor << 16) | patch


def compute_crc32(data: bytes) -> int:
    """Compute CRC32 matching the firmware implementation."""
    return zlib.crc32(data) & 0xFFFFFFFF


def parse_uint32(value: str, label: str) -> int:
    """Parse a hexadecimal CLI value that fits the image's uint32 fields."""
    try:
        parsed = int(value, 16)
    except ValueError as exc:
        raise ValueError(f"{label} must be a hexadecimal uint32, got '{value}'") from exc
    if not 0 <= parsed <= UINT32_MAX:
        raise ValueError(f"{label} is outside the uint32 range: '{value}'")
    return parsed


def validate_image_size(image_size: int) -> None:
    """Reject an image size the bootloader refuses (0, or over 16 MiB)."""
    if image_size <= 0 or image_size > MAX_IMAGE_SIZE:
        raise ValueError(
            f"image size must be between 1 and {MAX_IMAGE_SIZE} bytes; got {image_size}"
        )


def validate_image_layout(image_size: int, load_addr: int, entry_addr: int) -> None:
    """Reject images the bootloader will refuse before writing an artifact."""
    validate_image_size(image_size)
    if not 0 <= load_addr <= UINT32_MAX or not 0 <= entry_addr <= UINT32_MAX:
        raise ValueError("load and entry addresses must fit uint32")
    if load_addr == 0:
        # The firmware parser treats zero as an unset load address and skips
        # the runtime entry-range check for compatibility with XIP targets.
        return
    if image_size > UINT32_MAX - load_addr:
        raise ValueError("load address plus image size overflows uint32")
    image_end = load_addr + image_size
    if entry_addr < load_addr or entry_addr >= image_end:
        raise ValueError(
            f"entry address 0x{entry_addr:08X} is outside the image range "
            f"[0x{load_addr:08X}, 0x{image_end:08X})"
        )


def build_header(
    image_size: int,
    load_addr: int,
    entry_addr: int,
    image_version: int,
    image_crc: int,
    flags: int = 0,
) -> bytes:
    """Build the eos_image_header_t binary structure."""

    validate_image_layout(image_size, load_addr, entry_addr)
    if not 0 <= image_version <= UINT32_MAX:
        raise ValueError("image version must fit uint32")
    if not 0 <= image_crc <= UINT32_MAX or not 0 <= flags <= UINT32_MAX:
        raise ValueError("image CRC and flags must fit uint32")

    # Hash field: store CRC32 in first 4 bytes, rest zeroed
    hash_field = struct.pack('<I', image_crc) + b'\x00' * (EOS_HASH_SIZE - 4)

    # Header size = total packed size
    hdr_size = 4 + 2 + 2 + 4 + 4 + 4 + 4 + 4 + EOS_HASH_SIZE + 1 + 1 + 30 + EOS_SIG_MAX_SIZE

    header = struct.pack('<I', EOS_IMG_MAGIC)
    header += struct.pack('<H', EOS_IMAGE_HDR_VERSION)
    header += struct.pack('<H', hdr_size)
    header += struct.pack('<I', image_size)
    header += struct.pack('<I', load_addr)
    header += struct.pack('<I', entry_addr)
    header += struct.pack('<I', image_version)
    header += struct.pack('<I', flags)
    header += hash_field
    header += struct.pack('<B', SIG_CRC32)   # sig_type
    header += struct.pack('<B', 0)            # sig_len (no signature yet)
    header += b'\x00' * 30                    # tlv_len(2) + tlv_hash(28); 0 = no authenticated TLV area
    header += b'\x00' * EOS_SIG_MAX_SIZE      # signature placeholder

    assert len(header) == hdr_size, f"Header size mismatch: {len(header)} != {hdr_size}"
    return header


def write_output_atomically(output_path: Path, data: bytes) -> None:
    """Write data to output_path so that a failed run leaves no partial image.

    The data goes to a temporary file that mkstemp creates exclusively, with
    a unique name, in the destination directory: it can never be the input,
    another run's file or a planted symlink. It is then renamed over the
    destination. An output symlink is written through, as a direct write
    would: its target is replaced and the link is left in place.
    """
    target = output_path.resolve() if output_path.is_symlink() else output_path
    fd, temp_name = tempfile.mkstemp(
        dir=target.parent, prefix=f".{target.name}.", suffix=".tmp")
    try:
        with os.fdopen(fd, "wb") as temp_file:
            temp_file.write(data)
            temp_file.flush()
            os.fsync(temp_file.fileno())
        try:
            # Repacking must not loosen an existing image's permissions.
            shutil.copymode(target, temp_name)
        except FileNotFoundError:
            # A new output gets what a plain create would give it; mkstemp's
            # 0600 would be stricter than the old direct write.
            umask = os.umask(0)
            os.umask(umask)
            os.chmod(temp_name, 0o666 & ~umask)
        os.replace(temp_name, target)
    except BaseException:
        try:
            os.unlink(temp_name)
        except FileNotFoundError:
            pass
        except OSError as cleanup_error:
            print(f"Warning: cannot remove temporary file {temp_name}: "
                  f"{cleanup_error}", file=sys.stderr)
        raise


def main():
    parser = argparse.ArgumentParser(
        description='eBootloader Image Packer — creates .eimg images'
    )
    parser.add_argument('--input', '-i', required=True, help='Raw firmware binary')
    parser.add_argument('--output', '-o', required=True, help='Output .eimg file')
    parser.add_argument('--version', '-v', required=True, help='Image version (major.minor.patch)')
    parser.add_argument('--load-addr', required=True, help='Load address (hex)')
    parser.add_argument('--entry-addr', required=True, help='Entry address (hex)')
    parser.add_argument('--flags', default='0', help='Image flags (hex)')

    args = parser.parse_args()

    input_path = Path(args.input)
    if not input_path.is_file():
        print(f"Error: input file not found: {args.input}", file=sys.stderr)
        sys.exit(1)

    output_path = Path(args.output)
    if input_path.resolve() == output_path.resolve():
        print("Error: input and output paths must be different", file=sys.stderr)
        sys.exit(1)

    try:
        # Check the size before reading, and never read more than one byte
        # past the limit, so an oversized input cannot exhaust memory.
        validate_image_size(input_path.stat().st_size)
        with input_path.open("rb") as input_file:
            firmware_data = input_file.read(MAX_IMAGE_SIZE + 1)
    except OSError as e:
        print(f"Error: cannot read input file: {e}", file=sys.stderr)
        sys.exit(1)
    except ValueError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
    image_size = len(firmware_data)

    try:
        load_addr = parse_uint32(args.load_addr, "load address")
        entry_addr = parse_uint32(args.entry_addr, "entry address")
        flags = parse_uint32(args.flags, "flags")
        image_version = parse_version(args.version)
        validate_image_layout(image_size, load_addr, entry_addr)
    except ValueError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
    image_crc = compute_crc32(firmware_data)

    header = build_header(
        image_size=image_size,
        load_addr=load_addr,
        entry_addr=entry_addr,
        image_version=image_version,
        image_crc=image_crc,
        flags=flags,
    )

    try:
        write_output_atomically(output_path, header + firmware_data)
    except OSError as e:
        print(f"Error: cannot write output image: {e}", file=sys.stderr)
        sys.exit(1)

    ver_major = (image_version >> 24) & 0xFF
    ver_minor = (image_version >> 16) & 0xFF
    ver_patch = image_version & 0xFFFF

    print(f"eBootloader Image Packed:")
    print(f"  Input:      {args.input} ({image_size} bytes)")
    print(f"  Output:     {args.output} ({len(header) + image_size} bytes)")
    print(f"  Version:    {ver_major}.{ver_minor}.{ver_patch}")
    print(f"  Load addr:  0x{load_addr:08X}")
    print(f"  Entry addr: 0x{entry_addr:08X}")
    print(f"  CRC32:      0x{image_crc:08X}")
    print(f"  Header:     {len(header)} bytes")


if __name__ == '__main__':
    main()
