# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EoS Project

"""tools/board_ops_status.py reads what each board port's ops table provides.

The classification is the point: a port that registers a function which
discards its arguments and returns EOS_OK looks the same on disk as one that
programs flash, and the caller is told the operation happened. Every kind is
pinned here on a small C fixture, and the last tests hold the committed
docs/board-ops-status.{json,md} to what the tool produces from the tree.
"""

import json
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import board_ops_status as bos  # noqa: E402

HEADER = """
typedef struct {
    uint32_t flash_base;            /* not an operation */
    /* int (*commented_out)(void); */
    int (*flash_read)(uint32_t addr, void *buf, size_t len);
    int (*flash_write)(uint32_t addr, const void *buf, size_t len);
    int (*flash_erase)(uint32_t addr, size_t len);
    void (*watchdog_feed)(void);
    bool (*recovery_pin_asserted)(void);
    uint32_t (*get_tick_ms)(void);
    const char * (*name)(void);
} eos_board_ops_t;
"""

BOARD = """
// SPDX-License-Identifier: MIT
#include "eos_hal.h"
static uint32_t ticks;

static int demo_flash_read(uint32_t addr, void *buf, size_t len)
{
    memcpy(buf, (const void *)(uintptr_t)addr, len);   /* real work */
    return EOS_OK;
}

static int demo_flash_write(uint32_t addr, const void *buf, size_t len)
{
    /* In real impl: program the page */
    (void)addr; (void)buf; (void)len;
    return EOS_OK;
}

static int demo_flash_erase(uint32_t addr, size_t len)
{
    (void)addr; (void)len;
    return EOS_ERR_GENERIC;
}

static void demo_watchdog_feed(void) {}
static bool demo_recovery_pin(void) { return false; }
static uint32_t demo_get_tick_ms(void) { return ticks; }

static const eos_board_ops_t demo_ops = {
    .flash_base            = 0x08000000,
    .flash_read            = demo_flash_read,
    .flash_write           = &demo_flash_write,
    .flash_erase           = demo_flash_erase,
    .watchdog_feed         = demo_watchdog_feed,
    .recovery_pin_asserted = demo_recovery_pin,
    .get_tick_ms           = demo_get_tick_ms,
    .name                  = vendor_sdk_name,
};
"""

BARE = """
static const eos_board_ops_t bare_ops = {
    .flash_base  = 0,
    .flash_read  = NULL,
    .flash_write = 0
};
"""

# demo_flash_write and demo_ready are only CALLED here, each in front of a
# block. A call in that position has the shape of a definition.
CALLS = """
static int helper(uint32_t addr) { (void)addr; return EOS_OK; }

static int board_init(void)
{
    if (demo_flash_write(0, NULL, 0)) {
        return EOS_ERR_GENERIC;
    }
    while (demo_ready()) { }
    return EOS_OK;
}
"""


def make_tree(tmp_path, boards):
    (tmp_path / "include").mkdir()
    (tmp_path / "include" / "eos_hal.h").write_text(HEADER, encoding="utf-8")
    (tmp_path / "docs").mkdir()
    for name, text in boards.items():
        directory = tmp_path / "boards" / name
        directory.mkdir(parents=True)
        (directory / ("board_%s.c" % name)).write_text(text, encoding="utf-8")
    return tmp_path


# ---- reading the header -------------------------------------------------

def test_operations_are_the_function_pointers_in_declaration_order():
    assert bos.hal_operations(HEADER) == [
        ("flash_read", "int"), ("flash_write", "int"), ("flash_erase", "int"),
        ("watchdog_feed", "void"), ("recovery_pin_asserted", "bool"),
        ("get_tick_ms", "uint32_t"), ("name", "const char *"),
    ]


def test_a_header_without_the_struct_is_an_error():
    with pytest.raises(ValueError, match="eos_board_ops_t not found"):
        bos.hal_operations("typedef struct { int x; } other_t;")


def test_a_struct_without_function_pointers_is_an_error():
    with pytest.raises(ValueError, match="no function pointers"):
        bos.hal_operations("typedef struct { int x; } eos_board_ops_t;")


# ---- reading a board file -----------------------------------------------

