"""Regression tests for the EBLDR_BOARD dispatch chain in CMakeLists.txt.

A duplicated dispatch chain once left a stray message(FATAL_ERROR ...) inside
the "kalimba" branch, so `cmake -DEBLDR_BOARD=kalimba` aborted configuration
for a supported board. These parse the chain statically, so no cmake or
cross-compiler is needed.
"""

import re
from pathlib import Path

CMAKELISTS = Path(__file__).resolve().parents[2] / "CMakeLists.txt"

BOARD_BRANCH_RE = re.compile(r'(?:if|elseif)\(EBLDR_BOARD STREQUAL "([^"]+)"\)')


def _read_dispatch_chain():
    text = CMAKELISTS.read_text(encoding="utf-8")
    start = text.index('if(EBLDR_BOARD STREQUAL "stm32f4")')
    end = text.index("endif()", start)
    return text[start:end]


def test_board_dispatch_has_no_duplicate_branches():
    chain = _read_dispatch_chain()
    boards = BOARD_BRANCH_RE.findall(chain)

    assert boards, "expected to find EBLDR_BOARD branches in CMakeLists.txt"

    seen = set()
    duplicates = set()
    for board in boards:
        if board in seen:
            duplicates.add(board)
        seen.add(board)

    assert not duplicates, (
        f"duplicate EBLDR_BOARD branches found: {sorted(duplicates)}"
    )


def test_board_dispatch_only_errors_in_final_else():
    chain = _read_dispatch_chain()

    fatal_error_count = chain.count("message(FATAL_ERROR")
    assert fatal_error_count == 1, (
        f"expected exactly one message(FATAL_ERROR ...) in the board dispatch "
        f"chain, found {fatal_error_count}"
    )

    else_index = chain.rindex("else()")
    fatal_error_index = chain.index("message(FATAL_ERROR")
    assert fatal_error_index > else_index, (
        "message(FATAL_ERROR ...) must live in the final else() branch"
    )


def test_kalimba_board_is_reachable():
    chain = _read_dispatch_chain()
    boards = BOARD_BRANCH_RE.findall(chain)
    assert boards.count("kalimba") == 1
