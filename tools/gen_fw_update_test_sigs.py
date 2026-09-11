#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EoS Project
"""Sign the image headers that tests/unit/test_fw_update.c and
tests/unit/test_fw_transport.c stream through eos_fw_update_finalize().

Since #104 finalize verifies the Ed25519 signature unconditionally, and it
does so *before* the anti-rollback check, so an unsigned test image can no
longer reach the rollback stage -- it is refused as EOS_ERR_SIGNATURE first.
The images those suites build have to carry a real signature. eBoot has no
Ed25519 signer in C (only a verifier), so the signatures are computed here and
committed as tests/vectors/fw_update_test_sigs.h.

The signing key is the RFC 8032 section 7.1 TEST 1 key. Its secret half is
printed in the RFC, so nothing here is a secret. The tests provision the
public half through their simulated OTP (slot 0), which is the path
eos_keystore_init() takes on a real provisioned board.

core/keystore.c's compiled-in default_dev_key is described as this same key
but is not: it differs from byte 21 on and does not decode to a point on the
curve, so no signature can verify against it. That is a defect in its own
right and is not what this generator works around -- the OTP route is used
because it is the production path, not because the fallback is broken.

Each header prefix below must be byte-identical to what the C test builds;
the field values are copied from the tests, and the layout is the one
tests/unit/test_image_header_abi.c pins.

    python3 tools/gen_fw_update_test_sigs.py > tests/vectors/fw_update_test_sigs.h
"""

import hashlib
import struct
import sys

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

# RFC 8032 section 7.1, TEST 1.
RFC8032_TEST1_SECRET = bytes.fromhex(
    "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60")

EOS_IMG_MAGIC = 0x454F5349
EOS_IMAGE_HDR_VERSION = 2
EOS_IMG_STRUCT_SIZE = 156
EOS_IMG_SIGNED_LEN = 92
EOS_IMG_FLAG_HASH_SHA256 = 1 << 6
EOS_SIG_ED25519 = 3
EOS_SIG_MAX_SIZE = 64
EOS_IMG_TLV_HASH_LEN = 28
EOS_TLV_INFO_MAGIC = 0x6907
EOS_TLV_MIN_SEC_VER = 0x50


def crc32_payload(data: bytes) -> int:
    """update_crc() in core/fw_update.c, as test_fw_transport.c mirrors it."""
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xEDB88320 if crc & 1 else crc >> 1
    return (~crc) & 0xFFFFFFFF


def tlv_area(sec_ver: int) -> bytes:
    """[tlv_info(4)][entry_hdr(4)][uint32 value] -- the shape both suites build."""
    total = 4 + 4 + 4
    return (struct.pack("<HH", EOS_TLV_INFO_MAGIC, total)
            + struct.pack("<HH", EOS_TLV_MIN_SEC_VER, 4)
            + struct.pack("<I", sec_ver))


def signed_prefix(image_size, load_addr, entry_addr, version, flags, hash32,
                  tlv):
    tlv_hash = hashlib.sha256(tlv).digest()[:EOS_IMG_TLV_HASH_LEN]
    prefix = struct.pack("<IHHIIIII", EOS_IMG_MAGIC, EOS_IMAGE_HDR_VERSION,
                         EOS_IMG_STRUCT_SIZE, image_size, load_addr,
                         entry_addr, version, flags)
    prefix += hash32
    prefix += struct.pack("<BBH", EOS_SIG_ED25519, EOS_SIG_MAX_SIZE, len(tlv))
    prefix += tlv_hash
    assert len(prefix) == EOS_IMG_SIGNED_LEN, len(prefix)
    return prefix


def fw_update_prefix(sec_ver: int) -> bytes:
    # build_image() in tests/unit/test_fw_update.c
    payload = bytes((i * 7 + 1) & 0xFF for i in range(256))
    return signed_prefix(image_size=256, load_addr=0, entry_addr=0,
                         version=0x00010000, flags=EOS_IMG_FLAG_HASH_SHA256,
                         hash32=hashlib.sha256(payload).digest(),
                         tlv=tlv_area(sec_ver))


def fw_transport_prefix() -> bytes:
    # build_container() in tests/unit/test_fw_transport.c: CRC32 integrity
    # path (flags = 0), the CRC in the first four bytes of hash[].
    payload = bytes(0x5A + (i & 0x1F) for i in range(256))
    hash32 = struct.pack("<I", crc32_payload(payload)) + bytes(28)
    return signed_prefix(image_size=256, load_addr=0x10000, entry_addr=0x10000,
                         version=0, flags=0, hash32=hash32, tlv=tlv_area(5))


def carr(b: bytes, indent="    ") -> str:
    lines = []
    for i in range(0, len(b), 16):
        lines.append(indent + ",".join("0x%02x" % x for x in b[i:i + 16]) + ",")
    return "\n".join(lines)


def main() -> int:
    key = Ed25519PrivateKey.from_private_bytes(RFC8032_TEST1_SECRET)
    pub = key.public_key().public_bytes(serialization.Encoding.Raw,
                                        serialization.PublicFormat.Raw)

    vectors = [
        ("fw_update_sec_ver_3", fw_update_prefix(3),
         "test_fw_update.c build_image(out, 3)"),
        ("fw_update_sec_ver_9", fw_update_prefix(9),
         "test_fw_update.c build_image(out, 9)"),
        ("fw_transport_container", fw_transport_prefix(),
         "test_fw_transport.c build_container()"),
    ]

    out = []
    out.append("/* Generated by tools/gen_fw_update_test_sigs.py -- do not edit.")
    out.append(" *")
    out.append(" * Ed25519 signatures over the 92-byte signed header prefix of the")
    out.append(" * images tests/unit/test_fw_update.c and test_fw_transport.c build,")
    out.append(" * under the RFC 8032 section 7.1 TEST 1 key. Regenerate with:")
    out.append(" *")
    out.append(" *   python3 tools/gen_fw_update_test_sigs.py > tests/vectors/fw_update_test_sigs.h")
    out.append(" */")
    out.append("#ifndef EOS_FW_UPDATE_TEST_SIGS_H")
    out.append("#define EOS_FW_UPDATE_TEST_SIGS_H")
    out.append("")
    out.append("/* The public half. Tests serve it from simulated OTP slot 0 so the")
    out.append(" * keystore selects it the way a provisioned board would. */")
    out.append("static const unsigned char eos_test_sig_pubkey[32] = {")
    out.append(carr(pub))
    out.append("};")
    for name, prefix, origin in vectors:
        sig = key.sign(prefix)
        out.append("")
        out.append("/* %s */" % origin)
        out.append("static const unsigned char eos_test_sig_%s[64] = {" % name)
        out.append(carr(sig))
        out.append("};")
    out.append("")
    out.append("#endif /* EOS_FW_UPDATE_TEST_SIGS_H */")
    sys.stdout.write("\n".join(out) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
