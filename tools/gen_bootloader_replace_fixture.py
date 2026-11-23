#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EoS Project
"""Emit the signed stage-1 fixtures for the bootloader-replacement tests.

tools/gen_signed_image_fixture.py covers the application-image boot path;
this one covers the replacement path (docs/design/
bootloader-replacement-entry.md §3): a stage-1 image whose load address is
the STM32F4 stage-1 base, whose TLV area carries EOS_TLV_MIN_SEC_VER and
EOS_TLV_TARGET ("<board>:stage-1"), and whose signature verifies against
the keystore's compiled-in development key.

The key is RFC 8032 section 7.1 TEST 1 -- the same key core/keystore.c
falls back to when the board has no OTP, which is the host test boards'
configuration. Its private half is published in the RFC, so these fixtures
prove the verification logic, never a production trust anchor.

Deterministic -- fixed key, fixed payload -- so regenerating is a no-op
unless the image format really changed:

    python3 tools/gen_bootloader_replace_fixture.py \
        > tests/vectors/bootloader_replace_fixture.h

Variants, all over the same 256-byte payload:

  GOOD        -- load_addr 0x08004000, counter 1, target "stm32f4:stage-1".
  CRC_ONLY    -- same image, but EOS_IMG_FLAG_HASH_SHA256 cleared and
                 hash[] holding a CRC32: the replacement path must refuse
                 a CRC-only stage-1 image outright (§3 step 3).
  WRONG_BOARD -- same image, target "stm32h7:stage-1": the target check
                 must refuse an image built for another board.
"""

import struct
import sys

from cryptography.hazmat.primitives.asymmetric.ed25519 import (
    Ed25519PrivateKey,
)

# RFC 8032 section 7.1, TEST 1. The public key below is what
# core/keystore.c compiles in as the development trust anchor.
SEED = bytes.fromhex(
    "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60"
)
EXPECTED_PUBKEY = bytes.fromhex(
    "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"
)

PAYLOAD = bytes((i * 7 + 3) & 0xFF for i in range(256))
LOAD_ADDR = 0x08004000   # STM32F4 stage-1 base
ENTRY_ADDR = 0x08004000  # inside the stage-1 region
VERSION = 0x00010000
COUNTER = 1

# Header layout (include/eos_image.h static-asserts these offsets).
EOS_IMG_MAGIC = 0x454F5349
EOS_HDR_VERSION = 2
EOS_IMG_STRUCT_SIZE = 156
EOS_IMG_TLV_HASH_LEN = 28
SIGNED_LEN = 92
SIG_TYPE_ED25519 = 3
IMG_FLAG_SIGNED = 1 << 5
IMG_FLAG_HASH_SHA256 = 1 << 6

# TLV constants (include/eos_image_tlv.h).
TLV_INFO_MAGIC = 0x6907
TLV_KEYHASH = 0x01
TLV_SHA256 = 0x10
TLV_TARGET = 0x41
TLV_MIN_SEC_VER = 0x50


def sha256(data: bytes) -> bytes:
    import hashlib

    return hashlib.sha256(data).digest()


def crc32(data: bytes) -> int:
    import binascii

    return binascii.crc32(data) & 0xFFFFFFFF


def build_tlv(payload_hash: bytes, key_hash: bytes, counter: int,
              target: bytes) -> bytes:
    """TLV area: [info][SHA256][KEYHASH][MIN_SEC_VER][TARGET].

    Each entry's value is padded to a 4-byte boundary: core/image_tlv.c
    advances its offset that way, so the next entry has to start there.
    """
    entries = b""

    def entry(type_: int, value: bytes) -> bytes:
        padded = value + b"\x00" * ((-len(value)) % 4)
        return struct.pack("<HH", type_, len(value)) + padded

    entries += entry(TLV_SHA256, payload_hash)
    entries += entry(TLV_KEYHASH, key_hash)
    entries += entry(TLV_MIN_SEC_VER, struct.pack("<I", counter))
    entries += entry(TLV_TARGET, target)

    total_len = 4 + len(entries)
    return struct.pack("<HH", TLV_INFO_MAGIC, total_len) + entries


def build_header(payload: bytes, flags: int, hash_bytes: bytes,
                 sig_type: int, tlv: bytes) -> bytearray:
    """eos_image_header_t with a zeroed signature field, ready to sign."""
    tlv_hash = sha256(tlv)[:EOS_IMG_TLV_HASH_LEN]
    hdr = struct.pack("<I", EOS_IMG_MAGIC)
    hdr += struct.pack("<HH", EOS_HDR_VERSION, EOS_IMG_STRUCT_SIZE)
    hdr += struct.pack("<I", len(payload))
    hdr += struct.pack("<I", LOAD_ADDR)
    hdr += struct.pack("<I", ENTRY_ADDR)
    hdr += struct.pack("<I", VERSION)
    hdr += struct.pack("<I", flags)
    hdr += hash_bytes.ljust(32, b"\x00")[:32]
    hdr += struct.pack("B", sig_type)
    hdr += struct.pack("B", 64 if sig_type == SIG_TYPE_ED25519 else 0)
    hdr += struct.pack("<H", len(tlv))
    hdr += tlv_hash
    hdr += b"\x00" * 64
    assert len(hdr) == EOS_IMG_STRUCT_SIZE, len(hdr)
    return bytearray(hdr)


