# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EoS Project

#!/usr/bin/env python3
"""
uart_recovery.py — eBootloader UART Recovery Tool

Communicates with a device in recovery mode over UART to:
  - Ping and identify the device
  - Query device info (flash layout, slot addresses)
  - Erase a firmware slot
  - Upload a new image
  - Verify the uploaded image
  - Boot into a slot
  - Factory reset

Usage:
    python uart_recovery.py --port COM3 upload --slot A --image firmware.eimg
    python uart_recovery.py --port /dev/ttyUSB0 ping
    python uart_recovery.py --port /dev/ttyUSB0 info
"""

import argparse
import struct
import sys
import time
import hashlib
import os

try:
    import serial
except ImportError:
    print("Error: pyserial is required. Install with: pip install pyserial", file=sys.stderr)
    sys.exit(1)

from pathlib import Path

# Recovery protocol constants
CMD_PING    = 0x01
CMD_INFO    = 0x02
CMD_ERASE   = 0x03
CMD_WRITE   = 0x04
CMD_VERIFY  = 0x05
CMD_BOOT    = 0x06
CMD_LOG     = 0x07
CMD_RESET   = 0x08
CMD_FACTORY = 0x09

ACK  = 0xAA
NACK = 0x55

WRITE_CHUNK = 256
TIMEOUT = 5.0

SLOT_MAP = {'A': 0, 'B': 1, 'a': 0, 'b': 1}

CMD_AUTH = 0x10
CHALLENGE_SIZE = 32
RECOVERY_SECRET_SIZE = 32
BOOT_LOG_ENTRY_SIZE = 16

BOOT_LOG_EVENT_NAMES = {
    0x01: "BOOT_START",
    0x02: "IMAGE_VALID",
    0x03: "IMAGE_INVALID",
    0x04: "SLOT_SELECTED",
    0x05: "ROLLBACK",
    0x06: "RECOVERY_ENTER",
    0x07: "UPGRADE_START",
    0x08: "UPGRADE_DONE",
    0x09: "CONFIRM",
    0x0A: "FACTORY_RESET",
    0x0B: "WATCHDOG_RESET",
    0x0C: "BOOT_FAIL",
    0x20: "AUTH_SUCCESS",
    0x21: "AUTH_FAIL",
}

BOOT_LOG_SLOT_NAMES = {
    0: "A",
    1: "B",
    2: "RECOVERY",
    0xFF: "NONE",
}

def _boot_log_event_name(event: int) -> str:
    return BOOT_LOG_EVENT_NAMES.get(event, f"UNKNOWN(0x{event:02X})")
   
