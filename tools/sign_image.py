# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EoS Project

#!/usr/bin/env python3
"""
sign_image.py — eBootloader Image Signing Tool

Signs an existing .eimg file with a digital signature.
Supports CRC32, SHA-256, and Ed25519 signing methods.

Usage:
    python sign_image.py --image firmware.eimg --method crc32
    python sign_image.py --image firmware.eimg --method sha256
    python sign_image.py --image firmware.eimg --method ed25519 --key private.pem
    python sign_image.py --genkey --output keys/

Key Generation:
    python sign_image.py --genkey --output keys/
    Creates keys/private.pem and keys/public.pem (Ed25519 keypair)
    Also generates keys/public_key.h for embedding in bootloader.
"""

import argparse
import hashlib
import struct
import sys
import zlib
from pathlib import Path

EOS_IMG_MAGIC = 0x454F5349
EOS_HASH_SIZE = 32
EOS_SIG_MAX_SIZE = 64

SIG_NONE = 0
SIG_CRC32 = 1
SIG_SHA256 = 2
SIG_ED25519 = 3


def verify_header(data: bytes) -> dict:
    """Parse and verify an image header, return header fields."""
    if len(data) < 24:
        raise ValueError("File too small for image header")

    magic = struct.unpack_from('<I', data, 0)[0]
    if magic != EOS_IMG_MAGIC:
        raise ValueError(f"Bad magic: 0x{magic:08X} (expected 0x{EOS_IMG_MAGIC:08X})")

    hdr_version, hdr_size = struct.unpack_from('<HH', data, 4)
    image_size, load_addr, entry_addr, image_version, flags = \
        struct.unpack_from('<IIIII', data, 8)

    return {
        'hdr_version': hdr_version,
        'hdr_size': hdr_size,
        'image_size': image_size,
        'load_addr': load_addr,
        'entry_addr': entry_addr,
        'image_version': image_version,
        'flags': flags,
    }


def sign_crc32(image_path: Path):
    """Update the CRC32 in the image header hash field."""
    data = bytearray(image_path.read_bytes())
    hdr = verify_header(data)
    hdr_size = hdr['hdr_size']
    image_size = hdr['image_size']

    payload = data[hdr_size:hdr_size + image_size]
    crc = zlib.crc32(bytes(payload)) & 0xFFFFFFFF

    hash_offset = 28
    struct.pack_into('<I', data, hash_offset, crc)

    sig_type_offset = hash_offset + EOS_HASH_SIZE
    data[sig_type_offset] = SIG_CRC32
    data[sig_type_offset + 1] = 0

    image_path.write_bytes(bytes(data))
    print(f"CRC32 signature applied: 0x{crc:08X}")


def sign_sha256(image_path: Path):
    """Compute SHA-256 hash and store in the header."""
    data = bytearray(image_path.read_bytes())
    hdr = verify_header(data)
    hdr_size = hdr['hdr_size']
    image_size = hdr['image_size']

    payload = data[hdr_size:hdr_size + image_size]
    sha = hashlib.sha256(bytes(payload)).digest()

    hash_offset = 28
    data[hash_offset:hash_offset + EOS_HASH_SIZE] = sha

    sig_type_offset = hash_offset + EOS_HASH_SIZE
    data[sig_type_offset] = SIG_SHA256
    data[sig_type_offset + 1] = 0

    image_path.write_bytes(bytes(data))
    print(f"SHA-256 hash applied: {sha.hex()}")


def sign_ed25519(image_path: Path, key_path: Path):
    """Sign the image with Ed25519 using the provided private key."""
    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
        from cryptography.hazmat.primitives import serialization
    except ImportError:
        print("Error: 'cryptography' package required for Ed25519 signing.", file=sys.stderr)
        print("Install with: pip install cryptography", file=sys.stderr)
        sys.exit(1)

    # Load private key
    key_data = key_path.read_bytes()
    try:
        private_key = serialization.load_pem_private_key(key_data, password=None)
    except Exception as e:
        print(f"Error loading private key: {e}", file=sys.stderr)
        sys.exit(1)

    if not isinstance(private_key, Ed25519PrivateKey):
        print("Error: key is not an Ed25519 private key", file=sys.stderr)
        sys.exit(1)

    data = bytearray(image_path.read_bytes())
    hdr = verify_header(data)
    hdr_size = hdr['hdr_size']
    image_size = hdr['image_size']

    # Compute SHA-256 hash of payload
    payload = data[hdr_size:hdr_size + image_size]
    sha = hashlib.sha256(bytes(payload)).digest()

    # Store SHA-256 hash in header
    hash_offset = 28
    data[hash_offset:hash_offset + EOS_HASH_SIZE] = sha

    # Set flags to indicate SHA-256 hash
    flags_offset = 24
    flags = struct.unpack_from('<I', data, flags_offset)[0]
    flags |= (1 << 6)  # EOS_IMG_FLAG_HASH_SHA256
    struct.pack_into('<I', data, flags_offset, flags)

    # Sign the hash with Ed25519
    signature = private_key.sign(sha)

    # Store signature in header
    sig_type_offset = hash_offset + EOS_HASH_SIZE
    data[sig_type_offset] = SIG_ED25519
    data[sig_type_offset + 1] = len(signature)

    sig_offset = sig_type_offset + 2 + 30  # skip reserved bytes
    data[sig_offset:sig_offset + len(signature)] = signature

    image_path.write_bytes(bytes(data))
    print(f"Ed25519 signature applied:")
    print(f"  SHA-256: {sha.hex()}")
    print(f"  Signature: {signature.hex()}")


