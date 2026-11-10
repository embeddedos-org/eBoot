# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EoS Project

"""A build option that reaches the compiler must be read by something.

CMakeLists.txt offered EBLDR_REQUIRE_SIGNATURES and EBLDR_RECOVERY_AUTH as
options, forwarded each as a compile definition, and the README listed both
as ON by default. No source file tested either macro, so a build with the
option OFF was byte-for-byte the build with it ON: the switch documented a
capability the tree did not have. docs/book/book.md went further and listed
four options that were never in CMakeLists.txt at all.

Two source-level rules keep that from coming back:

1. Every EBLDR_* name that CMakeLists.txt turns into a compile definition is
   referenced by a preprocessor line in at least one C or header file.
2. Every EBLDR_* name a documentation table presents as a build option is
   declared by CMakeLists.txt (option() or a CACHE set()).
"""

import re
import shlex

import pytest
import yaml
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
CMAKE = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")

SOURCE_DIRS = ("core", "hal", "include", "stage0", "stage1", "boards", "tests")
DOC_TABLES = ("README.md", "docs/book/book.md")


def _strip_cmake_comments(text):
    return "\n".join(line.split("#", 1)[0] for line in text.splitlines())


def _declared_options():
    text = _strip_cmake_comments(CMAKE)
    names = set(re.findall(r"\boption\(\s*(EBLDR_[A-Z0-9_]+)", text))
    names |= set(re.findall(r"\bset\(\s*(EBLDR_[A-Z0-9_]+)\b[^)]*\bCACHE\b", text))
    return names


def _forwarded_definitions(text=None):
    """Every EBLDR_ name CMakeLists.txt forwards to the compiler, by any of the
    three ordinary spellings: add_compile_definitions(EBLDR_X),
    add_compile_definitions(EBLDR_X=1), and target_compile_definitions(<tgt>
    <scope> EBLDR_X). The first version of this matched only the first shape,
    with the closing paren required to follow the name, so the other two
    re-introduced a dead switch without the guard noticing."""
    if text is None:
        text = _strip_cmake_comments(CMAKE)
    names = set()
    for args in re.findall(r"(?:add|target)_compile_definitions\(([^)]*)\)", text):
        for tok in re.findall(r"\bEBLDR_[A-Z0-9_]+(?:=[^\s)]*)?", args):
            names.add(tok.split("=", 1)[0])
    return names


def _preprocessor_references():
    refs = set()
    for d in SOURCE_DIRS:
        for path in (REPO_ROOT / d).rglob("*"):
            if path.suffix not in (".c", ".h"):
                continue
            for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
                if line.lstrip().startswith("#"):
                    refs.update(re.findall(r"\bEBLDR_[A-Z0-9_]+\b", line))
    return refs


def _documented_options(rel):
    names = set()
    for line in (REPO_ROOT / rel).read_text(encoding="utf-8").splitlines():
        if line.startswith("|"):
            first_cell = line.split("|")[1]
            names.update(re.findall(r"\bEBLDR_[A-Z0-9_]+\b", first_cell))
    return names


def test_cmake_declares_the_options_this_guard_relies_on():
    declared = _declared_options()
    assert {"EBLDR_BOARD", "EBLDR_BUILD_TESTS", "EBLDR_VERIFY_STAGE1"} <= declared, declared


def test_every_forwarded_definition_is_read_by_a_source_file():
    unread = sorted(_forwarded_definitions() - _preprocessor_references())
    assert not unread, (
        "CMakeLists.txt forwards these as compile definitions, but no C or "
        "header file under %s tests them, so the option changes nothing: %s"
        % (", ".join(SOURCE_DIRS), unread))