class RecoveryClient:
    def __init__(self, port: str, baud: int = 115200):
        self.ser = serial.Serial(port, baud, timeout=TIMEOUT)
        time.sleep(0.1)

    def close(self):
        self.ser.close()

    def _send_packet(self, cmd: int, slot: int = 0, length: int = 0, offset: int = 0):
        pkt = struct.pack('<BBHI', cmd, slot, length, offset)
        self.ser.write(pkt)

    def _read_ack(self) -> bool:
        data = self.ser.read(1)
        if len(data) == 0:
            return False
        return data[0] == ACK

    def ping(self) -> bool:
        self._send_packet(CMD_PING)
        response = self.ser.read(5)
        if len(response) < 5:
            return False
        if response[0] != ACK:
            return False
        ident = response[1:4].decode('ascii', errors='replace')
        version = response[4]
        print(f"Device responded: {ident} v{version}")
        return True

    def info(self):
        self._send_packet(CMD_INFO)
        response = self.ser.read(1 + 4 * 5)
        if len(response) < 21 or response[0] != ACK:
            print("Failed to get device info")
            return False

        flash_size, slot_a_addr, slot_a_size, slot_b_addr, slot_b_size = \
            struct.unpack('<IIIII', response[1:21])

        print(f"Device Info:")
        print(f"  Flash size:  {flash_size // 1024}K")
        print(f"  Slot A:      0x{slot_a_addr:08X} ({slot_a_size // 1024}K)")
        print(f"  Slot B:      0x{slot_b_addr:08X} ({slot_b_size // 1024}K)")
        return True

    def erase(self, slot: int) -> bool:
        print(f"Erasing slot {['A', 'B'][slot]}...")
        self._send_packet(CMD_ERASE, slot=slot)
        if self._read_ack():
            print("Erase complete")
            return True
        print("Erase failed")
        return False

    def upload(self, slot: int, image_path: str) -> bool:
        data = Path(image_path).read_bytes()
        total = len(data)
        print(f"Uploading {total} bytes to slot {['A', 'B'][slot]}...")

        offset = 0
        while offset < total:
            chunk = data[offset:offset + WRITE_CHUNK]
            chunk_len = len(chunk)

            self._send_packet(CMD_WRITE, slot=slot, length=chunk_len, offset=offset)

            # Wait for ready ACK
            if not self._read_ack():
                print(f"\nDevice not ready at offset {offset}")
                return False

            # Send data
            self.ser.write(chunk)

            # Wait for write ACK
            if not self._read_ack():
                print(f"\nWrite failed at offset {offset}")
                return False

            offset += chunk_len
            pct = (offset * 100) // total
            print(f"\r  Progress: {pct}% ({offset}/{total})", end='', flush=True)

        print("\nUpload complete")
        return True

    def verify(self, slot: int) -> bool:
        print(f"Verifying slot {['A', 'B'][slot]}...")
        self._send_packet(CMD_VERIFY, slot=slot)
        if self._read_ack():
            print("Verification passed")
            return True
        print("Verification failed")
        return False

    def boot(self, slot: int) -> bool:
        print(f"Requesting boot into slot {['A', 'B'][slot]}...")
        self._send_packet(CMD_BOOT, slot=slot)
        if self._read_ack():
            print("Boot command accepted — device will reset")
            return True
        print("Boot command rejected")
        return False

    def reset(self) -> bool:
        print("Resetting device...")
        self._send_packet(CMD_RESET)
        if self._read_ack():
            print("Reset acknowledged")
            return True
        print("Reset failed")
        return False

    def factory_reset(self) -> bool:
        print("Factory reset...")
        self._send_packet(CMD_FACTORY)
        if self._read_ack():
            print("Factory reset complete")
            return True
        print("Factory reset failed")
        return False

    def authenticate(self, secret_hex: str) -> bool:
        try:
            shared_secret = bytes.fromhex(secret_hex)
        except ValueError:
            print("Recovery secret must be valid hex")
            return False

        if len(shared_secret) != RECOVERY_SECRET_SIZE:
            print("Recovery secret must be exactly 32 bytes")
            return False

        self._send_packet(CMD_AUTH)
        response = self.ser.read(1 + CHALLENGE_SIZE)
        if len(response) != 1 + CHALLENGE_SIZE or response[0] != ACK:
            print("Failed to get auth challenge")
            return False

        challenge = response[1:]
        response_digest = hashlib.sha256(challenge + shared_secret).digest()

        self.ser.write(response_digest)

        if self._read_ack():
            print("Authenticated")
            return True

        print("Authentication failed")
        return False

    def read_boot_log(self, start_index: int = 0, max_entries: int = 8) -> bool:
        self._send_packet(CMD_LOG, length=max_entries, offset=start_index)

        header = self.ser.read(3)
        if len(header) != 3 or header[0] != ACK:
            print("Log request failed")
            return False

        entry_count = struct.unpack('<H', header[1:3])[0]
        payload = self.ser.read(entry_count * BOOT_LOG_ENTRY_SIZE)
        if len(payload) != entry_count * BOOT_LOG_ENTRY_SIZE:
            print("Incomplete log response")
            return False

        if entry_count == 0:
            print("No log entries returned")
            return True

        for i in range(entry_count):
            chunk = payload[
                i * BOOT_LOG_ENTRY_SIZE:(i + 1) * BOOT_LOG_ENTRY_SIZE
            ]
            timestamp, event, slot, detail = struct.unpack('<IIII', chunk)

            print(
                f"[{start_index + i:02d}] "
                f"t={timestamp:>8} "
                f"event={_boot_log_event_name(event):<15} "
                f"slot={BOOT_LOG_SLOT_NAMES.get(slot, hex(slot)):<8} "
                f"detail=0x{detail:08X}"
            )

        return True

