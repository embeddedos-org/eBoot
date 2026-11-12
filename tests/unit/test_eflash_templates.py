# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EoS Project

"""Tests for the command templates eflash.py fills in.

`eflash.py flash` filled {image}, {address}, {board}, {port} and {device}
into the board's flash_command only. The verify_command and reset_command
it runs afterwards were used as written, apart from {image} in the verify
command, so `flash --board esp32 --reset` ran
`esptool.py --chip esp32 --port {port} run` with a literal "{port}".
"""

import os
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
EFLASH = REPO_ROOT / "tools" / "eflash.py"

posix_only = pytest.mark.skipif(
    os.name != "posix", reason="uses a POSIX shell script as the stub tool")


def _run_flash(tmp_path, *extra):
    """Run `eflash.py flash --dry-run` with a stub esptool.py on PATH.

    The stub only satisfies the tool_check lookup; --dry-run means no
    command is executed, only printed.
    """
    bindir = tmp_path / "bin"
    bindir.mkdir()
    stub = bindir / "esptool.py"
    stub.write_text("#!/bin/sh\nexit 0\n")
    stub.chmod(0o755)
    image = tmp_path / "fw.bin"
    image.write_bytes(b"\xA5" * 64)

    env = dict(os.environ)
    env["PATH"] = f"{bindir}{os.pathsep}{env.get('PATH', '')}"
    return subprocess.run(
        [sys.executable, str(EFLASH), "flash", "--board", "esp32",
         "--image", str(image), "--dry-run", *extra],
        capture_output=True, text=True, env=env, cwd=tmp_path, timeout=60,
    )


def _dry_run_lines(stdout):
    return [line.strip() for line in stdout.splitlines()
            if line.strip().startswith("[dry-run]")]


@posix_only
def test_reset_command_gets_the_port(tmp_path):
    proc = _run_flash(tmp_path, "--port", "/dev/ttyUSB1", "--reset")
    assert proc.returncode == 0, proc.stdout + proc.stderr

    lines = _dry_run_lines(proc.stdout)
    assert len(lines) == 2, proc.stdout
    flash_line, reset_line = lines
    assert "--port /dev/ttyUSB1" in flash_line
    assert reset_line == "[dry-run] esptool.py --chip esp32 --port /dev/ttyUSB1 run"


@posix_only
def test_reset_command_gets_the_default_port(tmp_path):
    proc = _run_flash(tmp_path, "--reset")
    assert proc.returncode == 0, proc.stdout + proc.stderr

    lines = _dry_run_lines(proc.stdout)
    assert len(lines) == 2, proc.stdout
    for line in lines:
        assert "{" not in line, line
    # Whatever default the flash step picked, reset talks to the same port.
    flash_port = lines[0].split("--port ", 1)[1].split()[0]
    assert lines[1].endswith(f"--port {flash_port} run")
