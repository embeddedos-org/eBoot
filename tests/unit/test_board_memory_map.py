# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EoS Project

"""tools/board_memory_map.py checks each port's memory-map numbers against
each other.

Every kind of finding is pinned here on small inputs, with the boundary next to
it that must NOT be a finding. The last tests hold the tree to the tool: the
findings on the real board ports are exactly the ones listed, with an issue, in
KNOWN.
"""

import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import board_memory_map as bmm  # noqa: E402
import board_ops_status as bos  # noqa: E402

# 1 MiB of flash at 0x08000000: bootloader, two slots, recovery, bootctl.
GOOD = {
    "flash_base": "0x08000000", "flash_size": "(1024 * 1024)",
    "bootloader_addr": "0x08000000", "bootloader_size": "0x10000",
    "slot_a_addr": "0x08010000", "slot_a_size": "0x60000",
    "slot_b_addr": "0x08070000", "slot_b_size": "0x60000",
    "recovery_addr": "0x080D0000", "recovery_size": "0x20000",
    "bootctl_addr": "0x080F0000", "bootctl_backup_addr": "0x080F8000",
}


def kinds(table, defined=None):
    return [(kind, subject)
            for kind, subject, _ in bmm.findings(table, defined or {})]


def changed(**fields):
    table = dict(GOOD)
    table.update(fields)
    return table


# ---- reading constants ---------------------------------------------------

def test_macros_are_read_with_continuations_and_without_function_likes():
    defined = bmm.macros(
        "#define A 0x10\n"
        "  #  define B  (A  *  2)\n"
        "#define LONG (1 + \\\n   2)\n"
        "#define F(x) ((x) + 1)\n"
        "#define EMPTY\n")
    assert defined == {"A": ("0x10",), "B": ("(A * 2)",),
                       "LONG": ("(1 + 2)",), "EMPTY": ("",)}


def test_a_repeated_identical_definition_is_one_definition():
    assert bmm.macros("#define A 4\n#define A   4\n") == {"A": ("4",)}


@pytest.mark.parametrize("expression, value", [
    ("0", 0),
    ("0x08000000", 0x08000000),
    ("0X1F", 31),
    ("010", 8),                                  # octal, as in C
    ("512UL", 512),
    ("(4ULL * 1024 * 1024 * 1024)", 1 << 32),
    ("(uint32_t)0x100 + 1", 0x101),
    ("(unsigned long)(2 << 10)", 2048),
    ("BASE + SIZE - 1", 0x08000000 + 4096 - 1),
    ("(BASE | 0x3) & 0xFF", 3),
    ("SIZE / 3", 1365),                          # integer division
    ("-1", -1),
])
def test_constant_expressions(expression, value):
    defined = bmm.macros("#define BASE 0x08000000u\n#define SIZE (4 * KB)\n"
                         "#define KB 1024\n")
    assert bmm.evaluate(expression, defined) == value


@pytest.mark.parametrize("expression, message", [
    ("UNKNOWN + 1", "UNKNOWN is not a constant"),
    ("sizeof(int)", "sizeof is not a constant"),
    ("TWICE", "TWICE has more than one definition: 1 | 2"),
    ("LOOP", "LOOP is defined in terms of itself"),
    ("1 ? 2 : 3", "not an integer constant expression"),
    ("(1 +", "not an integer constant expression"),
    ("", "not an integer constant expression"),
    ("1 / 0", "division by zero"),
    ("1 << 64", "shift by 64"),
    ("2 ** 3", "unsupported expression"),
    ("EMPTY", "not an integer constant expression"),
])
def test_what_is_not_a_constant_is_an_error(expression, message):
    defined = bmm.macros("#define TWICE 1\n#define TWICE 2\n"
                         "#define LOOP (LOOP + 1)\n#define EMPTY\n")
    with pytest.raises(ValueError, match=message.replace("|", r"\|")):
        bmm.evaluate(expression, defined)


def test_a_name_with_two_definitions_is_fine_until_it_is_needed():
    defined = bmm.macros("#define REG 1\n#define REG 2\n#define BASE 8\n")
    assert bmm.evaluate("BASE", defined) == 8


