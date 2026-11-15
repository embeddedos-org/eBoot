#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EoS Project

"""What each board port's ops table actually provides.

Every board port registers a `static const eos_board_ops_t` table. A field left
out or set to NULL makes the HAL wrapper refuse the operation; a field pointing
at a function may still do nothing. This tool reads include/eos_hal.h for the
list of operations and every boards/*/board_*.c for what each port supplies,
and classifies each (board, operation) pair:

    absent    NULL or not named in the table: no port function is called
    code      a function whose body is not recognised as one of the stubs below
    ok-stub   a function that discards its arguments and returns success
    err-stub  a function that discards its arguments and returns an error
    no-op     a void function with an empty body
    const     a function that only returns a fixed non-status value
    extern    a function that is not defined in the board file

"code" means the port has code for the operation. It does not mean the code is
correct, and nothing recorded here has been run on hardware. A stub written in
a way this tool does not recognise is reported as "code" too.

"absent" says only that the port supplies nothing. What the caller then sees
is decided per operation by the wrapper in hal/hal_core.c, where there is
one, and is not recorded here: some wrappers return an error, some return a
default value, and for a void operation the caller gets no signal at all.

Usage:
    python3 tools/board_ops_status.py           rewrite docs/board-ops-status.{json,md}
    python3 tools/board_ops_status.py --check   exit 1 if they are out of date
"""

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HAL_HEADER = "include/eos_hal.h"
JSON_OUT = "docs/board-ops-status.json"
MD_OUT = "docs/board-ops-status.md"

ABSENT, CODE, OK_STUB, ERR_STUB, NO_OP, CONST, EXTERN = (
    "absent", "code", "ok-stub", "err-stub", "no-op", "const", "extern")
KINDS = (CODE, OK_STUB, ERR_STUB, NO_OP, CONST, EXTERN, ABSENT)
SYMBOL = {CODE: "C", OK_STUB: "S!", ERR_STUB: "E", NO_OP: "0", CONST: "K",
          EXTERN: "X", ABSENT: "–"}

_CONSTANT = re.compile(
    r"true|false|NULL|[A-Z][A-Z0-9_]*|-?\d+[uUlL]*|0[xX][0-9a-fA-F]+[uUlL]*")
_DISCARD = re.compile(r"\(\s*void\s*\)\s*(?:\w+|\(\s*\w+\s*\))")   # (void)a  (void)(a)
_SUCCESS = re.compile(r"EOS_OK|0")
_ERROR = re.compile(r"EOS_ERR\w*|-\d+")


_COMMENT_OR_LITERAL = re.compile(
    r"/\*.*?\*/|//[^\n]*"
    r"|\"(?:\\.|[^\"\\\n])*\""
    r"|'(?:\\.|[^'\\\n])*'", re.S)


def strip_comments(text, literals=False):
    """Blank out C comments and, with literals=True, the contents of string
    and character literals. Every character keeps its offset and every
    newline stays, so an index into the result is an index into `text`.

    Comments and literals are found in one pass: a comment marker inside a
    string is not a comment, and a quote inside a comment opens nothing."""
    def blank(match):
        token = match.group(0)
        if token[0] == "/":
            return re.sub(r"[^\n]", " ", token)
        if literals:
            return token[0] + " " * (len(token) - 2) + token[-1]
        return token
    return _COMMENT_OR_LITERAL.sub(blank, text)


def matching_brace(text, open_at):
    """Index of the brace that closes the one at open_at."""
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    raise ValueError("unbalanced braces")


def hal_operations(header_text):
    """[(name, return type)] for every function pointer in eos_board_ops_t,
    in declaration order."""
    text = strip_comments(header_text)
    end = text.find("} eos_board_ops_t;")
    if end < 0:
        raise ValueError("eos_board_ops_t not found")
    start = text.rfind("typedef struct", 0, end)
    members = re.findall(r"([A-Za-z_][\w \t\*]*?)\(\s*\*\s*(\w+)\s*\)\s*\(",
                         text[start:end])
    if not members:
        raise ValueError("eos_board_ops_t has no function pointers")
    return [(name, " ".join(rtype.split())) for rtype, name in members]


def ops_table(board_text):
    """(table name, {field: initialiser}) of the board's ops table."""
    text = strip_comments(board_text)
    decl = re.search(r"static\s+const\s+eos_board_ops_t\s+(\w+)\s*=\s*\{", text)
    if not decl:
        raise ValueError("no eos_board_ops_t table")
    open_at = decl.end() - 1
    body = text[open_at + 1:matching_brace(text, open_at)]
    fields = dict(re.findall(r"\.(\w+)\s*=\s*([^,]+?)\s*(?:,|$)", body))
    return decl.group(1), fields


