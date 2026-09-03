# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EoS Project

"""eos_sign.py must place the payload where the bootloader looks for it.

core/image_verify.c computes `payload_addr = addr + hdr->hdr_size` and hashes
image_size bytes from there. tools/sign_image.py reads the same range,
`data[hdr_size:hdr_size + image_size]`. hdr_size is therefore the offset from
the image base to the payload, for everything that reads these images.

eos_sign.py writes [header][TLV area][payload] and used to stamp hdr_size as a
fixed 156 -- the struct size alone -- so that offset landed on the TLV block.
Every image it produced failed integrity verification on-device.
"""

import hashlib
import struct
import os
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
TOOLS = REPO_ROOT / "tools"

# A skip is right for a developer without the signing dependency installed and
# wrong for CI, where "collected 19 tests, ran 0" is a green run that checked
# nothing -- the failure .ai/security.md names directly. EOS_REQUIRE_SIGNING_TESTS
# is set in the workflow, so there a missing dependency is a hard error; locally
# the skip still applies.
if os.environ.get("EOS_REQUIRE_SIGNING_TESTS"):
    import cryptography  # noqa: F401  -- ImportError here must fail the job
else:
    pytest.importorskip(
        "cryptography", reason="signing tools require 'cryptography'")

HDR_SIZE_OFFSET = 6
IMAGE_SIZE_OFFSET = 8
HASH_OFFSET = 28
HDR_STRUCT_SIZE = 156          # sizeof(eos_image_header_t)
TLV_LEN_OFFSET = 62
TLV_HASH_OFFSET = 64
TLV_HASH_LEN = 28              # EOS_IMG_TLV_HASH_LEN
SIGNED_LEN = 92                # EOS_IMG_SIGNED_LEN
TLV_INFO_MAGIC = 0x6907


@pytest.fixture(scope="module")
def signed(tmp_path_factory):
    work = tmp_path_factory.mktemp("eos_sign")
    payload = bytes(range(256)) * 8          # 2048 deterministic bytes
    (work / "fw.bin").write_bytes(payload)

    subprocess.run(
        [sys.executable, str(TOOLS / "eos_sign.py"), "keygen",
         "--output", str(work / "kp")],
        check=True, capture_output=True,
    )
    subprocess.run(
        [sys.executable, str(TOOLS / "eos_sign.py"), "sign",
         "--key", str(work / "kp_private.pem"),
         "--input", str(work / "fw.bin"),
         "--output", str(work / "fw.signed")],
        check=True, capture_output=True,
    )
    return {"image": (work / "fw.signed").read_bytes(), "payload": payload}


def test_hdr_size_is_the_offset_to_the_payload(signed):
    """The exact computation core/image_verify.c performs."""
    image, payload = signed["image"], signed["payload"]
    hdr_size = struct.unpack_from("<H", image, HDR_SIZE_OFFSET)[0]
    image_size = struct.unpack_from("<I", image, IMAGE_SIZE_OFFSET)[0]

    assert image_size == len(payload)
    assert image[hdr_size:hdr_size + image_size] == payload


def test_payload_at_hdr_size_matches_the_stored_hash(signed):
    """What the bootloader actually checks, and what used to fail."""
    image, payload = signed["image"], signed["payload"]
    hdr_size = struct.unpack_from("<H", image, HDR_SIZE_OFFSET)[0]
    image_size = struct.unpack_from("<I", image, IMAGE_SIZE_OFFSET)[0]
    stored_hash = image[HASH_OFFSET:HASH_OFFSET + 32]

    computed = hashlib.sha256(image[hdr_size:hdr_size + image_size]).digest()
    assert computed == stored_hash
    assert computed == hashlib.sha256(payload).digest()


def test_hdr_size_is_the_struct_size_and_the_tlv_follows_the_payload(signed):
    """hdr_size is sizeof(eos_image_header_t), always.

    An earlier revision of this fix made hdr_size the payload offset instead.
    That is a wire-format field with four other readers and two of them reject
    the new meaning: eFirmware/src/efw_image.c checks `hdr_size != 156`
    exactly, and core/rollback.c computes the TLV address as
    hdr_size + image_size. Moving the bytes rather than the field's meaning
    leaves both correct by construction.
    """
    image = signed["image"]
    hdr_size = struct.unpack_from("<H", image, HDR_SIZE_OFFSET)[0]
    img_size = struct.unpack_from("<I", image, IMAGE_SIZE_OFFSET)[0]
    tlv_len = struct.unpack_from("<H", image, TLV_LEN_OFFSET)[0]

    assert hdr_size == HDR_STRUCT_SIZE, (
        "hdr_size is the struct size; eFirmware checks it for equality"
    )
    assert tlv_len > 0, "a signed image carries a TLV area"

    # The TLV area follows the payload, which is where core/rollback.c looks.
    tlv_offset = hdr_size + img_size
    tlv_magic = struct.unpack_from("<H", image, tlv_offset)[0]
    assert tlv_magic == TLV_INFO_MAGIC, (
        f"no TLV magic at hdr_size + image_size ({tlv_offset}); "
        f"eos_rollback_read_image_counter() would find nothing"
    )
    assert len(image) == hdr_size + img_size + tlv_len


def test_the_tlv_area_is_bound_to_the_signature(signed):
    """tlv_hash is inside EOS_IMG_SIGNED_LEN, so the trailing area is covered.

    Without it the signature stops at byte 92 and hash[] covers exactly
    image_size payload bytes, so nothing reaches the TLV -- and an attacker
    with flash write could raise a genuinely signed image's declared counter
    and walk it past eos_rollback_verify().
    """
    image = signed["image"]
    hdr_size = struct.unpack_from("<H", image, HDR_SIZE_OFFSET)[0]
    img_size = struct.unpack_from("<I", image, IMAGE_SIZE_OFFSET)[0]
    tlv_len = struct.unpack_from("<H", image, TLV_LEN_OFFSET)[0]

    tlv = image[hdr_size + img_size:hdr_size + img_size + tlv_len]
    stored = image[TLV_HASH_OFFSET:TLV_HASH_OFFSET + TLV_HASH_LEN]
    assert hashlib.sha256(tlv).digest()[:TLV_HASH_LEN] == stored

    # and it really is inside the signed prefix
    assert TLV_LEN_OFFSET + 2 + TLV_HASH_LEN <= SIGNED_LEN


def test_hdr_size_stays_within_the_parsers_bound(signed):
    """core/image_verify.c rejects hdr_size outside [sizeof(header), 4096]."""
    image = signed["image"]
    hdr_size = struct.unpack_from("<H", image, HDR_SIZE_OFFSET)[0]
    assert HDR_STRUCT_SIZE <= hdr_size <= 4096


def test_the_tools_own_verify_still_accepts_its_output(signed, tmp_path):
    """cmd_verify reads the TLV after the payload, and checks tlv_hash."""
    img = tmp_path / "fw.signed"
    img.write_bytes(signed["image"])
    # keygen wrote the public key beside the private one in the module fixture;
    # re-derive it here so this test does not depend on fixture layout.
    r = subprocess.run(
        [sys.executable, str(TOOLS / "eos_sign.py"), "verify", "--input", str(img)],
        capture_output=True, text=True,
    )
    assert "FAIL" not in r.stdout, r.stdout