def main():
    parser = argparse.ArgumentParser(description='eBootloader UART Recovery Tool')
    parser.add_argument('--port', '-p', required=True, help='Serial port (e.g., COM3 or /dev/ttyUSB0)')
    parser.add_argument('--baud', '-b', type=int, default=115200, help='Baud rate')
    parser.add_argument(
        '--secret-hex',
        default=os.environ.get('EBOOT_RECOVERY_SECRET_HEX'),
        help='32-byte recovery secret as hex; defaults to EBOOT_RECOVERY_SECRET_HEX'
    )

    subparsers = parser.add_subparsers(dest='command', required=True)

    subparsers.add_parser('ping', help='Ping device')
    subparsers.add_parser('info', help='Get device info')
    subparsers.add_parser('reset', help='Reset device')
    subparsers.add_parser('factory-reset', help='Factory reset')

    erase_p = subparsers.add_parser('erase', help='Erase slot')
    erase_p.add_argument('--slot', '-s', required=True, choices=['A', 'B'], help='Target slot')

    upload_p = subparsers.add_parser('upload', help='Upload image to slot')
    upload_p.add_argument('--slot', '-s', required=True, choices=['A', 'B'], help='Target slot')
    upload_p.add_argument('--image', '-i', required=True, help='Image file (.eimg)')

    verify_p = subparsers.add_parser('verify', help='Verify slot image')
    verify_p.add_argument('--slot', '-s', required=True, choices=['A', 'B'], help='Target slot')

    boot_p = subparsers.add_parser('boot', help='Boot into slot')
    boot_p.add_argument('--slot', '-s', required=True, choices=['A', 'B'], help='Target slot')

    log_p = subparsers.add_parser('log', help='Read boot log')
    log_p.add_argument('--start', type=int, default=0, help='Start log index')
    log_p.add_argument('--count', type=int, default=8, help='Number of entries to request')

    args = parser.parse_args()

    client = RecoveryClient(args.port, args.baud)

    protected_commands = {'erase', 'upload', 'verify', 'boot', 'factory-reset', 'log'}

    if args.command in protected_commands:
        if not args.secret_hex:
            print("This command requires --secret-hex or EBOOT_RECOVERY_SECRET_HEX", file=sys.stderr)
            sys.exit(1)

        if not client.authenticate(args.secret_hex):
            sys.exit(1)

    try:
        if args.command == 'ping':
            success = client.ping()
        elif args.command == 'info':
            success = client.info()
        elif args.command == 'erase':
            success = client.erase(SLOT_MAP[args.slot])
        elif args.command == 'upload':
            slot = SLOT_MAP[args.slot]
            success = client.erase(slot)
            if success:
                success = client.upload(slot, args.image)
                if success:
                    success = client.verify(slot)
        elif args.command == 'verify':
            success = client.verify(SLOT_MAP[args.slot])
        elif args.command == 'boot':
            success = client.boot(SLOT_MAP[args.slot])
        elif args.command == 'reset':
            success = client.reset()
        elif args.command == 'factory-reset':
            success = client.factory_reset()
        elif args.command == 'log':
            success = client.read_boot_log(args.start, args.count)
        else:
            success = False

        sys.exit(0 if success else 1)

    finally:
        client.close()

if __name__ == '__main__':
    main()