def test_board_macros_come_from_the_file_and_its_own_headers(tmp_path):
    board = tmp_path / "boards" / "demo"
    board.mkdir(parents=True)
    (board / "board_demo.h").write_text(
        "#define DEMO_BASE 0x1000 /* #define DEMO_BASE 0x2000 */\n",
        encoding="utf-8")
    (board / "unrelated.h").write_text("#define DEMO_BASE 0x3000\n",
                                       encoding="utf-8")
    (board / "board_demo.c").write_text(
        '#include "eos_hal.h"\n#include "board_demo.h"\n'
        "// #define DEMO_SIZE 1\n#define DEMO_SIZE 0x400\n", encoding="utf-8")
    assert bmm.board_macros(board / "board_demo.c") == {
        "DEMO_BASE": ("0x1000",), "DEMO_SIZE": ("0x400",)}


# ---- findings ------------------------------------------------------------

def test_a_coherent_map_has_no_findings():
    assert bmm.findings(GOOD, {}) == []


def test_a_port_that_names_no_memory_map_has_no_findings():
    assert bmm.findings({"flash_read": "NULL"}, {}) == []


def test_fields_the_table_does_not_name_are_not_checked():
    # no bootctl_addr at all: zero in C, "this port has none"
    table = {k: v for k, v in GOOD.items() if not k.startswith("bootctl")}
    assert bmm.findings(table, {}) == []


def test_an_initialiser_that_cannot_be_evaluated_is_reported_not_skipped():
    out = bmm.findings(changed(slot_a_addr="vendor_slot_base()"), {})
    assert [(k, s) for k, s, _ in out] == [("unevaluated", "slot_a_addr")]
    assert "vendor_slot_base" in out[0][2]


def test_a_value_wider_than_the_field_is_truncated():
    out = bmm.findings({"flash_base": "0",
                        "flash_size": "(4UL * 1024 * 1024 * 1024)"}, {})
    assert out == [("truncated", "flash_size",
                    "0x100000000 does not fit uint32_t; the port stores 0x0")]
    assert kinds({"flash_size": "0xFFFFFFFF"}) == []
    assert kinds({"flash_base": "-1"}) == [("truncated", "flash_base")]


def test_flash_may_end_exactly_at_4_gib_but_not_past_it():
    assert kinds({"flash_base": "0xFF000000", "flash_size": "0x01000000"}) == []
    assert kinds({"flash_base": "0xFF000000", "flash_size": "0x01000001"}) == [
        ("flash-wraps", "flash_size")]


def test_a_region_must_lie_inside_flash():
    table = {k: v for k, v in GOOD.items() if not k.startswith("bootctl")}
    table["recovery_size"] = "0x30000"          # ends where flash ends
    assert kinds(table) == []
    table["recovery_size"] = "0x30001"
    assert kinds(table) == [("outside-flash", "recovery")]
    assert ("outside-flash", "slot_a") in kinds(
        changed(slot_a_addr="0x07FFFFFF"))


def test_flash_size_zero_means_not_memory_mapped_and_skips_containment():
    table = changed(flash_base="0", flash_size="0")
    assert bmm.findings(table, {}) == []
    # ... but regions are still checked against each other
    assert kinds(changed(flash_size="0", slot_b_addr="0x0806FFFF")) == [
        ("overlap", "slot_a+slot_b")]


def test_a_region_without_a_size_is_not_a_region():
    table = changed(slot_b_addr="0x08010000")
    del table["slot_b_size"]
    assert bmm.findings(table, {}) == []


def test_regions_that_share_a_byte_overlap_and_adjacent_ones_do_not():
    assert kinds(changed(slot_a_size="0x60001")) == [
        ("overlap", "slot_a+slot_b")]
    assert kinds(changed(bootloader_size="0x10001")) == [
        ("overlap", "slot_a+bootloader")]
    assert kinds(changed(recovery_addr="0x08070000")) == [
        ("overlap", "slot_b+recovery")]


def test_a_block_inside_a_region_or_outside_flash_is_reported():
    assert kinds(changed(bootctl_addr="0x080CFFFF")) == [
        ("inside", "bootctl_addr")]
    assert kinds(changed(bootctl_backup_addr="0x08000000")) == [
        ("inside", "bootctl_backup_addr")]
    assert kinds(changed(log_addr="0x080EF000")) == [("inside", "log_addr")]
    assert kinds(changed(log_addr="0x080F4000")) == []
    assert kinds(changed(bootctl_addr="0x08100000")) == [
        ("outside-flash", "bootctl_addr")]
    assert kinds(changed(bootctl_addr="0x080FFFFF")) == []