def test_comment_stripping_keeps_line_numbers():
    text = "a /* one\ntwo */ b // three\nc"
    stripped = bos.strip_comments(text)
    assert stripped.count("\n") == text.count("\n")
    assert stripped.split() == ["a", "b", "c"]


def test_comment_markers_inside_a_string_are_not_comments():
    text = 'puts("http://x /* y */"); // real comment\n'
    stripped = bos.strip_comments(text)
    assert '"http://x /* y */"' in stripped
    assert "real comment" not in stripped
    assert len(stripped) == len(text)


def test_literal_contents_can_be_blanked_without_moving_anything():
    text = "a = '{'; s = \"}\\\"{\"; /* { */ b = '\\'';"
    blanked = bos.strip_comments(text, literals=True)
    assert len(blanked) == len(text)
    assert "{" not in blanked and "}" not in blanked
    assert blanked.startswith("a = ' '; s = \"")
    assert blanked.rstrip().endswith("b = '  ';")


def test_unbalanced_braces_are_an_error():
    with pytest.raises(ValueError, match="unbalanced"):
        bos.matching_brace("{ {", 0)


def test_ops_table_fields_are_read_without_comments():
    name, fields = bos.ops_table(BOARD)
    assert name == "demo_ops"
    assert fields["flash_write"] == "&demo_flash_write"
    assert fields["flash_base"] == "0x08000000"
    # the last field has no trailing comma in BARE
    assert bos.ops_table(BARE)[1] == {
        "flash_base": "0", "flash_read": "NULL", "flash_write": "0"}


def test_a_board_file_without_a_table_is_an_error():
    with pytest.raises(ValueError, match="no eos_board_ops_t table"):
        bos.ops_table("int unrelated;")


# ---- classifying function bodies ----------------------------------------

@pytest.mark.parametrize("body, return_type, kind", [
    ("memcpy(a, b, n); return EOS_OK;", "int", bos.CODE),
    ("if (x) { return EOS_OK; } return EOS_OK;", "int", bos.CODE),
    ("\n#ifdef REAL\n return EOS_OK;\n#endif\n", "int", bos.CODE),
    ("return ticks;", "uint32_t", bos.CODE),          # a variable, not a constant
    ("(void)a; (void)b; return EOS_OK;", "int", bos.OK_STUB),
    ("(void)(a); (void) ( b ); return (EOS_OK);", "int", bos.OK_STUB),
    ("return ((0));", "int", bos.OK_STUB),
    ("return (EOS_ERR_GENERIC);", "int", bos.ERR_STUB),
    ("return (a) + (b);", "int", bos.CODE),          # not one parenthesised value
    ("(void)f(a); return EOS_OK;", "int", bos.CODE),  # a call, result discarded
    ("return 0;", "int", bos.OK_STUB),
    ("(void)a; return EOS_ERR_GENERIC;", "int", bos.ERR_STUB),
    ("return -1;", "int", bos.ERR_STUB),
    ("return 7;", "int", bos.CONST),                  # neither success nor error
    ("", "void", bos.NO_OP),
    ("(void)timeout_ms;", "void", bos.NO_OP),
    ("return;", "void", bos.NO_OP),
    ("return false;", "bool", bos.CONST),
    ("return 0;", "uint32_t", bos.CONST),             # 0 ticks is not "success"
    ("return EOS_RESET_POWER_ON;", "eos_reset_reason_t", bos.CONST),
    ("return 0x10u;", "uint32_t", bos.CONST),
])
def test_body_kinds(body, return_type, kind):
    assert bos.classify_body(body, return_type) == kind


def test_a_non_void_function_that_returns_nothing_is_a_no_op():
    assert bos.classify_body("(void)a;", "int") == bos.NO_OP


# ---- classifying table entries ------------------------------------------

@pytest.mark.parametrize("initialiser", [None, "NULL", "0"])
def test_missing_or_null_entries_are_absent(initialiser):
    assert bos.classify(BOARD, initialiser, "int") == bos.ABSENT


