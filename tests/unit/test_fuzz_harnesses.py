"""Static guards for the libFuzzer harnesses under tests/fuzz/.

A fuzz harness fails quietly in a way a unit test does not. It has no
assertions to go red, so the only signals it can give are "did not build" and
"crashed" -- and until the fuzz-build job was added, nothing built them:
EBLDR_BUILD_FUZZ defaults OFF and no workflow turned it on.

What that hid, and what #99 repaired: four of the five harnesses did not call
the code they claimed to. Three declared functions that exist nowhere --
eos_bootctl_parse, eos_recovery_parse_packet, eos_fw_update_init and friends --
so those targets could never link. The fourth declared a real function,
eos_image_parse_header, with the wrong signature: a pointer and a length where
the function takes a flash address and an output struct. That one *did* link,
and ran, and fuzzed nothing -- 20,000 inputs, every one of them bailing at the
first flash read.

The common cause is a hand-written `extern` prototype in the harness instead of
an #include of the real header. An `extern` is a promise the compiler is
obliged to believe and has no way to check. #99 removed all five instances;
these tests refuse the pattern, so the sixth cannot be written. The compiler
then sees both declarations and a mismatch is a build error rather than a
harness that reports success having tested nothing.

Static: they parse the sources and CMakeLists, so no clang, cmake or libFuzzer
is needed to run them.
"""

import re
from pathlib import Path

FUZZ_DIR = Path(__file__).resolve().parents[1] / "fuzz"
FUZZ_CMAKE = FUZZ_DIR / "CMakeLists.txt"

ADD_EXECUTABLE_RE = re.compile(r"add_executable\(\s*(\w+)\s+([^)]*?)\)", re.S)

# `extern "C"` and an extern *variable* are not the problem; an extern function
# prototype standing in for a header is.
EXTERN_FUNCTION_RE = re.compile(
    r'^\s*extern\s+(?!"C")[^;{]*\w+\s*\([^;{]*\)\s*;', re.M
)


def _harnesses():
    return sorted(FUZZ_DIR.glob("fuzz_*.c"))


def _cmake_text():
    return FUZZ_CMAKE.read_text(encoding="utf-8")


def test_there_are_harnesses_to_check():
    """A glob that matches nothing would make every test below vacuous."""
    assert _harnesses(), f"no fuzz_*.c under {FUZZ_DIR}"


def test_every_harness_is_built():
    """A harness with no add_executable() is never compiled, so never checked."""
    registered = set()
    for _target, sources in ADD_EXECUTABLE_RE.findall(_cmake_text()):
        for source in sources.split():
            registered.add(Path(source).name)

    missing = [p.name for p in _harnesses() if p.name not in registered]
    assert not missing, (
        f"these harnesses exist but no add_executable() in "
        f"tests/fuzz/CMakeLists.txt builds them: {missing}. An unbuilt harness "
        f"cannot fail, which is how three of them came to name functions that "
        f"do not exist."
    )


def test_no_harness_declares_its_own_prototypes():
    """Include the header; an `extern` here is an unchecked promise."""
    offenders = {}
    for path in _harnesses():
        found = EXTERN_FUNCTION_RE.findall(path.read_text(encoding="utf-8"))
        if found:
            offenders[path.name] = [line.strip() for line in found]

    assert not offenders, (
        f"these harnesses declare function prototypes instead of including the "
        f"header that declares them: {offenders}. The compiler then never sees "
        f"the real declaration beside the call, so a wrong signature does not "
        f"even warn -- eos_image_parse_header was called with a pointer and a "
        f"length for exactly this reason."
    )


def test_every_harness_defines_the_entry_point():
    """Without it the target links against libFuzzer's main and does nothing."""
    for path in _harnesses():
        text = path.read_text(encoding="utf-8")
        assert "LLVMFuzzerTestOneInput" in text, (
            f"{path.name} defines no LLVMFuzzerTestOneInput"
        )


def test_every_harness_links_the_library_under_test():
    """A harness that links nothing would build and fuzz an empty program."""
    text = _cmake_text()
    for target, _sources in ADD_EXECUTABLE_RE.findall(text):
        if not target.startswith("fuzz_"):
            continue
        link = re.search(
            r"target_link_libraries\(\s*" + re.escape(target) + r"\s+[^)]*\)",
            text,
        )
        assert link and "eboot_core" in link.group(0), (
            f"{target} does not link eboot_core, so it fuzzes nothing"
        )