def function_bodies(board_text, name):
    """Bodies of every file-scope definition of `name` (comments removed).

    `if (name(x)) {` has the shape of a definition. What tells the two apart
    is where they stand: a definition is outside every brace, a call never
    is. Braces are counted with literals blanked, so '{' or "}" in a string
    neither opens nor closes anything."""
    text = strip_comments(board_text)
    scan = strip_comments(board_text, literals=True)
    bodies = []
    for match in re.finditer(r"\b%s\s*\([^;{}]*\)\s*\{" % re.escape(name), scan):
        if scan.count("{", 0, match.start()) != scan.count("}", 0, match.start()):
            continue
        open_at = match.end() - 1
        bodies.append(text[open_at + 1:matching_brace(scan, open_at)])
    return bodies


def classify_body(body, return_type):
    """One of CODE, OK_STUB, ERR_STUB, NO_OP, CONST for a function body."""
    if "{" in body or "#" in body:
        return CODE
    returned = None
    for statement in (s.strip() for s in body.split(";")):
        if not statement or _DISCARD.fullmatch(statement):
            continue
        result = re.fullmatch(r"return\b\s*(.*)", statement, flags=re.S)
        if not result:
            return CODE
        returned = result.group(1).strip()
        while True:                     # return (EOS_OK);
            inner = re.fullmatch(r"\(\s*([^()]*|\(.*\))\s*\)", returned, flags=re.S)
            if not inner:
                break
            returned = inner.group(1).strip()
        if returned and not _CONSTANT.fullmatch(returned):
            return CODE
    if return_type == "void" or returned is None:
        return NO_OP
    if return_type == "int":
        if _SUCCESS.fullmatch(returned):
            return OK_STUB
        if _ERROR.fullmatch(returned):
            return ERR_STUB
    return CONST


def classify(board_text, initialiser, return_type):
    """Kind of one ops-table entry."""
    if initialiser is None or initialiser in ("NULL", "0"):
        return ABSENT
    name = initialiser.lstrip("&").strip()
    if not re.fullmatch(r"[A-Za-z_]\w*", name):
        return EXTERN
    kinds = {classify_body(body, return_type)
             for body in function_bodies(board_text, name)}
    if not kinds:
        return EXTERN
    return kinds.pop() if len(kinds) == 1 else CODE


def collect(root):
    """The whole status document as a dict."""
    try:
        operations = hal_operations(
            (root / HAL_HEADER).read_text(encoding="utf-8"))
    except ValueError as exc:
        raise ValueError("%s: %s" % (HAL_HEADER, exc)) from None
    boards = []
    for path in sorted(root.glob("boards/*/board_*.c")):
        rel = path.relative_to(root).as_posix()
        text = path.read_text(encoding="utf-8")
        try:
            # Definitions are told from calls by brace depth (function_bodies),
            # so a file whose braces do not pair up cannot be read reliably:
            # everything after the stray one would count as "not defined here".
            scan = strip_comments(text, literals=True)
            if scan.count("{") != scan.count("}"):
                raise ValueError("unbalanced braces: %d '{' and %d '}'"
                                 % (scan.count("{"), scan.count("}")))
            table, fields = ops_table(text)
            kinds = {name: classify(text, fields.get(name), rtype)
                     for name, rtype in operations}
        except ValueError as exc:
            raise ValueError("%s: %s" % (rel, exc)) from None
        boards.append({
            "board": path.parent.name,
            "file": rel,
            "ops_table": table,
            "operations": kinds,
        })
    if not boards:
        raise ValueError("no board ports found")
    by_operation = {}
    for name, _ in operations:
        counts = {}
        for board in boards:
            kind = board["operations"][name]
            counts[kind] = counts.get(kind, 0) + 1
        by_operation[name] = {k: counts[k] for k in KINDS if k in counts}
    return {
        "schema": 1,
        "generated_by": "tools/board_ops_status.py",
        "source": "boards/*/board_*.c against " + HAL_HEADER,
        "caveat": ("'code' means the port has code for the operation; it is "
                   "not a statement that the code is correct or has run on "
                   "hardware. 'absent' means the port supplies nothing; what "
                   "the caller sees instead is decided per operation by the "
                   "wrapper in hal/hal_core.c, where there is one, and is not "
                   "recorded here"),
        "operations": [name for name, _ in operations],
        "boards": boards,
        "summary": {"boards": len(boards), "by_operation": by_operation},
    }


