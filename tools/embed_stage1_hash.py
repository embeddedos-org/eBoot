#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EoS Project
#
# tools/embed_stage1_hash.py
#
# Reads a Stage-1 binary, computes its SHA-256 digest, and generates a
# C header file containing the hash as a static const uint8_t array.
#
# Usage:
#   python embed_stage1_hash.py --input stage1.bin --output stage0/stage1_hash_gen.h

"""Generate a C header with the SHA-256 hash of a Stage-1 binary."""

import argparse
import hashlib
import os
import sys
from pathlib import Path


def compute_sha256(filepath: Path) -> bytes:
    """Return the SHA-256 digest of *filepath*."""
    h = hashlib.sha256()
    with open(filepath, "rb") as f:
        while True:
            chunk = f.read(65536)
            if not chunk:
                break
            h.update(chunk)
    return h.digest()


def format_c_array(digest: bytes) -> str:
    """Format a 32-byte digest as a C initializer list."""
    lines = []
    for i in range(0, len(digest), 8):
        values = ", ".join(f"0x{b:02x}" for b in digest[i : i + 8])
        lines.append(f"    {values},")
    return "\n".join(lines)


def generate_header(digest: bytes, input_name: str) -> str:
    """Return the full C header file content."""
    hex_str = digest.hex()
    array_body = format_c_array(digest)

    return f"""\
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
//
// AUTO-GENERATED — do not edit.
// Source binary: {input_name}
// SHA-256:       {hex_str}

#ifndef STAGE1_HASH_GEN_H
#define STAGE1_HASH_GEN_H

#include <stdint.h>

static const uint8_t stage1_expected_hash[32] = {{
{array_body}
}};

#endif /* STAGE1_HASH_GEN_H */
"""


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Embed SHA-256 hash of Stage-1 binary into a C header."
    )
    parser.add_argument(
        "--input", required=True, help="Path to the Stage-1 binary (e.g. stage1.bin)"
    )
    parser.add_argument(
        "--output",
        required=True,
        help="Output C header path (e.g. stage0/stage1_hash_gen.h)",
    )
    args = parser.parse_args()

    input_path = Path(args.input)
    output_path = Path(args.output)

    if not input_path.is_file():
        print(f"Error: input file not found: {input_path}", file=sys.stderr)
        return 1

    digest = compute_sha256(input_path)
    header_content = generate_header(digest, input_path.name)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(header_content, encoding="utf-8")

    print(f"Generated {output_path}")
    print(f"  SHA-256: {digest.hex()}")
    print(f"  Source:  {input_path} ({os.path.getsize(input_path)} bytes)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