def test_entries_of_the_fixture_board():
    _, fields = bos.ops_table(BOARD)
    kinds = {name: bos.classify(BOARD, fields.get(name), rtype)
             for name, rtype in bos.hal_operations(HEADER)}
    assert kinds == {
        "flash_read": bos.CODE,
        "flash_write": bos.OK_STUB,          # says it wrote, wrote nothing
        "flash_erase": bos.ERR_STUB,
        "watchdog_feed": bos.NO_OP,
        "recovery_pin_asserted": bos.CONST,
        "get_tick_ms": bos.CODE,
        "name": bos.EXTERN,                  # vendor_sdk_name is not in the file
    }


def test_an_initialiser_that_is_not_a_function_name_is_extern():
    assert bos.classify(BOARD, "(fn_t)0x1000", "int") == bos.EXTERN


def test_two_definitions_agree_or_count_as_code():
    same = ("#if A\nstatic int f(void) { return EOS_OK; }\n#else\n"
            "static int f(void) { return 0; }\n#endif\n")
    differ = ("#if A\nstatic int f(void) { return EOS_OK; }\n#else\n"
              "static int f(void) { write_page(); return EOS_OK; }\n#endif\n")
    assert bos.classify(same, "f", "int") == bos.OK_STUB
    assert bos.classify(differ, "f", "int") == bos.CODE


def test_a_prototype_is_not_a_definition():
    text = "static int f(void);\nstatic int g(void) { return f(); }\n"
    assert bos.function_bodies(text, "f") == []
    assert bos.classify(text, "f", "int") == bos.EXTERN


def test_a_call_followed_by_a_block_is_not_a_definition():
    assert bos.function_bodies(CALLS, "demo_flash_write") == []
    assert bos.function_bodies(CALLS, "demo_ready") == []
    # called here, defined somewhere else: extern, not "returns an error"
    assert bos.classify(CALLS, "demo_flash_write", "int") == bos.EXTERN
    # the definitions in the same text are still found
    assert bos.classify(CALLS, "helper", "int") == bos.OK_STUB
    assert bos.classify(CALLS, "board_init", "int") == bos.CODE


def test_a_call_site_does_not_change_the_kind_of_the_definition():
    text = CALLS + (
        "static int demo_flash_write(uint32_t a, const void *b, size_t n)\n"
        "{\n    (void)a; (void)b; (void)n;\n    return EOS_OK;\n}\n")
    # read as two bodies, the call's block made this stub look like code
    assert bos.function_bodies(text, "demo_flash_write") == [
        "\n    (void)a; (void)b; (void)n;\n    return EOS_OK;\n"]
    assert bos.classify(text, "demo_flash_write", "int") == bos.OK_STUB


def test_a_brace_in_a_literal_neither_ends_nor_hides_a_body():
    text = ("static void put(void) { uart_putc('{'); }\n"
            "static int f(void) { return EOS_OK; }\n"
            "static void say(void) { uart_puts(\"}\"); }\n"
            "static int g(void) { return EOS_OK; }\n")
    assert bos.function_bodies(text, "put") == [" uart_putc('{'); "]
    assert bos.function_bodies(text, "say") == [" uart_puts(\"}\"); "]
    assert bos.classify(text, "f", "int") == bos.OK_STUB
    assert bos.classify(text, "g", "int") == bos.OK_STUB


# ---- the whole document --------------------------------------------------

def test_collect_counts_every_board_and_operation(tmp_path):
    root = make_tree(tmp_path, {"demo": BOARD, "bare": BARE})
    status = bos.collect(root)
    assert [b["board"] for b in status["boards"]] == ["bare", "demo"]
    assert status["boards"][1]["file"] == "boards/demo/board_demo.c"
    assert status["boards"][1]["ops_table"] == "demo_ops"
    assert set(status["boards"][0]["operations"].values()) == {bos.ABSENT}
    assert status["summary"]["boards"] == 2
    assert status["summary"]["by_operation"]["flash_write"] == {
        bos.OK_STUB: 1, bos.ABSENT: 1}
    assert status["operations"][0] == "flash_read"


def test_a_tree_without_boards_is_an_error(tmp_path):
    root = make_tree(tmp_path, {})
    with pytest.raises(ValueError, match="no board ports"):
        bos.collect(root)


