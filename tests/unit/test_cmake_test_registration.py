"""Regression tests for the CTest registrations in tests/CMakeLists.txt.

Every C suite under tests/unit/ has to be named in tests/CMakeLists.txt to be
compiled and run at all. Nothing else notices when one is left out: the suite
stops building, ctest reports one fewer test, and the run still goes green.

That is how test_fw_transport.c -- the 12-case regression suite covering the
unbounded raw length prefix, the unbounded YMODEM block-0 filename scan and
the missing block-number validation -- stopped running. A merge replaced its
registration block instead of appending a new one, so the file stayed in the
tree while the target that built it disappeared.

These parse tests/CMakeLists.txt statically, so no cmake or compiler is needed.
"""

import re
from pathlib import Path

TESTS_DIR = Path(__file__).resolve().parent
CMAKELISTS = TESTS_DIR.parent / "CMakeLists.txt"

ADD_EXECUTABLE_RE = re.compile(r"add_executable\(\s*(\w+)\s+([^)]*?)\)", re.S)
ADD_TEST_RE = re.compile(r"add_test\(\s*NAME\s+(\w+)\s+COMMAND\s+(\w+)")


def _cmake_text():
    return CMAKELISTS.read_text(encoding="utf-8")


def _c_suites():
    """Every C suite file under tests/unit/, by file name."""
    return sorted(p.name for p in TESTS_DIR.glob("test_*.c"))


def _registered_sources():
    """Source file names named by an add_executable() in tests/CMakeLists.txt."""
    sources = set()
    for _target, source_list in ADD_EXECUTABLE_RE.findall(_cmake_text()):
        for source in source_list.split():
            sources.add(Path(source).name)
    return sources


def test_every_c_suite_is_built():
    suites = _c_suites()
    assert suites, "expected to find test_*.c suites in tests/unit/"

    registered = _registered_sources()
    missing = [name for name in suites if name not in registered]

    assert not missing, (
        "these suites exist under tests/unit/ but no add_executable() in "
        f"tests/CMakeLists.txt builds them, so they never run: {missing}"
    )


def test_every_built_suite_is_registered_with_ctest():
    text = _cmake_text()
    targets = {target for target, _sources in ADD_EXECUTABLE_RE.findall(text)}
    commands = {command for _name, command in ADD_TEST_RE.findall(text)}

    unregistered = sorted(targets - commands)
    assert not unregistered, (
        "these test executables are built but never added to ctest, so a "
        f"failure in them cannot fail the build: {unregistered}"
    )


def test_fw_transport_suite_is_registered():
    """Pin the specific suite that was dropped, by name."""
    assert "test_fw_transport.c" in _registered_sources(), (
        "test_fw_transport.c is not built by tests/CMakeLists.txt -- the UART "
        "transport regression suite would silently stop running again"
    )