def generate_keypair(output_dir: Path):
    """Generate an Ed25519 keypair and save to files."""
    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
        from cryptography.hazmat.primitives import serialization
    except ImportError:
        print("Error: 'cryptography' package required for key generation.", file=sys.stderr)
        print("Install with: pip install cryptography", file=sys.stderr)
        sys.exit(1)

    output_dir.mkdir(parents=True, exist_ok=True)

    # Generate keypair
    private_key = Ed25519PrivateKey.generate()
    public_key = private_key.public_key()

    # Save private key
    priv_path = output_dir / "private.pem"
    priv_pem = private_key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.PKCS8,
        encryption_algorithm=serialization.NoEncryption()
    )
    priv_path.write_bytes(priv_pem)
    print(f"Private key saved: {priv_path}")

    # Save public key
    pub_path = output_dir / "public.pem"
    pub_pem = public_key.public_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PublicFormat.SubjectPublicKeyInfo
    )
    pub_path.write_bytes(pub_pem)
    print(f"Public key saved: {pub_path}")

    # Extract raw public key bytes
    pub_raw = public_key.public_bytes(
        encoding=serialization.Encoding.Raw,
        format=serialization.PublicFormat.Raw
    )

    # Generate C header for embedding
    hdr_path = output_dir / "public_key.h"
    hex_bytes = ', '.join(f'0x{b:02x}' for b in pub_raw)
    c_header = f"""\
// SPDX-License-Identifier: MIT
// Auto-generated by sign_image.py --genkey
// DO NOT EDIT — regenerate with: python sign_image.py --genkey

#ifndef EBLDR_PUBLIC_KEY_H
#define EBLDR_PUBLIC_KEY_H

#include <stdint.h>

static const uint8_t ebldr_default_pubkey[32] = {{
    {hex_bytes}
}};

#endif /* EBLDR_PUBLIC_KEY_H */
"""
    hdr_path.write_text(c_header)
    print(f"C header saved: {hdr_path}")
    print(f"Public key (hex): {pub_raw.hex()}")


def extract_pubkey(key_path: Path, output_path: Path):
    """Extract public key from a private key and generate C header."""
    try:
        from cryptography.hazmat.primitives import serialization
    except ImportError:
        print("Error: 'cryptography' package required.", file=sys.stderr)
        sys.exit(1)

    key_data = key_path.read_bytes()
    private_key = serialization.load_pem_private_key(key_data, password=None)
    public_key = private_key.public_key()

    pub_raw = public_key.public_bytes(
        encoding=serialization.Encoding.Raw,
        format=serialization.PublicFormat.Raw
    )

    hex_bytes = ', '.join(f'0x{b:02x}' for b in pub_raw)
    c_header = f"""\
// SPDX-License-Identifier: MIT
// Auto-generated by sign_image.py --extract-pubkey

#ifndef EBLDR_PUBLIC_KEY_H
#define EBLDR_PUBLIC_KEY_H

#include <stdint.h>

static const uint8_t ebldr_default_pubkey[32] = {{
    {hex_bytes}
}};

#endif /* EBLDR_PUBLIC_KEY_H */
"""
    output_path.write_text(c_header)
    print(f"Public key header saved: {output_path}")
    print(f"Public key (hex): {pub_raw.hex()}")


def main():
    parser = argparse.ArgumentParser(description='eBootloader Image Signing Tool')
    parser.add_argument('--image', '-i', help='Image file (.eimg)')
    parser.add_argument('--method', '-m',
                        choices=['crc32', 'sha256', 'ed25519'],
                        help='Signing method')
    parser.add_argument('--key', '-k', help='Private key file (for ed25519)')
    parser.add_argument('--verify', action='store_true', help='Verify instead of sign')
    parser.add_argument('--genkey', action='store_true', help='Generate Ed25519 keypair')
    parser.add_argument('--extract-pubkey', help='Extract public key from private key')
    parser.add_argument('--output', '-o', help='Output directory (for --genkey) or file')

    args = parser.parse_args()

    if args.genkey:
        output_dir = Path(args.output) if args.output else Path('keys')
        generate_keypair(output_dir)
        return

    if args.extract_pubkey:
        key_path = Path(args.extract_pubkey)
        output_path = Path(args.output) if args.output else Path('public_key.h')
        extract_pubkey(key_path, output_path)
        return

    if not args.image or not args.method:
        parser.print_help()
        sys.exit(1)

    image_path = Path(args.image)
    if not image_path.exists():
        print(f"Error: image not found: {args.image}", file=sys.stderr)
        sys.exit(1)

    if args.method == 'crc32':
        sign_crc32(image_path)
    elif args.method == 'sha256':
        sign_sha256(image_path)
    elif args.method == 'ed25519':
        if not args.key:
            print("Error: --key required for ed25519 signing", file=sys.stderr)
            sys.exit(1)
        key_path = Path(args.key)
        if not key_path.exists():
            print(f"Error: key file not found: {args.key}", file=sys.stderr)
            sys.exit(1)
        sign_ed25519(image_path, key_path)


if __name__ == '__main__':
    main()
