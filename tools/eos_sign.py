#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EoS Project

"""
eos_sign.py — Firmware image signing tool for eBoot secure boot

This tool emits [header][payload][TLV area].

It used to emit [header][TLV][payload] while writing a fixed hdr_size of 156,
so core/image_verify.c's `payload_addr = addr + hdr->hdr_size` landed on the
TLV block and every image it produced failed integrity verification on-device.

An earlier revision of this fix moved hdr_size instead, making it the offset
to the payload. That was the wrong half to move: hdr_size is a wire-format
field with four other readers, two of which reject the new meaning --
eFirmware/src/efw_image.c checks `hdr_size != 156` exactly, and
core/rollback.c computes the TLV address as hdr_size + image_size. Moving the
bytes rather than the field's meaning leaves both correct by construction, and
it is what master settled on: include/eos_image.h now carries tlv_len and
tlv_hash inside the signed prefix, documented as "the TLV area sits after the
payload".

So: hdr_size stays 156 always, the TLV area follows the payload, and the
header declares its length and digest. Those two fields are inside
EOS_IMG_SIGNED_LEN, which is what makes a TLV-declared value such as
EOS_TLV_MIN_SEC_VER trustworthy enough to gate anti-rollback on.

Usage:
    python eos_sign.py sign --key private.pem --input firmware.bin --output firmware.signed.bin
    python eos_sign.py verify --key public.pem --input firmware.signed.bin
    python eos_sign.py keygen --output keypair

Creates a signed image with the eBoot image header format:
    [eos_image_header_t][payload][TLV area]

Supports Ed25519 signatures (default) and SHA-256 integrity hashes.
"""

import argparse
import hashlib
import os
import struct
import sys

# Image header constants (must match eos_image.h)
EOS_IMG_MAGIC = 0x454F5349    # "EOSI" (matches eos_types.h)
EOS_HDR_VERSION = 2
EOS_HASH_SIZE = 32
EOS_SIG_MAX_SIZE = 64

# Header layout (include/eos_image.h static-asserts these offsets).
SIG_TYPE_OFFSET = 60
SIG_LEN_OFFSET = 61
SIGNATURE_OFFSET = 92

# Bytes [0, SIGNED_LEN) of the header are covered by the signature: everything
# except signature[] itself.
SIGNED_LEN = SIGNATURE_OFFSET

# Signature types — must match eos_sig_type_t in include/eos_types.h.
SIG_TYPE_NONE = 0
SIG_TYPE_CRC32 = 1
SIG_TYPE_SHA256 = 2
SIG_TYPE_ED25519 = 3
SIG_TYPE_ECDSA = 4

# Image flags — must match EOS_IMG_FLAG_* in include/eos_types.h.
IMG_FLAG_ENCRYPTED = (1 << 0)
IMG_FLAG_COMPRESSED = (1 << 1)
IMG_FLAG_DEBUG = (1 << 2)
IMG_FLAG_RTOS = (1 << 3)
IMG_FLAG_LINUX = (1 << 4)
IMG_FLAG_SIGNED = (1 << 5)
IMG_FLAG_HASH_SHA256 = (1 << 6)

# TLV constants
TLV_INFO_MAGIC = 0x6907
TLV_SHA256 = 0x10
TLV_KEYHASH = 0x01
TLV_ED25519 = 0x24


def sha256(data: bytes) -> bytes:
    return hashlib.sha256(data).digest()


EOS_IMG_STRUCT_SIZE = 156      # sizeof(eos_image_header_t); pinned in eos_image.h
EOS_IMG_TLV_HASH_LEN = 28      # EOS_IMG_TLV_HASH_LEN