# The three shapes a compile definition is forwarded in. The guard once matched
# only the first; the reviewer demonstrated the other two passing it with a dead
# name in place. Each is asserted to be collected, with the =value stripped.
FORWARD_SHAPES = [
    ("add_compile_definitions(EBLDR_DEAD_A)", "EBLDR_DEAD_A"),
    ("add_compile_definitions(EBLDR_DEAD_B=1)", "EBLDR_DEAD_B"),
    ("target_compile_definitions(eboot_core PRIVATE EBLDR_DEAD_C)", "EBLDR_DEAD_C"),
    ("target_compile_definitions(eboot_core PUBLIC EBLDR_DEAD_D=0 OTHER=1)", "EBLDR_DEAD_D"),
    ("add_compile_definitions(\n    EBLDR_DEAD_E=1\n    EBLDR_DEAD_F\n)", "EBLDR_DEAD_E"),
]


@pytest.mark.parametrize("snippet, name", FORWARD_SHAPES, ids=[s[1] for s in FORWARD_SHAPES])
def test_every_way_of_forwarding_a_definition_is_seen(snippet, name):
    assert name in _forwarded_definitions(snippet)


def test_a_forwarded_definition_nothing_reads_is_caught_in_every_shape():
    """The guard is only worth having if each shape, with a name no source
    references, fails it. Run against the real tree plus one appended line,
    which is exactly how a dead switch would come back."""
    real = _strip_cmake_comments(CMAKE)
    refs = _preprocessor_references()
    for snippet, name in FORWARD_SHAPES:
        unread = _forwarded_definitions(real + "\n" + snippet) - refs
        assert name in unread, f"{snippet!r} forwarded a name nothing reads and the guard missed it"


def test_every_declared_option_is_documented():
    """test_documented_options_exist checks documented <= declared. This is the
    other direction: an option that exists and is documented nowhere is a
    switch a user cannot know about. Clean today, enumerated by the reviewer."""
    declared = _declared_options()
    documented = set()
    for rel in DOC_TABLES:
        documented |= _documented_options(rel)
    # EBLDR_BOARD is covered in README prose above the table, not the table itself.
    undocumented = sorted(declared - documented - {"EBLDR_BOARD"})
    assert not undocumented, "declared in CMakeLists.txt but in no options table: %s" % undocumented


def test_documented_options_exist():
    declared = _declared_options()
    for rel in DOC_TABLES:
        phantom = sorted(_documented_options(rel) - declared)
        assert not phantom, "%s lists build options CMakeLists.txt does not declare: %s" % (rel, phantom)


def test_no_option_offers_to_skip_verification_or_authentication():
    # These were removed because nothing read them. Re-adding one as a real
    # switch would add a way to build a bootloader that does not verify, which
    # is a design change and not a configuration.
    declared = _declared_options()
    assert "EBLDR_REQUIRE_SIGNATURES" not in declared
    assert "EBLDR_RECOVERY_AUTH" not in declared


# ── Rule 3: a -D flag a workflow passes to CMake names a variable that exists ──
#
# CMake does not fail on a -D name the project never reads: it configures,
# prints "Manually-specified variables were not used by the project" as a
# warning, and the flag does nothing. ci.yml's test job once passed
# -DBUILD_TESTS=ON for EBLDR_BUILD_TESTS and ran zero tests (TASKS.md T-004,
# #41); the tag-only `release` job kept the same misspelling, as
# -DBUILD_TESTS=OFF, after that fix.
#
# Scope: the `run:` scripts of the workflows under .github/workflows/. Only
# -D arguments of a `cmake` configure command count (`-DNAME=v`,
# `-DNAME:TYPE=v` and the spaced `-D NAME=v`); `cmake --build`, other
# programs and a -D inside a quoted flags string such as CMAKE_C_FLAGS are
# not CMake cache variables. Variables that CMake or a standard module owns
# are not the project's to declare and are skipped (NON_PROJECT_PREFIXES).
# ci/ci_security.yml is not a workflow GitHub runs and is not scanned.