def test_constants_are_resolved_through_the_board_macros():
    defined = bmm.macros("#define SLOT_SIZE (224 * 1024)\n"
                         "#define SLOT_B 0x48000\n")
    table = {"flash_base": "0", "flash_size": "(512 * 1024)",
             "slot_b_addr": "SLOT_B", "slot_b_size": "SLOT_SIZE",
             "recovery_addr": "0x7C000", "recovery_size": "(8 * 1024)",
             "bootctl_addr": "0x7E000"}
    assert kinds(table, defined) == [("overlap", "slot_b+recovery"),
                                     ("inside", "bootctl_addr")]


# ---- the tree ------------------------------------------------------------

BOARD = """
#include "board_%(name)s.h"
static const eos_board_ops_t %(name)s_ops = {
    .flash_base = BASE, .flash_size = SIZE,
    .slot_a_addr = BASE, .slot_a_size = SLOT,
    .slot_b_addr = BASE + SLOT, .slot_b_size = SLOT,
};
"""


def make_tree(tmp_path, boards):
    for name, header in boards.items():
        directory = tmp_path / "boards" / name
        directory.mkdir(parents=True)
        (directory / ("board_%s.h" % name)).write_text(header,
                                                       encoding="utf-8")
        (directory / ("board_%s.c" % name)).write_text(BOARD % {"name": name},
                                                       encoding="utf-8")
    return tmp_path


FITS = "#define BASE 0x1000\n#define SIZE 0x2000\n#define SLOT 0x1000\n"
SPILLS = "#define BASE 0x1000\n#define SIZE 0x1000\n#define SLOT 0x1000\n"


def test_collect_names_the_board_of_each_finding(tmp_path):
    root = make_tree(tmp_path, {"good": FITS, "bad": SPILLS})
    assert [f[:3] for f in bmm.collect(root)] == [
        ("bad", "outside-flash", "slot_b")]


def test_a_tree_without_boards_and_a_board_without_a_table_are_errors(tmp_path):
    with pytest.raises(ValueError, match="no boards"):
        bmm.collect(tmp_path)
    root = make_tree(tmp_path, {"good": FITS})
    (root / "boards" / "good" / "board_good.c").write_text("int x;\n",
                                                           encoding="utf-8")
    with pytest.raises(ValueError, match="boards/good/board_good.c: no "):
        bmm.collect(root)


def test_check_passes_only_when_findings_are_exactly_the_known_ones(
        tmp_path, capsys):
    root = make_tree(tmp_path, {"good": FITS, "bad": SPILLS})
    key = ("bad", "outside-flash", "slot_b")

    assert bmm.main(["--check"], root=root, known={}) == 1
    assert "(NEW)" in capsys.readouterr().out

    assert bmm.main(["--check"], root=root, known={key: "issue-1"}) == 0
    out = capsys.readouterr().out
    assert "(known: issue-1)" in out
    assert "1 finding(s): 1 known, 0 new; 0 stale KNOWN entries" in out

    # the port is fixed but its entry is left behind
    stale = {key: "issue-1", ("good", "overlap", "slot_a+slot_b"): "issue-2"}
    assert bmm.main(["--check"], root=root, known=stale) == 1
    assert "good: overlap: slot_a+slot_b is listed in KNOWN but no longer " \
           "occurs" in capsys.readouterr().out

    # without --check the tool reports and exits 0
    assert bmm.main([], root=root, known={}) == 0


def test_the_checked_fields_are_uint32_fields_of_the_ops_table():
    header = bos.strip_comments(
        (ROOT / bos.HAL_HEADER).read_text(encoding="utf-8"))
    for field in bmm.FIELDS:
        assert ("uint32_t %s;" % field) in header, field


def test_every_board_port_memory_map_is_coherent_or_tracked(capsys):
    """Fails on a board whose memory map contradicts itself and is not listed
    in KNOWN, and on a KNOWN entry whose port has been fixed. Run
    python3 tools/board_memory_map.py to see the findings."""
    assert bmm.main(["--check"]) == 0, capsys.readouterr().out


def test_every_known_finding_names_its_issue():
    for key, issue in bmm.KNOWN.items():
        assert issue.startswith(
            "https://github.com/embeddedos-org/eBoot/issues/"), key