def build_header(payload: bytes, entry_addr: int, load_addr: int,
                 version: int, sig: bytes, sig_type: int,
                 tlv_len: int = 0, tlv_hash: bytes = b'') -> bytes:
    """Build the eos_image_header_t structure.

    tlv_len and tlv_hash describe the TLV area that follows the payload.
    Both sit inside EOS_IMG_SIGNED_LEN, so the signature covers them and the
    trailing area cannot be rewritten without invalidating it. hdr_size is
    always EOS_IMG_STRUCT_SIZE -- it is the size of this struct, which is what
    core/image_verify.c, core/rollback.c, stage1/jump_app.c and
    eFirmware/src/efw_image.c all read it as.
    """
    payload_hash = sha256(payload)

    # EOS_IMG_FLAG_HASH_SHA256 is what makes eos_image_verify_integrity() take
    # the SHA-256 path; without it the bootloader treats hash[] as a CRC32.
    flags = IMG_FLAG_HASH_SHA256
    if sig_type != SIG_TYPE_NONE:
        flags |= IMG_FLAG_SIGNED

    # Header: magic(4) + hdr_version(2) + hdr_size(2) + image_size(4) +
    #          load_addr(4) + entry_addr(4) + image_version(4) + flags(4) +
    #          hash(32) + sig_type(1) + sig_len(1) + tlv_len(2) +
    #          tlv_hash(28) + signature(64)
    # hdr_size is the size of this struct, always, and the TLV area follows
    # the payload rather than preceding it.
    hdr = struct.pack('<I', EOS_IMG_MAGIC)
    hdr += struct.pack('<HH', EOS_HDR_VERSION, EOS_IMG_STRUCT_SIZE)
    hdr += struct.pack('<I', len(payload))
    hdr += struct.pack('<I', load_addr)
    hdr += struct.pack('<I', entry_addr)
    hdr += struct.pack('<I', version)
    hdr += struct.pack('<I', flags)
    hdr += payload_hash  # 32 bytes
    hdr += struct.pack('B', sig_type)
    # Ed25519 signatures are always 64 bytes. sig_len sits inside the signed
    # region, so it has to be final before the prefix is signed — it cannot
    # wait for the signature to exist.
    hdr += struct.pack('B', EOS_SIG_MAX_SIZE if sig_type == SIG_TYPE_ED25519
                       else (len(sig) if sig else 0))
    # tlv_len and tlv_hash bind the trailing TLV area to the signed prefix.
    # Neither the signature (which stops at EOS_IMG_SIGNED_LEN) nor hash[]
    # (which covers exactly image_size payload bytes) reaches those bytes, so
    # without this pair an attacker with flash write could raise a genuinely
    # signed image's declared counter and walk it past eos_rollback_verify().
    hdr += struct.pack('<H', tlv_len)
    hdr += (tlv_hash or b'').ljust(EOS_IMG_TLV_HASH_LEN, b'\x00')[:EOS_IMG_TLV_HASH_LEN]

    # Signature field (padded to 64 bytes)
    sig_padded = (sig or b'').ljust(EOS_SIG_MAX_SIZE, b'\x00')
    hdr += sig_padded[:EOS_SIG_MAX_SIZE]

    return hdr


def build_tlv(payload_hash: bytes, key_hash: bytes) -> bytes:
    """Build the TLV area that follows the payload.

    No signature entry. The signature lives in the header's signature[] field
    and covers EOS_IMG_SIGNED_LEN, which includes tlv_hash -- so a TLV area
    containing the signature could not be hashed before the signature existed.
    Putting the signature in the header and the metadata in the TLV is what
    makes that ordering resolvable, and it is the arrangement
    include/eos_image.h documents.
    """
    entries = b''

    # SHA-256 hash TLV
    entries += struct.pack('<HH', TLV_SHA256, len(payload_hash))
    entries += payload_hash

    # Key hash TLV
    if key_hash:
        entries += struct.pack('<HH', TLV_KEYHASH, len(key_hash))
        entries += key_hash

    # TLV info header
    total_len = 4 + len(entries)  # info header + entries
    info = struct.pack('<HH', TLV_INFO_MAGIC, total_len)

    return info + entries


def cmd_keygen(args):
    """Generate Ed25519 keypair."""
    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
        from cryptography.hazmat.primitives import serialization
    except ImportError:
        print("Error: pip install cryptography")
        sys.exit(1)

    private_key = Ed25519PrivateKey.generate()
    public_key = private_key.public_key()

    # Save private key
    priv_pem = private_key.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption(),
    )
    priv_path = args.output + "_private.pem"
    with open(priv_path, 'wb') as f:
        f.write(priv_pem)

    # Save public key
    pub_pem = public_key.public_bytes(
        serialization.Encoding.PEM,
        serialization.PublicFormat.SubjectPublicKeyInfo,
    )
    pub_path = args.output + "_public.pem"
    with open(pub_path, 'wb') as f:
        f.write(pub_pem)

    # Save raw public key (32 bytes) for embedding in firmware
    pub_raw = public_key.public_bytes(
        serialization.Encoding.Raw,
        serialization.PublicFormat.Raw,
    )
    raw_path = args.output + "_public.raw"
    with open(raw_path, 'wb') as f:
        f.write(pub_raw)

    key_hash = sha256(pub_raw)

    print(f"Private key: {priv_path}")
    print(f"Public key:  {pub_path}")
    print(f"Raw key:     {raw_path} ({len(pub_raw)} bytes)")
    print(f"Key hash:    {key_hash.hex()}")


