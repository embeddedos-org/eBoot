# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EoS Project

"""The stm32f4 stage-0 size gate (eBoot #207).

`ebldr_stage0.elf` links into a 16 KiB flash region. On master at #207
filing it had 12 bytes free under CI's toolchain, so a 16-byte change
failed to link with `region FLASH overflowed`. These tests pin the
gate that replaced the linker error with a clear message:

  * `tools/check_stage0_size.py` parses `arm-none-eabi-size` output
    and fails when the free space drops under the stated minimum;
  * the budget it gates on is the region length in the stage-0 linker
    script, not a second copy of the number;
  * the Cross-compile STM32F4 job in `.github/workflows/build.yml`
    actually runs it, with the minimum the landing measured.

No ARM toolchain is needed here: the script is driven with a fake
`size` that prints canned output, and the budget is read from the
linker script itself.
"""

import re
import subprocess
import sys
from pathlib import Path

import pytest
import yaml

REPO_ROOT = Path(__file__).resolve().parents[2]
TOOLS_DIR = REPO_ROOT / "tools"
sys.path.insert(0, str(TOOLS_DIR))

import check_stage0_size as gate  # noqa: E402

SIZE_OUTPUT = (
    "   text\t   data\t    bss\t    dec\t    hex\tfilename\n"
    "  16232\t      4\t    388\t  16624\t   40f0\tbuild-arm/ebldr_stage0.elf\n"
)


def test_parse_size_output():
    assert gate.parse_size_output(SIZE_OUTPUT) == (16232, 4)


def test_parse_size_output_rejects_garbage():
    with pytest.raises(ValueError):
        gate.parse_size_output("no size here\n")


def test_check_passes_with_room():
    free, ok, message = gate.check(16236, 16384, 16)
    assert (free, ok) == (148, True)
    assert "148 free" in message


def test_check_fails_under_the_minimum():
    free, ok, message = gate.check(16372, 16384, 16)
    assert (free, ok) == (12, False)
    assert "below the 16-byte minimum" in message


def test_check_fails_on_overflow():
    free, ok, message = gate.check(16400, 16384, 16)
    assert (free, ok) == (-16, False)
    assert "overflowed by 16" in message


def _fake_size(tmp_path):
    # A stand-in for arm-none-eabi-size: prints the canned output for a
    # file that exists, fails like the real tool for one that does not.
    script = tmp_path / "fake-size.sh"
    script.write_text(
        "#!/bin/sh\n"
        "test -f \"$1\" || { echo \"size: $1: No such file\" >&2; exit 1; }\n"
        "printf '%s' \"$FAKE_SIZE_OUTPUT\"\n",
        encoding="utf-8")
    script.chmod(0o755)
    return script


def _run_gate(tmp_path, elf, min_free, size_output=SIZE_OUTPUT):
    fake = _fake_size(tmp_path)
    env = {"FAKE_SIZE_OUTPUT": size_output, "PATH": "/usr/bin:/bin"}
    return subprocess.run(
        [sys.executable, str(TOOLS_DIR / "check_stage0_size.py"),
         "--elf", str(elf), "--size", str(fake), "--nm", "false",
         "--min-free", str(min_free), "--top", "0"],
        capture_output=True, text=True, env=env)


def test_main_passes(tmp_path):
    (tmp_path / "ebldr_stage0.elf").write_bytes(b"\x7fELF")
    result = _run_gate(tmp_path, tmp_path / "ebldr_stage0.elf", 16)
    assert result.returncode == 0
    assert "16236 / 16384 bytes (148 free" in result.stdout


def test_main_fails_under_the_minimum(tmp_path):
    (tmp_path / "ebldr_stage0.elf").write_bytes(b"\x7fELF")
    result = _run_gate(tmp_path, tmp_path / "ebldr_stage0.elf", 200)
    assert result.returncode == 1
    assert "::error::" in result.stderr
    assert "below the 200-byte minimum" in result.stderr


def test_main_missing_elf_is_a_tool_error(tmp_path):
    result = _run_gate(tmp_path, tmp_path / "no-such.elf", 16)
    assert result.returncode == 2
    assert "::error::" in result.stderr


def test_budget_matches_the_linker_script():
    """The gate's budget is the region length, not a second copy."""
    script = REPO_ROOT / "boards" / "stm32f4" / "stm32f4_stage0.ld"
    match = re.search(r"FLASH \(rx\)\s*:\s*ORIGIN = 0x[0-9a-fA-F]+, "
                      r"LENGTH = (\d+)K", script.read_text(encoding="utf-8"))
    assert match, "stage-0 linker script has no FLASH LENGTH"
    assert gate.DEFAULT_BUDGET == int(match.group(1)) * 1024


def test_workflow_gates_stage0():
    """The STM32F4 job runs the gate with the measured minimum."""
    doc = yaml.safe_load(
        (REPO_ROOT / ".github" / "workflows" / "build.yml")
        .read_text(encoding="utf-8"))
    job = doc["jobs"]["build-arm"]
    assert job["name"] == "Cross-compile STM32F4"
    runs = [step.get("run", "") for step in job["steps"]]
    gate_steps = [run for run in runs
                  if "check_stage0_size.py" in run]
    assert len(gate_steps) == 1, (
        "expected exactly one stage-0 gate step, found %d" % len(gate_steps))
    assert "--min-free 16" in gate_steps[0]
    assert "ebldr_stage0.elf" in gate_steps[0]