def sign_image(payload: bytes, flags: int, hash_bytes: bytes,
               tlv: bytes, key) -> bytes:
    hdr = build_header(payload, flags, hash_bytes, SIG_TYPE_ED25519, tlv)
    sig = key.sign(bytes(hdr[:SIGNED_LEN]))
    assert len(sig) == 64
    hdr[SIGNED_LEN:SIGNED_LEN + 64] = sig
    return bytes(hdr) + payload + tlv


def c_array(name: str, data: bytes) -> str:
    lines = [f"static const unsigned char {name}[{len(data)}] = {{"]
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        lines.append("    " + ",".join(f"0x{b:02x}" for b in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)


def main() -> int:
    from cryptography.hazmat.primitives import serialization

    key = Ed25519PrivateKey.from_private_bytes(SEED)
    pub_raw = key.public_key().public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    if pub_raw != EXPECTED_PUBKEY:
        print("REFUSING: seed does not derive the RFC 8032 TEST 1 key",
              file=sys.stderr)
        return 1

    payload_hash = sha256(PAYLOAD)
    key_hash = sha256(pub_raw)

    good_tlv = build_tlv(payload_hash, key_hash, COUNTER, b"stm32f4:stage-1")
    good = sign_image(PAYLOAD, IMG_FLAG_SIGNED | IMG_FLAG_HASH_SHA256,
                      payload_hash, good_tlv, key)

    crc_hash = struct.pack("<I", crc32(PAYLOAD))
    crc_only = sign_image(PAYLOAD, IMG_FLAG_SIGNED, crc_hash, good_tlv, key)

    wrong_tlv = build_tlv(payload_hash, key_hash, COUNTER, b"stm32h7:stage-1")
    wrong_board = sign_image(PAYLOAD, IMG_FLAG_SIGNED | IMG_FLAG_HASH_SHA256,
                             payload_hash, wrong_tlv, key)

    w = sys.stdout.write
    w("/* SPDX-License-Identifier: MIT\n"
      " * Copyright (c) 2026 EoS Project\n"
      " */\n\n")
    w("/* GENERATED FILE -- do not edit by hand.\n"
      " * tools/gen_bootloader_replace_fixture.py\n"
      " *\n"
      " * Signed stage-1 images for the bootloader-replacement entry point\n"
      " * (docs/design/bootloader-replacement-entry.md §3). All three are\n"
      " * signed with the RFC 8032 TEST 1 key -- the keystore's compiled-in\n"
      " * development anchor -- over the same 256-byte payload, and all\n"
      " * three carry EOS_TLV_MIN_SEC_VER = 1:\n"
      " *\n"
      " *   GOOD        target \"stm32f4:stage-1\" -- must verify.\n"
      " *   CRC_ONLY    SHA-256 flag cleared, hash[] is a CRC32 -- the\n"
      " *               replacement path must refuse a CRC-only image.\n"
      " *   WRONG_BOARD target \"stm32h7:stage-1\" -- the target check must\n"
      " *               refuse an image built for another board.\n"
      " */\n\n")
    w("#ifndef EOS_BOOTLOADER_REPLACE_FIXTURE_H\n"
      "#define EOS_BOOTLOADER_REPLACE_FIXTURE_H\n\n")
    w(f"#define EOS_BTLDR_FIXTURE_PAYLOAD_LEN   {len(PAYLOAD)}\n")
    w(f"#define EOS_BTLDR_FIXTURE_GOOD_LEN      {len(good)}\n")
    w(f"#define EOS_BTLDR_FIXTURE_CRC_ONLY_LEN  {len(crc_only)}\n")
    w(f"#define EOS_BTLDR_FIXTURE_WRONG_BOARD_LEN {len(wrong_board)}\n")
    w(f"#define EOS_BTLDR_FIXTURE_TLV_LEN       {len(good_tlv)}\n")
    w(f"#define EOS_BTLDR_FIXTURE_COUNTER       {COUNTER}\n")
    w("#define EOS_BTLDR_FIXTURE_LOAD_ADDR     0x08004000\n\n")
    w("/* RFC 8032 section 7.1 TEST 1 public key (the dev trust anchor). */\n")
    w(c_array("eos_btldr_fixture_pubkey", EXPECTED_PUBKEY))
    w("\n\n")
    w(c_array("eos_btldr_fixture_image_good", good))
    w("\n\n")
    w(c_array("eos_btldr_fixture_image_crc_only", crc_only))
    w("\n\n")
    w(c_array("eos_btldr_fixture_image_wrong_board", wrong_board))
    w("\n\n#endif /* EOS_BOOTLOADER_REPLACE_FIXTURE_H */\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