WORKFLOWS = REPO_ROOT / ".github" / "workflows"
NON_PROJECT_PREFIXES = ("CMAKE_", "FETCHCONTENT_", "Python3_", "IDF_")
_NOT_CONFIGURE = {"--build", "--install", "-E", "-P", "--find-package", "--help", "--version"}


def _strip_shell_comment(line):
    """Drop a full-line comment, or a trailing one that starts at a # after
    whitespace, the way the shell would read it."""
    if line.lstrip().startswith("#"):
        return ""
    return re.split(r"\s#", line, maxsplit=1)[0]


# Words that can come before the program a simple command runs. A configure
# written as `then cmake ...`, `env CC=clang cmake ...` or `sudo cmake ...`
# is still a configure, so these are dropped before argv[0] is tested.
_LEADING_KEYWORDS = {"then", "do", "else", "elif", "if", "while", "until", "!", "{"}
_WRAPPERS = {"env", "time", "sudo", "nice", "exec", "command", "nohup"}


def _drop_leading_words(argv):
    i = 0
    while i < len(argv):
        word = argv[i]
        if word in _LEADING_KEYWORDS or re.match(r"[A-Za-z_][A-Za-z0-9_]*=", word):
            i += 1
        elif word in _WRAPPERS:
            i += 1
            # the wrapper's own options, e.g. `sudo -E`, `nice -n 10`
            while i < len(argv) and (argv[i].startswith("-") or argv[i].isdigit()):
                i += 1
        else:
            break
    return argv[i:]


def _commands(script):
    """Split a run script into argv lists, one per simple command. Command
    substitutions, subshells and backticks are split out too, so a cmake
    inside `$( ... )` or `( ... )` is a command of its own."""
    text = script.replace("\\\n", " ")
    for line in text.splitlines():
        line = _strip_shell_comment(line)
        for part in re.split(r"&&|\|\||;|\||\$\(|`|\(|\)", line):
            try:
                argv = shlex.split(part)
            except ValueError:
                argv = part.split()
            argv = _drop_leading_words(argv)
            if argv:
                yield argv


def _cmake_defines_in_script(script):
    names = set()
    for argv in _commands(script):
        if Path(argv[0]).name != "cmake" or _NOT_CONFIGURE & set(argv[1:]):
            continue
        args = iter(argv[1:])
        for arg in args:
            if arg == "-D":
                arg = "-D" + next(args, "")
            m = re.match(r"-D([A-Za-z_][A-Za-z0-9_]*)(?::[A-Za-z]+)?=", arg)
            if m:
                names.add(m.group(1))
    return names


def _run_scripts(path):
    doc = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    for job in (doc.get("jobs") or {}).values():
        for step in job.get("steps") or []:
            if isinstance(step.get("run"), str):
                yield step["run"]


def _undeclared(names):
    declared = _declared_options()
    return sorted(n for n in names
                  if not n.startswith(NON_PROJECT_PREFIXES) and n not in declared)


def test_every_cmake_define_in_a_workflow_is_declared():
    found = {}
    for path in sorted(WORKFLOWS.glob("*.y*ml")):
        names = set()
        for script in _run_scripts(path):
            names |= _cmake_defines_in_script(script)
        if _undeclared(names):
            found[path.name] = _undeclared(names)
    assert not found, (
        "these workflows pass -D flags CMakeLists.txt does not declare; CMake "
        "ignores them with a warning, so the flag does nothing: %s" % found)


def test_workflow_scan_reads_the_flags_it_must():
    # A scan that finds nothing passes trivially; make sure it sees the flags
    # the CI test job really passes.
    names = set()
    for script in _run_scripts(WORKFLOWS / "ci.yml"):
        names |= _cmake_defines_in_script(script)
    assert {"EBLDR_BUILD_TESTS", "EBLDR_BOARD", "CMAKE_BUILD_TYPE"} <= names