def test_errors_name_the_file_they_are_about(tmp_path):
    root = make_tree(tmp_path, {"demo": BOARD, "broken": "int unrelated;\n",
                                "open": BOARD + "static void f(void) {\n"})
    with pytest.raises(ValueError, match=(
            r"^boards/broken/board_broken\.c: no eos_board_ops_t table$")):
        bos.collect(root)
    (root / "boards" / "broken" / "board_broken.c").write_text(BARE, encoding="utf-8")
    # one brace too many anywhere in a file: refused, not guessed around
    with pytest.raises(ValueError, match=(
            r"^boards/open/board_open\.c: unbalanced braces: "
            r"\d+ '\{' and \d+ '\}'$")):
        bos.collect(root)
    (root / "include" / "eos_hal.h").write_text(
        "typedef struct { int x; } other_t;", encoding="utf-8")
    with pytest.raises(ValueError, match=(
            r"^include/eos_hal\.h: eos_board_ops_t not found$")):
        bos.collect(root)


def test_markdown_names_the_ports_that_report_success_without_working(tmp_path):
    status = bos.collect(make_tree(tmp_path, {"demo": BOARD, "bare": BARE}))
    text = bos.render_markdown(status)
    assert "| `flash_write` | demo |" in text
    assert "| `demo` | C | S! | E | 0 | K | C | X |" in text
    assert "| `bare` | – | – | – | – | – | – | – |" in text
    assert "not a supported board" in " ".join(text.split())
    # an absent operation is not always refused: the legend sends the reader
    # to the wrapper instead of promising an error
    assert "`hal/hal_core.c`" in text
    assert "returns an error or a default" not in text
    assert json.loads(bos.render_json(status)) == status


def test_markdown_says_none_when_no_operation_is_stubbed(tmp_path):
    status = bos.collect(make_tree(tmp_path, {"bare": BARE}))
    section = bos.render_markdown(status).split(
        "## Operations that report success without doing anything")[1]
    assert section.split("## Per board")[0].strip() == "None."


# ---- the command ---------------------------------------------------------

def test_write_then_check_then_drift(tmp_path, capsys):
    root = make_tree(tmp_path, {"demo": BOARD})
    assert bos.main(["--check"], root=root) == 1          # nothing written yet
    out = capsys.readouterr().out
    assert ("out of date: docs/board-ops-status.json "
            "(run python3 tools/board_ops_status.py)") in out

    assert bos.main([], root=root) == 0
    assert b"\r" not in (root / "docs" / "board-ops-status.md").read_bytes()
    assert "wrote docs/board-ops-status.md" in capsys.readouterr().out
    assert bos.main(["--check"], root=root) == 0
    assert capsys.readouterr().out == ""

    # the stub gains a body: the committed files no longer describe the tree
    board = root / "boards" / "demo" / "board_demo.c"
    board.write_text(BOARD.replace(
        "(void)addr; (void)buf; (void)len;\n    return EOS_OK;",
        "program_page(addr, buf, len);\n    return EOS_OK;"), encoding="utf-8")
    assert bos.main(["--check"], root=root) == 1
    out = capsys.readouterr().out
    assert "out of date: docs/board-ops-status.json" in out
    assert "out of date: docs/board-ops-status.md" in out


def test_the_committed_status_files_describe_this_tree(capsys):
    """Fails when a board port or eos_board_ops_t changes without the status
    files being regenerated: run python3 tools/board_ops_status.py and commit
    both."""
    assert bos.main(["--check"]) == 0, capsys.readouterr().out


def test_every_port_in_the_tree_is_classified_for_every_operation():
    status = bos.collect(ROOT)
    assert status["summary"]["boards"] == len(
        list(ROOT.glob("boards/*/board_*.c")))
    for board in status["boards"]:
        assert list(board["operations"]) == status["operations"]
        assert set(board["operations"].values()) <= set(bos.KINDS)


def test_no_port_reports_a_flash_operation_it_does_not_perform():
    """A flash stub that returns success tells bootctl and the updater that
    data was persisted when nothing was written (#179). A port without a
    driver leaves the entry NULL and the HAL refuses the operation."""
    status = bos.collect(ROOT)
    stubbed = sorted(
        "%s.%s" % (board["board"], name)
        for board in status["boards"]
        for name in ("flash_read", "flash_write", "flash_erase")
        if board["operations"][name] == bos.OK_STUB)
    assert stubbed == []
