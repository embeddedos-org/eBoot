"""Regression tests for the stage-0 reset entry memory init.

Reset_Handler zeros .bss and then fills the free RAM above it with a
0xDEADBEEF canary for stack-depth analysis. The canary fill once started at
_sbss instead of _ebss, so on every reset it overwrote the whole of .bss --
including boot_log's log_head/log_initialized, board_registry's board_count
and slot_manager's slot table -- immediately after zeroing it.

stage0/ is only compiled by a cross build (CMakeLists.txt builds ebldr_stage0
only when EBLDR_BOARD selects a board and a toolchain file is given), so this
is a source-level guard in the style of test_cmake_board_dispatch.py: no
cross-compiler and no target hardware are needed to run it.
"""

import re
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
RESET_ENTRY = REPO_ROOT / "stage0" / "reset_entry.c"
STAGE0_LINKER_SCRIPTS = sorted(REPO_ROOT.glob("boards/*/*_stage0.ld"))


def _reset_handler_body():
    text = RESET_ENTRY.read_text(encoding="utf-8")
    start = text.index("void Reset_Handler(void)")
    end = text.index("\n}", start)
    return text[start:end]


def _strip_comments(text):
    return re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)


def test_canary_fill_starts_at_end_of_bss():
    """The canary fill must begin at _ebss, above the .bss it just zeroed."""
    body = _strip_comments(_reset_handler_body())

    match = re.search(r"stack_ptr\s*=\s*&\s*(_\w+)", body)
    assert match, "no canary fill pointer initialisation in Reset_Handler"

    assert match.group(1) == "_ebss", (
        f"the stack canary fill starts at {match.group(1)}; starting anywhere "
        f"at or below _sbss overwrites the .bss zeroed a few lines above with "
        f"0xDEADBEEF on every reset"
    )


def test_bss_is_zeroed_before_the_canary_fill():
    """Order matters: zeroing after the fill would erase the canary."""
    body = _strip_comments(_reset_handler_body())

    zero_loop = body.index("while (dst < &_ebss)")
    canary = body.index("0xDEADBEEF")

    assert zero_loop < canary, (
        "the .bss zero loop must run before the canary fill, otherwise the "
        "canary is erased and stack-depth analysis reads all zeroes"
    )


def test_stage0_linker_scripts_put_the_stack_above_bss():
    """The fix assumes .bss sits below the stack. Pin that assumption.

    If a future board script inverts the layout, filling upward from _ebss
    would run into whatever follows .bss instead of into free stack, and this
    test is where that shows up.
    """
    assert STAGE0_LINKER_SCRIPTS, "no stage-0 linker scripts found"

    for script in STAGE0_LINKER_SCRIPTS:
        text = script.read_text(encoding="utf-8")
        assert "_ebss" in text, f"{script.name} defines no _ebss"
        assert "_estack" in text, f"{script.name} defines no _estack"

        # _estack is set from the top of the RAM region, so the region name it
        # names must be the one .bss is placed into.
        estack = re.search(r"_estack\s*=\s*ORIGIN\((\w+)\)\s*\+\s*LENGTH\(\1\)", text)
        assert estack, (
            f"{script.name}: _estack is not defined as the top of a MEMORY "
            f"region, so 'stack above .bss' can no longer be checked here"
        )

        bss_region = re.search(r"_ebss\s*=\s*\.;\s*\}\s*>\s*(\w+)", text, re.DOTALL)
        assert bss_region, f"{script.name}: cannot find the region .bss is placed in"

        assert bss_region.group(1) == estack.group(1), (
            f"{script.name}: .bss is placed in {bss_region.group(1)} but "
            f"_estack is the top of {estack.group(1)}"
        )