# (script, names the scan must report)
SCAN_CASES = [
    ("cmake -B b -DEBLDR_BOARD:STRING=none", {"EBLDR_BOARD"}),
    ("cmake -B b -D EBLDR_BOARD=none", {"EBLDR_BOARD"}),
    ("cmake -B b \\\n  -DEBLDR_BUILD_TESTS=ON \\\n  -D EBLDR_BOARD=none", {"EBLDR_BUILD_TESTS", "EBLDR_BOARD"}),
    ("cmake -B b -DEBLDR_BUILD_TESTS=ON # was -DOLD=1", {"EBLDR_BUILD_TESTS"}),
    ("  # was: cmake -B b -DBUILD_TESTS=ON", set()),
    ("cmake -B b -DCMAKE_C_FLAGS=\"-DEBLDR_FOO=1 -O2\"", {"CMAKE_C_FLAGS"}),
    ("gcc -DNDEBUG=1 -c x.c", set()),
    ("export CFLAGS=\"-DUSE_X=1\"", set()),
    ("cmake --build build -DIGNORED=1", set()),
    ("mkdir -p b && cmake -S . -B b -DEBLDR_HARDENING=ON; ctest", {"EBLDR_HARDENING"}),
    # A configure that does not start the line must still be seen.
    ("if [ -d src ]; then cmake -B b -DEBLDR_TYPO=1 ..; fi", {"EBLDR_TYPO"}),
    ("env FOO=1 cmake -B b -DEBLDR_TYPO=1 ..", {"EBLDR_TYPO"}),
    ("CC=clang cmake -B b -DEBLDR_TYPO=1", {"EBLDR_TYPO"}),
    ("for t in a b; do cmake -B b-$t -DEBLDR_TYPO=1; done", {"EBLDR_TYPO"}),
    ("time cmake -B b -DEBLDR_TYPO=1", {"EBLDR_TYPO"}),
    ("sudo -E cmake -B b -DEBLDR_TYPO=1", {"EBLDR_TYPO"}),
    ("out=$(cmake -B b -DEBLDR_TYPO=1 2>&1)", {"EBLDR_TYPO"}),
    ("(cd x && cmake -B b -DEBLDR_TYPO=1)", {"EBLDR_TYPO"}),
    ("cmake --build b --parallel $(nproc)", set()),
]


@pytest.mark.parametrize("script, expected", SCAN_CASES, ids=[str(i) for i in range(len(SCAN_CASES))])
def test_scan_reads_cmake_configure_defines_only(script, expected):
    assert _cmake_defines_in_script(script) == expected


@pytest.mark.parametrize("name", [
    "CMAKE_CXX_COMPILER", "FETCHCONTENT_FULLY_DISCONNECTED", "Python3_EXECUTABLE", "IDF_TARGET",
])
def test_non_project_variables_are_not_required_to_be_declared(name):
    assert _undeclared({name}) == []


@pytest.mark.parametrize("script", [
    "cmake -B build/release -G Ninja -DBUILD_TESTS=OFF",
    "cmake -B build -DEBLDR_BUILD_TEST=ON",
    "cmake -B build -D EBLDR_BUILD_TEST=ON",
    "cmake -B build -DEBLDR_ENABLE_SECURITY_CHECKS=ON",
], ids=["BUILD_TESTS", "EBLDR_BUILD_TEST", "spaced", "EBLDR_ENABLE_SECURITY_CHECKS"])
def test_an_undeclared_define_is_caught(script):
    # The guard's negative control: the real ci.yml scripts plus one more --
    # the way a dead flag would come back. EBLDR_ENABLE_SECURITY_CHECKS is
    # declared, but only in cmake/security_checks.cmake, which CMakeLists.txt
    # never includes, so passing it does nothing either.
    names = set()
    for s in list(_run_scripts(WORKFLOWS / "ci.yml")) + [script]:
        names |= _cmake_defines_in_script(s)
    name = re.search(r"-D ?(\w+)=", script).group(1)
    assert name in _undeclared(names)
