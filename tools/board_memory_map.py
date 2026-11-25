#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EoS Project

"""Does each board port's memory map hold together?

Every board port fills the memory-map fields of its `eos_board_ops_t` table
(include/eos_hal.h) from constants: where flash is, where the two slots, the
recovery image, the boot-control block and the bootloader live. Nothing checked
those numbers against each other. This tool evaluates them for every
boards/*/board_*.c and reports:

    unevaluated   an initialiser this tool cannot turn into a number
    truncated     a value that does not fit the field (uint32_t); the port
                  stores the low 32 bits
    flash-wraps   flash_base + flash_size runs past the 4 GiB address space
    outside-flash a region or block that is not inside [flash_base,
                  flash_base + flash_size)
    overlap       two of slot A, slot B, recovery and bootloader share bytes,
                  or the boot-control block, its backup and the log are not
                  all at distinct addresses
    inside        the boot-control block, its backup or the log starts inside
                  one of those regions, or a region that has an address but no
                  size field starts inside a sized one (subject: block@region
                  or region@region, one finding per containing region)

Rules that keep the report honest:

  - Only fields the table names are checked. A field left out is zero in C and
    means "this port has none", which is not a finding.
  - Containment in flash is checked only when flash_size is non-zero. Ports
    whose storage is not memory-mapped set flash_size to 0 on purpose.
  - A region the table gives an address but no size field is checked by its
    start address alone: it must lie inside flash and outside every sized
    region, and two such regions must not start at the same address. A size
    written as 0 means "none".
  - Values are evaluated as plain integers, not with the target's integer
    widths, and casts are dropped rather than applied. The comparison with the
    field width is exact, but an expression that overflows inside a 32-bit
    `int` on the target is not detected.
  - Read from each board file: the first ops table, designated initialisers,
    and object-like macros of the file and of the headers it includes from its
    own directory. Conditional compilation is not followed.
  - A clean report says the numbers are consistent with each other. It does not
    say they match the hardware, a linker script or a flashed device.

Known findings are listed in KNOWN with the issue that tracks each. `--check`
fails on a finding that is not listed, and on a listed one that no longer
occurs, so the list can neither hide a new defect nor go stale.

Usage:
    python3 tools/board_memory_map.py           print every finding
    python3 tools/board_memory_map.py --check   exit 1 unless findings == KNOWN
"""

import argparse
import ast
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import board_ops_status as bos  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
FIELD_MAX = 0xFFFFFFFF

# name -> (address field, size field)
REGIONS = {
    "slot_a": ("slot_a_addr", "slot_a_size"),
    "slot_b": ("slot_b_addr", "slot_b_size"),
    "recovery": ("recovery_addr", "recovery_size"),
    "bootloader": ("bootloader_addr", "bootloader_size"),
}
BLOCKS = ("bootctl_addr", "bootctl_backup_addr", "log_addr")
FIELDS = ("flash_base", "flash_size") + tuple(
    name for pair in REGIONS.values() for name in pair) + BLOCKS

# (board, kind, subject) -> the issue that tracks it. Remove the entry in the
# change that fixes the port; --check fails if it is left behind.
#
# Empty, and meant to stay that way. The twelve ports this table was created
# for were fixed by #195, #196, #197 and #199; all 83 ports are now coherent
# and `--check` passes on zero findings. A new entry here is a defect someone
# chose to leave in the tree, so it needs an issue that says why it cannot be
# fixed now -- not a place to park a finding.
_ISSUES = "https://github.com/embeddedos-org/eBoot/issues/"
KNOWN = {}

_DEFINE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(\w+)(\(?)[ \t]*(.*)$", re.M)
_INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]+"([^"]+)"', re.M)
_NUMBER = re.compile(r"\b(0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*\b")
_WORD = re.compile(r"\b[A-Za-z_]\w*\b")
_CAST = re.compile(
    r"\(\s*(?:u?int(?:8|16|32|64|ptr)_t|size_t|(?:unsigned\s+)?(?:long\s+)?"
    r"(?:long|int)|unsigned)\s*\)")
_BINARY = {
    ast.Add: lambda a, b: a + b, ast.Sub: lambda a, b: a - b,
    ast.Mult: lambda a, b: a * b, ast.FloorDiv: lambda a, b: a // b,
    ast.LShift: lambda a, b: a << b, ast.RShift: lambda a, b: a >> b,
    ast.BitOr: lambda a, b: a | b, ast.BitAnd: lambda a, b: a & b,
}


