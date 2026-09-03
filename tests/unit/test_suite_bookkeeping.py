# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EoS Project
"""Two guards against counts and lists that are maintained by hand.

Both patterns below were found in review, one after another, and both have the
same shape: a number or a list that describes what the suite does, written out
separately from the thing it describes, with nothing checking the two agree.

  1. `main()` assigned `tests_run = <literal>` while the `TEST()` macro
     incremented only `tests_passed`. A test defined but never wired into
     `main()` was then skipped with a zero exit -- the suite reported
     "N/N passed" for an N that was a claim, not a count.

     Not hypothetical: `tests/unit/test_ed25519.c` carried a hardcoded 11 that
     masked one test called twice and two never called at all, and
     `tests/unit/test_tlv_auth.c` assigned `tests_run` twice in one function
     (8, then 7) with the stale value surviving only because the later
     assignment won.

  2. The Valgrind `foreach` named its suites again by hand, and had drifted to
     17 of 21 -- test_ecc, test_rollback, test_secure_boot and test_storage
     got no memory-safety run, and nothing failed when a name was forgotten.

These tests are cheap and need no C toolchain.
"""

import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
UNIT = REPO / "tests" / "unit"
CMAKE = REPO / "tests" / "CMakeLists.txt"

#: Suites with no TEST() macro -- they count differently or not at all. A name
#: may sit here only with a reason about the suite itself.
NO_TEST_MACRO = {
    "test_boot_log.c": "prints its own summary and has no TEST() macro",
    "test_ecc.c": "single-scenario suite; no per-test harness",
    "test_image_abi.c": "compile-time _Static_asserts; nothing runs per test",
}


def _suites():
    found = sorted(UNIT.glob("test_*.c"))
    assert found, f"no unit suites under {UNIT}"
    return found


def _macro_block(source):
    m = re.search(r"#define TEST\(name\)(?:[^\n]*\\\n)*[^\n]*\n", source)
    return m.group(0) if m else None


def test_no_suite_hardcodes_its_own_total():
    """`tests_run = <literal>` is a claim about the suite, not a measurement."""
    offenders = []
    for path in _suites():
        source = path.read_text(encoding="utf-8")
        for literal in re.findall(r"tests_run\s*=\s*([1-9]\d*)\s*;", source):
            offenders.append(f"{path.name}: tests_run = {literal}")
        # `printf("%d/12 tests passed")` is the same claim in another place.
        for literal in re.findall(r"%d\s*/\s*(\d+)\s*(?:tests )?passed", source):
            offenders.append(f"{path.name}: literal total {literal} in the summary")
    assert not offenders, (
        "these suites state a total instead of counting one; a test that is "
        "defined but never called is then invisible:\n  " + "\n  ".join(offenders)
    )


def test_every_test_macro_counts_the_test_it_runs():
    offenders = []
    for path in _suites():
        if path.name in NO_TEST_MACRO:
            continue
        block = _macro_block(path.read_text(encoding="utf-8"))
        if block is None:
            offenders.append(f"{path.name}: has no TEST() macro and is not in NO_TEST_MACRO")
        elif "tests_run++" not in block:
            offenders.append(f"{path.name}: TEST() does not increment tests_run")
    assert not offenders, "\n  ".join([""] + offenders)


def test_every_defined_test_is_actually_called():
    """A TEST() nobody calls is a test that silently does not run."""
    offenders = []
    for path in _suites():
        source = path.read_text(encoding="utf-8")
        defined = set(re.findall(r"^TEST\((\w+)\)", source, re.M))
        if not defined:
            continue
        called = set(re.findall(r"run_(\w+)\s*\(\s*\)\s*;", source))
        for name in sorted(defined - called):
            offenders.append(f"{path.name}: {name}() is defined but never called")
    assert not offenders, "\n  ".join([""] + offenders)


def test_the_valgrind_list_is_derived_not_repeated():
    """Every registered suite gets a Valgrind run, by construction."""
    cmake = CMAKE.read_text(encoding="utf-8")

    registered = re.findall(r"add_test\(NAME (\w+) COMMAND", cmake)
    appended = re.findall(r"list\(APPEND EBLDR_UNIT_TESTS (\w+)\)", cmake)

    assert "foreach(TEST_NAME ${EBLDR_UNIT_TESTS})" in cmake, (
        "the Valgrind foreach must iterate the accumulated list rather than "
        "name its suites again; a hand-written list drifts and nothing fails"
    )
    missing = sorted(set(registered) - set(appended))
    assert not missing, (
        f"these suites are registered with ctest but never appended to "
        f"EBLDR_UNIT_TESTS, so they get no Valgrind run: {missing}"
    )
