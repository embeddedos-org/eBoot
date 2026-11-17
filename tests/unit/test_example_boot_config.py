# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EoS Project

"""The config-generation check that CONTRIBUTING.md and AGENTS.md document.

Both tell contributors to run
`python scripts/generate_config.py configs/example_boot.yaml <dir>` and to
check that it writes the layout header and the linker fragment. This runs
that command on the file it names, so the documented check works.
"""

import re
import subprocess
import sys
from pathlib import Path

import pytest

yaml = pytest.importorskip("yaml")

REPO_ROOT = Path(__file__).resolve().parents[2]
EXAMPLE = "configs/example_boot.yaml"
BOARD_HEADER = "boards/stm32f4/board_stm32f4.h"

# example_boot.yaml region -> STM32F4_<NAME>_ADDR / _SIZE in BOARD_HEADER
BOARD_REGIONS = {
    "stage0": "STAGE0",
    "stage1": "STAGE1",
    "slot_a": "SLOT_A",
    "slot_b": "SLOT_B",
    "bootctl_primary": "BOOTCTL",
    "bootctl_backup": "BOOTCTL_BACKUP",
}


def _board_defines():
    """{NAME: value} for the STM32F4_* address and size defines."""
    text = (REPO_ROOT / BOARD_HEADER).read_text(encoding="utf-8")
    values = {}
    for name, expr in re.findall(
            r"^#define\s+STM32F4_(\w+)\s+(0x[0-9A-Fa-f]+|\(\s*\d+\s*\*\s*1024\s*\))",
            text, flags=re.M):
        if expr.startswith("0x"):
            values[name] = int(expr, 16)
        else:
            values[name] = int(re.search(r"\d+", expr).group(0)) * 1024
    return values


def _example():
    return yaml.safe_load((REPO_ROOT / EXAMPLE).read_text(encoding="utf-8"))["boot"]


def test_documented_command_names_this_file():
    for doc in ("CONTRIBUTING.md", "AGENTS.md"):
        text = (REPO_ROOT / doc).read_text(encoding="utf-8")
        assert "scripts/generate_config.py %s" % EXAMPLE in text, doc


def test_example_config_generates_header_and_linker_fragment(tmp_path):
    result = subprocess.run(
        [sys.executable, "scripts/generate_config.py", EXAMPLE, str(tmp_path)],
        cwd=REPO_ROOT, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    header = (tmp_path / "eboot_generated_layout.h").read_text()
    memory = (tmp_path / "eboot_generated_memory.ld").read_text()
    for region in ("STAGE0", "STAGE1", "BOOTCTL_PRIMARY", "BOOTCTL_BACKUP",
                   "SLOT_A", "SLOT_B"):
        assert "#define EBOOT_%s_ADDR" % region in header
        assert region.lower() in memory


def test_example_layout_fits_its_flash():
    boot = _example()
    regions = sorted((int(r["offset"], 16), r["size"])
                     for r in boot["layout"].values())
    end = 0
    for offset, size in regions:
        assert offset >= end, "regions overlap"
        end = offset + size
    assert end <= boot["flash_size"]


def test_example_layout_is_the_stm32f4_board_map():
    # The example says board: stm32f4, so its regions must be the ones the
    # board port uses. The F407 erases whole, non-uniform sectors, and the
    # board header gives every region its own sectors.
    boot = _example()
    board = _board_defines()
    assert boot["board"] == "stm32f4"
    base = int(boot["flash_base"], 16)
    assert base == board["FLASH_BASE"]
    assert boot["flash_size"] == board["FLASH_SIZE"]
    assert set(boot["layout"]) == set(BOARD_REGIONS)
    for region, name in BOARD_REGIONS.items():
        entry = boot["layout"][region]
        assert base + int(entry["offset"], 16) == board[name + "_ADDR"], region
        assert entry["size"] == board[name + "_SIZE"], region
