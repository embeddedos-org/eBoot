import hashlib
import importlib.util
import struct
import sys
import types
import uuid
from pathlib import Path


UART_RECOVERY_PATH = Path(__file__).resolve().parents[2] / "tools" / "uart_recovery.py"


class FakeSerial:
    def __init__(self, incoming=b""):
        self.incoming = bytearray(incoming)
        self.writes = []
        self.closed = False

    def read(self, size):
        chunk = bytes(self.incoming[:size])
        del self.incoming[:size]
        return chunk

    def write(self, data):
        self.writes.append(bytes(data))
        return len(data)

    def close(self):
        self.closed = True


def load_uart_recovery_module(monkeypatch, fake_serial):
    serial_module = types.ModuleType("serial")
    serial_module.Serial = lambda *args, **kwargs: fake_serial
    monkeypatch.setitem(sys.modules, "serial", serial_module)

    module_name = f"uart_recovery_test_{uuid.uuid4().hex}"
    spec = importlib.util.spec_from_file_location(module_name, UART_RECOVERY_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    monkeypatch.setattr(module.time, "sleep", lambda _: None)
    return module


def test_auth_success_sends_expected_digest(monkeypatch):
    secret = bytes(range(32))
    challenge = bytes(range(32, 64))
    fake_serial = FakeSerial(bytes([0xAA]) + challenge + bytes([0xAA]))
    uart_recovery = load_uart_recovery_module(monkeypatch, fake_serial)

    client = uart_recovery.RecoveryClient("dummy-port")

    assert client.authenticate(secret.hex()) is True
    assert fake_serial.writes[0] == struct.pack(
        "<BBHI", uart_recovery.CMD_AUTH, 0, 0, 0
    )
    assert fake_serial.writes[1] == hashlib.sha256(challenge + secret).digest()


def test_auth_rejects_invalid_secret_length(monkeypatch, capsys):
    fake_serial = FakeSerial()
    uart_recovery = load_uart_recovery_module(monkeypatch, fake_serial)

    client = uart_recovery.RecoveryClient("dummy-port")

    assert client.authenticate("00") is False
    assert fake_serial.writes == []
    assert "exactly 32 bytes" in capsys.readouterr().out


def test_log_decodes_readable_entries(monkeypatch, capsys):
    payload = b"".join(
        [
            struct.pack("<IIII", 100, 0x01, 0, 0),
            struct.pack("<IIII", 200, 0x21, 0xFF, 3),
        ]
    )
    header = bytes([0xAA]) + struct.pack("<H", 2)
    fake_serial = FakeSerial(header + payload)
    uart_recovery = load_uart_recovery_module(monkeypatch, fake_serial)

    client = uart_recovery.RecoveryClient("dummy-port")

    assert client.read_boot_log(0, 2) is True
    output = capsys.readouterr().out
    assert "BOOT_START" in output
    assert "AUTH_FAIL" in output
    assert "slot=A" in output
    assert "slot=NONE" in output


def test_log_rejects_incomplete_payload(monkeypatch, capsys):
    payload = struct.pack("<IIII", 100, 0x01, 0, 0)
    header = bytes([0xAA]) + struct.pack("<H", 2)
    fake_serial = FakeSerial(header + payload)
    uart_recovery = load_uart_recovery_module(monkeypatch, fake_serial)

    client = uart_recovery.RecoveryClient("dummy-port")

    assert client.read_boot_log(0, 2) is False
    assert "Incomplete log response" in capsys.readouterr().out