def cmd_sign(args):
    """Sign a firmware binary."""
    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
        from cryptography.hazmat.primitives import serialization
    except ImportError:
        print("Error: pip install cryptography")
        sys.exit(1)

    # Load private key
    with open(args.key, 'rb') as f:
        private_key = serialization.load_pem_private_key(f.read(), password=None)

    # Load firmware payload
    with open(args.input, 'rb') as f:
        payload = f.read()

    # Compute hash
    payload_hash = sha256(payload)

    # Get public key hash for TLV
    pub_raw = private_key.public_key().public_bytes(
        serialization.Encoding.Raw,
        serialization.PublicFormat.Raw,
    )
    key_hash = sha256(pub_raw)

    # Build image
    version = getattr(args, 'version', 0x00010000)
    entry = getattr(args, 'entry', 0x08020000)
    load = getattr(args, 'load', 0x08020000)

    # Build the header with a zeroed signature, sign its prefix, then splice
    # the signature in. Signing the header rather than payload_hash alone binds
    # image_size, load_addr, entry_addr and flags to the signature, so none of
    # them can be altered without invalidating it.
    # tlv_len and tlv_hash sit inside the signed prefix, so the TLV area has
    # to be final before the prefix is signed. It is, because it carries no
    # signature: the signature goes in the header's signature[] field.
    tlv = build_tlv(payload_hash, key_hash)
    tlv_hash = sha256(tlv)[:EOS_IMG_TLV_HASH_LEN]

    header = bytearray(build_header(payload, entry, load, version, b'',
                                    SIG_TYPE_ED25519, len(tlv), tlv_hash))
    sig = private_key.sign(bytes(header[:SIGNED_LEN]))
    if len(sig) != EOS_SIG_MAX_SIZE:
        # Not an assert: `python -O` removes those, and this is release
        # signing tooling. A short signature spliced into a 64-byte field
        # would be padded with zeros and fail on-device, or worse.
        raise SystemExit(
            f'ed25519 signature is {len(sig)} bytes, expected {EOS_SIG_MAX_SIZE}')
    header[SIGNATURE_OFFSET:SIGNATURE_OFFSET + EOS_SIG_MAX_SIZE] = sig
    header = bytes(header)

    # Output: [header][payload][tlv]
    output = header + payload + tlv

    with open(args.output, 'wb') as f:
        f.write(output)

    print(f"Signed image: {args.output}")
    print(f"  Payload:    {len(payload)} bytes")
    print(f"  Header:     {len(header)} bytes")
    print(f"  TLV:        {len(tlv)} bytes")
    print(f"  Total:      {len(output)} bytes")
    print(f"  SHA-256:    {payload_hash.hex()}")
    print(f"  Key hash:   {key_hash.hex()}")
    print(f"  Signature:  {sig.hex()[:32]}...")


