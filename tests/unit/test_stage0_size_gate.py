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


def test_main_rejects_negative_min_free(tmp_path):
    (tmp_path / "ebldr_stage0.elf").write_bytes(b"\x7fELF")
    result = _run_gate(tmp_path, tmp_path / "ebldr_stage0.elf", -1)
    assert result.returncode == 2
    assert "--min-free must not be negative" in result.stderr


def test_main_reports_size_timeout(tmp_path, capsys, monkeypatch):
    (tmp_path / "ebldr_stage0.elf").write_bytes(b"\x7fELF")
    slow = tmp_path / "slow-size.sh"
    slow.write_text("#!/bin/sh\nsleep 2\n", encoding="utf-8")
    slow.chmod(0o755)
    monkeypatch.setattr(gate, "TIMEOUT_SECONDS", 0.1)
    rc = gate.main(["--elf", str(tmp_path / "ebldr_stage0.elf"),
                    "--size", str(slow), "--top", "0"])
    assert rc == 2
    assert "timed out" in capsys.readouterr().err


def test_main_warns_when_symbol_listing_fails(tmp_path):
    (tmp_path / "ebldr_stage0.elf").write_bytes(b"\x7fELF")
    result = subprocess.run(
        [sys.executable, str(TOOLS_DIR / "check_stage0_size.py"),
         "--elf", str(tmp_path / "ebldr_stage0.elf"),
         "--size", str(_fake_size(tmp_path)), "--nm", "false",
         "--min-free", "16"],
        capture_output=True, text=True,
        env={"FAKE_SIZE_OUTPUT": SIZE_OUTPUT, "PATH": "/usr/bin:/bin"})
    assert result.returncode == 0
    assert "symbol listing failed" in result.stderr


def test_budget_matches_the_linker_script():
    """The gate's budget is the region length, not a second copy."""
    script = REPO_ROOT / "boards" / "stm32f4" / "stm32f4_stage0.ld"
    match = re.search(r"FLASH \(rx\)\s*:\s*ORIGIN = 0x[0-9a-fA-F]+, "
                      r"LENGTH = (\d+)K", script.read_text(encoding="utf-8"))
    assert match, "stage-0 linker script has no FLASH LENGTH"
    assert gate.DEFAULT_BUDGET == int(match.group(1)) * 1024


def _build_arm_job():
    doc = yaml.safe_load(
        (REPO_ROOT / ".github" / "workflows" / "build.yml")
        .read_text(encoding="utf-8"))
    job = doc["jobs"]["build-arm"]
    assert job["name"] == "Cross-compile STM32F4"
    return job


def test_workflow_gates_stage0():
    """The STM32F4 job runs the gate with the measured minimum."""
    runs = [step.get("run", "") for step in _build_arm_job()["steps"]]
    gate_steps = [run for run in runs
                  if "check_stage0_size.py" in run
                  and "--explain-overflow" not in run]
    assert len(gate_steps) == 1, (
        "expected exactly one stage-0 gate step, found %d" % len(gate_steps))
    assert "--min-free 16" in gate_steps[0]
    assert "ebldr_stage0.elf" in gate_steps[0]


# The gate needs an ELF, so it cannot reach an image that overflows the
# region outright: that fails at link time and leaves nothing to
# measure, and CI would show only ld's error. These pin the other half.

LD_OVERFLOW = (
    "[100%] Linking C executable ebldr_stage0.elf\n"
    "/usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/bin/ld: "
    "ebldr_stage0.elf section `.text' will not fit in region `FLASH'\n"
    "/usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/bin/ld: "
    "region `FLASH' overflowed by 16 bytes\n"
    "collect2: error: ld returned 1 exit status\n")

LD_OVERFLOW_UNQUOTED = "ld: region FLASH overflowed by 240 bytes\n"


def test_find_overflow_reads_the_linker_figure():
    assert gate.find_overflow(LD_OVERFLOW) == ("FLASH", 16)


def test_find_overflow_tolerates_the_other_quoting_style():
    """ld's quoting varies between versions; the match must not."""
    assert gate.find_overflow(LD_OVERFLOW_UNQUOTED) == ("FLASH", 240)


def test_find_overflow_returns_none_for_an_unrelated_failure():
    assert gate.find_overflow("undefined reference to `memcpy'\n") is None


def test_explain_overflow_states_the_budget_and_what_must_come_out():
    found, message = gate.explain_overflow(LD_OVERFLOW, 16384, 16)
    assert found
    # 16 over the 16384-byte region, and 16 more wanted spare.
    assert "16400" in message and "16384" in message
    assert "32 bytes have to come out" in message
    assert "#207" in message


def test_explain_overflow_does_not_invent_one():
    found, message = gate.explain_overflow("ld: cannot find -lc\n", 16384, 16)
    assert not found and message is None


def test_main_explain_overflow_reports_and_exits_zero(tmp_path, capsys):
    """The failed build is what fails the job; this only explains it.

    A non-zero exit here would add a second failure, and is also how
    this step could start masking a different build error.
    """
    log = tmp_path / "build-arm.log"
    log.write_text(LD_OVERFLOW, encoding="utf-8")
    assert gate.main(["--explain-overflow", str(log), "--min-free", "16"]) == 0
    assert "overflowed its FLASH region by 16 bytes" in capsys.readouterr().out


def test_main_explain_overflow_is_quiet_about_other_failures(tmp_path, capsys):
    log = tmp_path / "build-arm.log"
    log.write_text("undefined reference to `foo'\n", encoding="utf-8")
    assert gate.main(["--explain-overflow", str(log)]) == 0
    assert "other than a stage-0 flash region overflow" in capsys.readouterr().out


def test_main_explain_overflow_survives_a_missing_log(tmp_path):
    missing = tmp_path / "nope.log"
    assert gate.main(["--explain-overflow", str(missing)]) == 0


def test_main_requires_an_elf_or_a_log():
    with pytest.raises(SystemExit) as excinfo:
        gate.main(["--min-free", "16"])
    assert excinfo.value.code == 2


def test_workflow_keeps_the_build_log_and_explains_an_overflow():
    """The diagnostic is useless if the log is not kept, and dangerous
    if it could pass a failed build."""
    steps = _build_arm_job()["steps"]
    by_name = {step.get("name", ""): step for step in steps}

    build = by_name["Build"]
    assert "tee build-arm.log" in build["run"], (
        "the Build step must keep its log for the overflow diagnostic")
    assert "set -o pipefail" in build["run"], (
        "piping into tee takes tee's status without pipefail, so a failed "
        "build would report success")
    assert build.get("continue-on-error") is not True

    explain = by_name["Explain a stage-0 overflow"]
    assert explain["if"] == "failure()", (
        "the diagnostic must run only on failure, and must not be the "
        "thing that decides the job")
    assert "--explain-overflow build-arm.log" in explain["run"]
    assert "--min-free 16" in explain["run"]
    assert explain.get("continue-on-error") is not True

    assert list(steps).index(build) < list(steps).index(explain)