def macros(text):
    """Object-like #defines of one comment-free source text: name -> tuple of
    the distinct bodies it is given, in order. Conditional compilation is not
    followed, so a name defined differently under #if has several bodies;
    evaluate() refuses such a name rather than pick one."""
    found = {}
    # A backslash-newline is deleted, as the C preprocessor does: it can
    # split a token ("0x10\<newline>00" is 0x1000), so no space goes in.
    spliced = text.replace("\\\r\n", "").replace("\\\n", "")
    for match in _DEFINE.finditer(spliced):
        name, paren, body = match.group(1), match.group(2), match.group(3)
        if paren:                       # function-like: #define F(x) ...
            continue
        body = " ".join(body.split())
        if body not in found.setdefault(name, ()):
            found[name] += (body,)
    return found


def board_macros(board_file):
    """Macros of a board file and of the headers it includes from its own
    directory."""
    text = bos.strip_comments(board_file.read_text(encoding="utf-8",
                                                   errors="replace"))
    texts = [text]
    for header in _INCLUDE.findall(text):
        path = board_file.parent / header
        if path.is_file():
            texts.append(bos.strip_comments(
                path.read_text(encoding="utf-8", errors="replace")))
    return macros("\n".join(texts))


def _walk(node):
    if isinstance(node, ast.Expression):
        return _walk(node.body)
    if isinstance(node, ast.Constant) and type(node.value) is int:
        return node.value
    if isinstance(node, ast.BinOp) and type(node.op) in _BINARY:
        left, right = _walk(node.left), _walk(node.right)
        if isinstance(node.op, ast.FloorDiv):
            if right == 0:
                raise ValueError("division by zero")
            quotient = abs(left) // abs(right)      # C truncates towards 0
            return quotient if (left < 0) == (right < 0) else -quotient
        if isinstance(node.op, (ast.LShift, ast.RShift)) \
                and not 0 <= right < 64:
            raise ValueError("shift by %d" % right)
        return _BINARY[type(node.op)](left, right)
    if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
        return -_walk(node.operand)
    if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.UAdd):
        return _walk(node.operand)
    raise ValueError("unsupported expression")


def expand(expression, defined, _seen=()):
    """`expression` with every macro name replaced by its body, textually and
    without added parentheses, as the preprocessor does: with A defined as
    `1 + 1`, `A * 2` is `1 + 1 * 2`."""
    def name(match):
        word = match.group(0)
        if word in _seen:
            raise ValueError("%s is defined in terms of itself" % word)
        if word not in defined:
            raise ValueError("%s is not a constant this tool can see" % word)
        if len(defined[word]) > 1:
            raise ValueError("%s has more than one definition: %s"
                             % (word, " | ".join(defined[word])))
        return " %s " % expand(defined[word][0], defined, _seen + (word,))

    return _WORD.sub(name, _CAST.sub(" ", expression))


def evaluate(expression, defined):
    """The integer a C constant expression stands for, with macro names
    resolved from `defined` (the mapping macros() returns). Integer literals,
    + - * / << >> | &, parentheses and casts to integer types; anything else is
    a ValueError. A cast is dropped, not applied."""
    def number(match):
        digits = match.group(1)
        if len(digits) > 1 and digits[0] == "0" and digits[1] not in "xX":
            return str(int(digits, 8))
        return str(int(digits, 0))

    refused = ValueError("'%s' is not an integer constant expression"
                         % " ".join(expression.split()))
    try:
        text = _NUMBER.sub(number, expand(expression, defined))
        if not re.fullmatch(r"[0-9\s()+\-*/<>|&]+", text):
            raise refused
        return _walk(ast.parse(text.strip().replace("/", "//"), mode="eval"))
    except (SyntaxError, RecursionError, MemoryError):
        raise refused from None


def _intersect(a, b):
    return a[0] < b[0] + b[1] and b[0] < a[0] + a[1]