def render_json(status):
    return json.dumps(status, indent=2, ensure_ascii=False) + "\n"


def _row(cells):
    return "| " + " | ".join(cells) + " |\n"


def render_markdown(status):
    ops = status["operations"]
    boards = status["boards"]
    out = [
        "# Board ops status\n\n",
        "<!-- Generated by tools/board_ops_status.py. Do not edit by hand:\n",
        "     run `python3 tools/board_ops_status.py` and commit both this\n",
        "     file and the .json. -->\n\n",
        "What each board port's `eos_board_ops_t` table provides, read from\n",
        "`boards/*/board_*.c` against `include/eos_hal.h`. The same data is in\n",
        "[`board-ops-status.json`](board-ops-status.json).\n\n",
        "**What this is not.** `C` means the port has code for the operation.\n",
        "It does not mean the code is correct, and nothing recorded here has\n",
        "been run on hardware. A board listed here is a port in the tree, not\n",
        "a supported board.\n\n",
        "## Legend\n\n",
        "| Mark | Kind | Meaning |\n|---|---|---|\n",
        "| `C` | code | a function whose body this tool does not recognise "
        "as one of the stubs below. Whether that body performs the operation "
        "is not checked |\n",
        "| `–` | absent | `NULL` or not named in the table: no port function "
        "is called. What the caller sees instead is decided per operation by "
        "the wrapper in `hal/hal_core.c`, where there is one, and is not "
        "recorded here — some wrappers return an error, some return a default "
        "value, and for a `void` operation the caller gets no signal at all "
        "|\n",
        "| `S!` | ok-stub | a function that discards its arguments and returns "
        "success — the caller is told the operation happened |\n",
        "| `E` | err-stub | a function that discards its arguments and returns "
        "an error |\n",
        "| `0` | no-op | a void function with an empty body |\n",
        "| `K` | const | a function that only returns a fixed non-status value "
        "|\n",
        "| `X` | extern | a function that is not defined in the board file |\n\n",
        "## Summary\n\n",
        "%d board ports.\n\n" % status["summary"]["boards"],
        _row(["Operation"] + ["`%s`" % SYMBOL[k] for k in KINDS]),
        _row(["---"] + ["---:"] * len(KINDS)),
    ]
    for name in ops:
        counts = status["summary"]["by_operation"][name]
        out.append(_row(["`%s`" % name] + [str(counts.get(k, 0)) for k in KINDS]))

    out.append("\n## Operations that report success without doing anything\n\n")
    stubbed = [(name, [b["board"] for b in boards
                       if b["operations"][name] == OK_STUB]) for name in ops]
    stubbed = [(name, names) for name, names in stubbed if names]
    if stubbed:
        out.append("| Operation | Ports |\n|---|---|\n")
        for name, names in stubbed:
            out.append(_row(["`%s`" % name, ", ".join(names)]))
    else:
        out.append("None.\n")

    out.append("\n## Per board\n\n")
    out.append("Columns are the operations in `eos_board_ops_t` order: "
               + ", ".join("%d `%s`" % (i + 1, name)
                           for i, name in enumerate(ops)) + ".\n\n")
    out.append(_row(["Board"] + [str(i + 1) for i in range(len(ops))]))
    out.append(_row(["---"] + [":-:"] * len(ops)))
    for board in boards:
        out.append(_row(["`%s`" % board["board"]]
                        + [SYMBOL[board["operations"][name]] for name in ops]))
    return "".join(out)


def main(argv=None, root=ROOT):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--check", action="store_true",
                        help="exit 1 if the committed files are out of date")
    args = parser.parse_args(argv)

    status = collect(root)
    wanted = {JSON_OUT: render_json(status), MD_OUT: render_markdown(status)}
    if args.check:
        stale = [rel for rel, text in wanted.items()
                 if not (root / rel).is_file()
                 or (root / rel).read_text(encoding="utf-8") != text]
        for rel in stale:
            print("out of date: %s (run python3 tools/board_ops_status.py)"
                  % rel)
        return 1 if stale else 0
    for rel, text in wanted.items():
        # open(), not Path.write_text(newline=): that argument needs 3.10
        with open(root / rel, "w", encoding="utf-8", newline="\n") as out:
            out.write(text)
        print("wrote %s" % rel)
    return 0


if __name__ == "__main__":  # pragma: no cover
    sys.exit(main())
