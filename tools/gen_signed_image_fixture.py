#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EoS Project
"""Emit a signed-image fixture so the C boot path can be run over real tool output.

tools/eos_sign.py used to emit [header][TLV][payload] while stamping hdr_size
as a fixed 156, so core/image_verify.c computed the payload address as
addr + 156 and landed on the TLV block. Every image the tool produced failed
integrity verification on-device.

It now emits [header][payload][TLV], which is the layout master documents:
hdr_size stays 156 for every image, and the trailing TLV area is bound to the
signature through the header's tlv_len and tlv_hash.

Proving that in Python only re-states the tool's own arithmetic. This emits
both layouts as C byte arrays so tests/unit/test_eos_sign_boot_path.c can run
the actual bootloader code over them: the current layout must verify, and the
old one -- TLV between the header and the payload -- must be refused.

Deterministic -- fixed key seed, fixed payload -- so the fixture is stable and
regenerating it is a no-op unless the tool's output really changed.

    python3 tools/gen_signed_image_fixture.py > tests/vectors/signed_image_fixture.h
"""

import importlib.util
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

spec = importlib.util.spec_from_file_location("eos_sign", ROOT / "tools" / "eos_sign.py")
eos_sign = importlib.util.module_from_spec(spec)
spec.loader.exec_module(eos_sign)

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives import serialization

SEED = bytes(range(32))                 # fixed, so the fixture is reproducible
PAYLOAD = bytes((i * 7 + 3) & 0xFF for i in range(256))
ENTRY = 0x08020000
LOAD = 0x08020000
VERSION = 0x00010000


def build(tlv_after_payload: bool) -> bytes:
    key = Ed25519PrivateKey.from_private_bytes(SEED)
    payload_hash = eos_sign.sha256(PAYLOAD)
    pub_raw = key.public_key().public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    key_hash = eos_sign.sha256(pub_raw)

    tlv = eos_sign.build_tlv(payload_hash, key_hash)
    tlv_hash = eos_sign.sha256(tlv)[:eos_sign.EOS_IMG_TLV_HASH_LEN]

    hdr = bytearray(eos_sign.build_header(PAYLOAD, ENTRY, LOAD, VERSION, b"",
                                          eos_sign.SIG_TYPE_ED25519,
                                          len(tlv), tlv_hash))
    sig = key.sign(bytes(hdr[:eos_sign.SIGNED_LEN]))
    hdr[eos_sign.SIGNATURE_OFFSET:eos_sign.SIGNATURE_OFFSET + 64] = sig

    if tlv_after_payload:
        return bytes(hdr) + PAYLOAD + tlv
    # The old shape: TLV between the header and the payload, so
    # addr + hdr_size lands on the TLV block instead of the payload.
    return bytes(hdr) + tlv + PAYLOAD


def carr(b, indent="    "):
    out, line = [], indent
    for i, x in enumerate(b):
        line += "0x%02x," % x
        if (i + 1) % 16 == 0:
            out.append(line); line = indent
    if line.strip():
        out.append(line)
    return "\n".join(out)


def main():
    good = build(True)
    old = build(False)
    pub = Ed25519PrivateKey.from_private_bytes(SEED).public_key().public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw)

    w = sys.stdout.write
    w("/* SPDX-License-Identifier: MIT\n * Copyright (c) 2026 EoS Project\n */\n\n")
    w("/* GENERATED FILE -- do not edit by hand.\n")
    w(" * tools/gen_signed_image_fixture.py\n *\n")
    w(" * Two images over the same payload, the same key and the same header,\n")
    w(" * differing only in where the TLV area sits.\n *\n")
    w(" *   CURRENT: [header][payload][TLV]  -- addr + hdr_size is the payload\n")
    w(" *   OLD:     [header][TLV][payload]  -- addr + hdr_size is the TLV block\n")
    w(" *\n * hdr_size is %d in both: it is the struct size, not a payload offset.\n" % struct.unpack_from('<H', good, 6)[0])
    w(" *\n * The bootloader must accept the first and refuse the second.\n */\n\n")
    w("#ifndef EOS_SIGNED_IMAGE_FIXTURE_H\n#define EOS_SIGNED_IMAGE_FIXTURE_H\n\n")
    w("#define EOS_FIXTURE_PAYLOAD_LEN   %d\n" % len(PAYLOAD))
    w("#define EOS_FIXTURE_GOOD_LEN      %d\n" % len(good))
    w("#define EOS_FIXTURE_OLD_LEN       %d\n" % len(old))
    w("#define EOS_FIXTURE_GOOD_HDR_SIZE %d\n" % struct.unpack_from('<H', good, 6)[0])
    w("#define EOS_FIXTURE_OLD_HDR_SIZE  %d\n" % struct.unpack_from('<H', old, 6)[0])
    w("#define EOS_FIXTURE_TLV_LEN       %d\n\n" % struct.unpack_from('<H', good, 62)[0])
    w("/* Ed25519 public key for the fixed test seed. */\n")
    w("static const unsigned char eos_fixture_pubkey[32] = {\n%s\n};\n\n" % carr(pub))
    w("/* [header][payload][TLV]: the current tool's output. */\n")
    w("static const unsigned char eos_fixture_image_good[EOS_FIXTURE_GOOD_LEN] = {\n%s\n};\n\n" % carr(good))
    w("/* [header][TLV][payload]: what the tool emitted before the fix. */\n")
    w("static const unsigned char eos_fixture_image_old[EOS_FIXTURE_OLD_LEN] = {\n%s\n};\n\n" % carr(old))
    w("#endif /* EOS_SIGNED_IMAGE_FIXTURE_H */\n")
    sys.stderr.write("good hdr_size=%d len=%d ; old hdr_size=%d len=%d\n" % (
        struct.unpack_from('<H', good, 6)[0], len(good),
        struct.unpack_from('<H', old, 6)[0], len(old)))


if __name__ == "__main__":
    main()