def findings(initialisers, defined):
    """Findings for one port as (kind, subject, detail), in a fixed order.
    The subject is a field or region name, `first+second` for an overlap and
    `block@region` for a block inside a region, and `region@region` for a
    region without a size field that starts inside a sized one.
    `initialisers` is the ops table as field -> initialiser text."""
    out, value = [], {}
    for field in FIELDS:
        if field not in initialisers:
            continue
        try:
            number = evaluate(initialisers[field], defined)
        except ValueError as error:
            out.append(("unevaluated", field, str(error)))
            continue
        if not 0 <= number <= FIELD_MAX:
            out.append(("truncated", field,
                        "%#x does not fit uint32_t; the port stores %#x"
                        % (number, number & FIELD_MAX)))
            continue
        value[field] = number

    flash = None
    if value.get("flash_size"):
        flash = (value.get("flash_base", 0), value["flash_size"])
        if flash[0] + flash[1] > FIELD_MAX + 1:
            out.append(("flash-wraps", "flash_size",
                        "flash [%#x, +%#x) ends %#x bytes past 4 GiB"
                        % (flash[0], flash[1],
                           flash[0] + flash[1] - FIELD_MAX - 1)))

    regions = {}
    for name, (addr, size) in REGIONS.items():
        if addr in value and value.get(size):
            regions[name] = (value[addr], value[size])
    for name, region in regions.items():
        if flash and not (flash[0] <= region[0]
                          and region[0] + region[1] <= flash[0] + flash[1]):
            out.append(("outside-flash", name,
                        "%s [%#x, +%#x) is not inside flash [%#x, +%#x)"
                        % ((name,) + region + flash)))
    starts = {}
    for name, (addr, size) in REGIONS.items():
        if addr not in value or name in regions or size in value:
            continue                    # sized, or explicitly size 0
        starts[name] = value[addr]
        if flash and not flash[0] <= value[addr] < flash[0] + flash[1]:
            out.append(("outside-flash", name,
                        "%s starts at %#x, not inside flash [%#x, +%#x)"
                        % ((name, value[addr]) + flash)))
    names = list(starts)
    for i, first in enumerate(names):
        for second in names[i + 1:]:
            if starts[first] == starts[second]:
                out.append(("overlap", "%s+%s" % (first, second),
                            "%s and %s both start at %#x"
                            % (first, second, starts[first])))
    names = list(regions)
    for i, first in enumerate(names):
        for second in names[i + 1:]:
            if _intersect(regions[first], regions[second]):
                out.append(("overlap", "%s+%s" % (first, second),
                            "%s [%#x, +%#x) and %s [%#x, +%#x) share bytes"
                            % ((first,) + regions[first]
                               + (second,) + regions[second])))
    for start, at in starts.items():
        for name, region in regions.items():
            if region[0] <= at < region[0] + region[1]:
                out.append(("inside", "%s@%s" % (start, name),
                            "%s starts at %#x, inside %s [%#x, +%#x)"
                            % ((start, at, name) + region)))
    present = [block for block in BLOCKS if block in value]
    for i, first in enumerate(present):
        for second in present[i + 1:]:
            if value[first] == value[second]:
                out.append(("overlap", "%s+%s" % (first, second),
                            "%s and %s are both at %#x, so one is not a "
                            "separate copy of the other"
                            % (first, second, value[first])))
    for block in present:
        at = value[block]
        if flash and not flash[0] <= at < flash[0] + flash[1]:
            out.append(("outside-flash", block,
                        "%s %#x is not inside flash [%#x, +%#x)"
                        % ((block, at) + flash)))
        for name, region in regions.items():
            if region[0] <= at < region[0] + region[1]:
                # The region is part of the subject: one block can lie in
                # two regions, and one KNOWN entry must not cover both.
                out.append(("inside", "%s@%s" % (block, name),
                            "%s %#x is inside %s [%#x, +%#x)"
                            % ((block, at, name) + region)))
    return out


def collect(root):
    """Every finding in the tree as (board, kind, subject, detail)."""
    boards = sorted((root / "boards").glob("*/board_*.c"))
    if not boards:
        raise ValueError("no boards/*/board_*.c under %s" % root)
    out = []
    for board_file in boards:
        board = board_file.parent.name
        try:
            text = bos.strip_comments(board_file.read_text(
                encoding="utf-8", errors="replace"))
            _, initialisers = bos.ops_table(text)
            defined = board_macros(board_file)
        except ValueError as error:
            raise ValueError("%s: %s" % (
                board_file.relative_to(root).as_posix(), error)) from None
        out.extend((board,) + finding
                   for finding in findings(initialisers, defined))
    return out


def main(argv=None, root=ROOT, known=None):
    known = KNOWN if known is None else known
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--check", action="store_true",
                        help="exit 1 unless the findings are exactly KNOWN")
    args = parser.parse_args(argv)

    try:
        found = collect(root)
    except ValueError as error:
        print("error: %s" % error)
        return 2
    new = 0
    for board, kind, subject, detail in found:
        issue = known.get((board, kind, subject))
        new += issue is None
        print("%s: %s: %s (%s)" % (board, kind, detail,
                                   "known: %s" % issue if issue else "NEW"))
    seen = {finding[:3] for finding in found}
    stale = sorted(key for key in known if key not in seen)
    for board, kind, subject in stale:
        print("%s: %s: %s is listed in KNOWN but no longer occurs; remove it"
              % (board, kind, subject))
    print("%d finding(s): %d known, %d new; %d stale KNOWN entr%s"
          % (len(found), len(found) - new, new, len(stale),
             "y" if len(stale) == 1 else "ies"))
    if args.check:
        return 1 if new or stale else 0
    return 0


if __name__ == "__main__":  # pragma: no cover
    sys.exit(main())