def cmd_verify(args):
    """Verify a signed firmware image."""
    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
        from cryptography.hazmat.primitives import serialization
    except ImportError:
        print("Error: pip install cryptography")
        sys.exit(1)

    with open(args.key, 'rb') as f:
        public_key = serialization.load_pem_public_key(f.read())

    with open(args.input, 'rb') as f:
        image = f.read()

    # This command is pointed at untrusted files, so its malformed-input path
    # has to be as tidy as its happy path. Without this a short image raised
    # a struct.error traceback instead of the FAIL: line every other failure
    # here produces.
    if len(image) < EOS_IMG_STRUCT_SIZE:
        print(f'FAIL: image is {len(image)} bytes, shorter than the '
              f'{EOS_IMG_STRUCT_SIZE}-byte header')
        sys.exit(1)

    # Parse header (first 4 bytes = magic)
    magic = struct.unpack('<I', image[:4])[0]
    if magic != EOS_IMG_MAGIC:
        print(f"FAIL: Bad magic 0x{magic:08X} (expected 0x{EOS_IMG_MAGIC:08X})")
        sys.exit(1)

    hdr_size = struct.unpack('<H', image[6:8])[0]
    img_size = struct.unpack('<I', image[8:12])[0]
    version = struct.unpack('<I', image[20:24])[0]
    stored_hash = image[28:60]
    sig_type = image[60]
    sig_len = image[61]
    tlv_len = struct.unpack('<H', image[62:64])[0]
    stored_tlv_hash = image[64:64 + EOS_IMG_TLV_HASH_LEN]
    sig = image[92:92 + sig_len]

    # hdr_size is the struct size, always. The payload follows the header and
    # the TLV area follows the payload.
    if hdr_size != EOS_IMG_STRUCT_SIZE:
        print(f'FAIL: hdr_size is {hdr_size}, expected {EOS_IMG_STRUCT_SIZE}')
        sys.exit(1)

    payload_offset = EOS_IMG_STRUCT_SIZE
    payload = image[payload_offset:payload_offset + img_size]
    if len(payload) != img_size:
        print(f'FAIL: image declares {img_size} payload bytes but holds '
              f'{len(payload)}')
        sys.exit(1)

    # The TLV area is bound to the signature through tlv_hash, so checking it
    # here is checking something the signature actually covers.
    if tlv_len:
        tlv_offset = payload_offset + img_size
        tlv = image[tlv_offset:tlv_offset + tlv_len]
        if len(tlv) != tlv_len:
            print(f'FAIL: header declares a {tlv_len}-byte TLV area but the '
                  f'image holds {len(tlv)} bytes after the payload')
            sys.exit(1)
        tlv_magic = struct.unpack('<H', tlv[:2])[0] if len(tlv) >= 2 else 0
        if tlv_magic != TLV_INFO_MAGIC:
            print(f'FAIL: no TLV magic at offset {tlv_offset}')
            sys.exit(1)
        if sha256(tlv)[:EOS_IMG_TLV_HASH_LEN] != stored_tlv_hash:
            print('FAIL: TLV area does not match tlv_hash in the signed header')
            sys.exit(1)

    # Verify hash
    computed_hash = sha256(payload)
    if computed_hash != stored_hash:
        print("FAIL: Hash mismatch")
        print(f"  Stored:   {stored_hash.hex()}")
        print(f"  Computed: {computed_hash.hex()}")
        sys.exit(1)
    print("  Hash:      OK")

    # Verify signature over the header prefix (see SIGNED_LEN).
    try:
        public_key.verify(sig, image[:SIGNED_LEN])
        print("  Signature: OK")
    except Exception as e:
        print(f"  Signature: FAILED ({e})")
        sys.exit(1)

    print(f"  Version:   0x{version:08X}")
    print(f"  Size:      {img_size} bytes")
    print("VERIFIED: Image is authentic and intact")


def main():
    parser = argparse.ArgumentParser(description='EoS Firmware Image Signing Tool')
    sub = parser.add_subparsers(dest='command', required=True)

    # keygen
    p_kg = sub.add_parser('keygen', help='Generate Ed25519 keypair')
    p_kg.add_argument('--output', default='eos_key', help='Key file prefix')

    # sign
    p_sign = sub.add_parser('sign', help='Sign a firmware binary')
    p_sign.add_argument('--key', required=True, help='Ed25519 private key (PEM)')
    p_sign.add_argument('--input', required=True, help='Firmware binary')
    p_sign.add_argument('--output', required=True, help='Signed image output')
    p_sign.add_argument('--version', type=lambda x: int(x, 0), default=0x00010000)
    p_sign.add_argument('--entry', type=lambda x: int(x, 0), default=0x08020000)
    p_sign.add_argument('--load', type=lambda x: int(x, 0), default=0x08020000)

    # verify
    p_ver = sub.add_parser('verify', help='Verify a signed image')
    p_ver.add_argument('--key', required=True, help='Ed25519 public key (PEM)')
    p_ver.add_argument('--input', required=True, help='Signed image')

    args = parser.parse_args()
    if args.command == 'keygen':
        cmd_keygen(args)
    elif args.command == 'sign':
        cmd_sign(args)
    elif args.command == 'verify':
        cmd_verify(args)


if __name__ == '__main__':
    main()
