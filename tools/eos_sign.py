#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EoS Project

"""
eos_sign.py — Firmware image signing tool for eBoot secure boot

Usage:
    python eos_sign.py sign --key private.pem --input firmware.bin --output firmware.signed.bin
    python eos_sign.py verify --key public.pem --input firmware.signed.bin
    python eos_sign.py keygen --output keypair

Creates a signed image with the eBoot image header format:
    [eos_image_header_t][TLV area][payload]

Supports Ed25519 signatures (default) and SHA-256 integrity hashes.
"""

import argparse
import hashlib
import os
import struct
import sys

# Image header constants (must match eos_image.h)
EOS_IMG_MAGIC = 0x454F5300    # "EOS\0"
EOS_HDR_VERSION = 1
EOS_HASH_SIZE = 32
EOS_SIG_MAX_SIZE = 64

# Signature types
SIG_TYPE_NONE = 0
SIG_TYPE_ED25519 = 1
SIG_TYPE_RSA2048 = 2

# Image flags
IMG_FLAG_ENCRYPTED = (1 << 0)
IMG_FLAG_COMPRESSED = (1 << 1)
IMG_FLAG_SIGNED = (1 << 2)

# TLV constants
TLV_INFO_MAGIC = 0x6907
TLV_SHA256 = 0x10
TLV_KEYHASH = 0x01
TLV_ED25519 = 0x24


def sha256(data: bytes) -> bytes:
    return hashlib.sha256(data).digest()


def build_header(payload: bytes, entry_addr: int, load_addr: int,
                 version: int, sig: bytes, sig_type: int) -> bytes:
    """Build the eos_image_header_t structure."""
    payload_hash = sha256(payload)
    flags = IMG_FLAG_SIGNED if sig else 0

    # Header: magic(4) + hdr_version(2) + hdr_size(2) + image_size(4) +
    #          load_addr(4) + entry_addr(4) + image_version(4) + flags(4) +
    #          hash(32) + sig_type(1) + sig_len(1) + reserved(30) + signature(64)
    hdr_size = 4 + 2 + 2 + 4 + 4 + 4 + 4 + 4 + 32 + 1 + 1 + 30 + 64  # = 156

    hdr = struct.pack('<I', EOS_IMG_MAGIC)
    hdr += struct.pack('<HH', EOS_HDR_VERSION, hdr_size)
    hdr += struct.pack('<I', len(payload))
    hdr += struct.pack('<I', load_addr)
    hdr += struct.pack('<I', entry_addr)
    hdr += struct.pack('<I', version)
    hdr += struct.pack('<I', flags)
    hdr += payload_hash  # 32 bytes
    hdr += struct.pack('B', sig_type)
    hdr += struct.pack('B', len(sig) if sig else 0)
    hdr += b'\x00' * 30  # reserved

    # Signature field (padded to 64 bytes)
    sig_padded = (sig or b'').ljust(EOS_SIG_MAX_SIZE, b'\x00')
    hdr += sig_padded[:EOS_SIG_MAX_SIZE]

    return hdr


def build_tlv(payload_hash: bytes, key_hash: bytes, sig: bytes) -> bytes:
    """Build TLV area with SHA-256, key hash, and signature entries."""
    entries = b''

    # SHA-256 hash TLV
    entries += struct.pack('<HH', TLV_SHA256, len(payload_hash))
    entries += payload_hash

    # Key hash TLV
    if key_hash:
        entries += struct.pack('<HH', TLV_KEYHASH, len(key_hash))
        entries += key_hash

    # Signature TLV
    if sig:
        entries += struct.pack('<HH', TLV_ED25519, len(sig))
        entries += sig

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

    # Sign the hash
    sig = private_key.sign(payload_hash)

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

    header = build_header(payload, entry, load, version, sig, SIG_TYPE_ED25519)
    tlv = build_tlv(payload_hash, key_hash, sig)

    # Output: [header][tlv][payload]
    output = header + tlv + payload

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
    sig = image[92:92 + sig_len]

    # Find payload (after header + TLV)
    # TLV starts at hdr_size offset
    tlv_magic = struct.unpack('<H', image[hdr_size:hdr_size + 2])[0]
    if tlv_magic == TLV_INFO_MAGIC:
        tlv_total = struct.unpack('<H', image[hdr_size + 2:hdr_size + 4])[0]
        payload_offset = hdr_size + tlv_total
    else:
        payload_offset = hdr_size

    payload = image[payload_offset:payload_offset + img_size]

    # Verify hash
    computed_hash = sha256(payload)
    if computed_hash != stored_hash:
        print("FAIL: Hash mismatch")
        print(f"  Stored:   {stored_hash.hex()}")
        print(f"  Computed: {computed_hash.hex()}")
        sys.exit(1)
    print("  Hash:      OK")

    # Verify signature
    try:
        public_key.verify(sig, computed_hash)
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
