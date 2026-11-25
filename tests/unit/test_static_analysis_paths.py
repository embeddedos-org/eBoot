"""The cppcheck steps must analyse the source that exists, and be able to fail.

`.github/workflows/ci.yml` handed cppcheck `src/`, a directory this repository
has never had. cppcheck answered "could not find or open any of the paths
given" and analysed zero files from 2026-05-27 until #208 -- while
`static-analysis` sat in `ci-gate`'s `needs:` and reported success, because
`continue-on-error: true` discarded the exit status. A run over zero files is
indistinguishable from a clean one.

Three shapes made that possible, and this file refuses all three for every
cppcheck step in every workflow:

  - a path that does not exist, or a directory of C sources left out;
  - `--error-exitcode` missing, `|| true` after the command, a pipe without
    `set -o pipefail` (GitHub runs `run:` under `bash -e`, not `-o pipefail`,
    so `cppcheck ... | tee` takes tee's status), or
    `continue-on-error: true` on the step;
  - a `--suppress=` that is not in ALLOWED_SUPPRESSIONS below. Findings are
    suppressed one line at a time, in the source, with a reason, which is what
    `--inline-suppr` is for; a command-line `--suppress=id` hides the id
    everywhere and is the thing that cannot be reviewed later.
"""

import re
import shlex
import subprocess
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS_DIR = ROOT / ".github" / "workflows"

#: Top-level directories that hold C sources and are deliberately not analysed.
#: A directory of C sources that is neither analysed nor listed here fails the
#: test, so the next rename or new subsystem cannot quietly leave the check.
NOT_ANALYSED = {
    "tests":
        "test code, not the product. Analysing it is a separate decision with "
        "its own set of findings; leaving it out does not weaken the check on "
        "what ships",
    "poc":
        "poc/tlv_downgrade_poc.c reproduces a vulnerability on purpose. It is "
        "meant to stay as it is, so a static analyser has nothing to tell us "
        "about it",
    "build_sim":
        "a CMake build directory that is committed to the repository by "
        "mistake; its one .c file is CMake's own generated "
        "CMakeCCompilerId.c. That it is tracked at all is a separate problem "
        "from #208",
}

#: Every `--suppress=` a cppcheck step may pass, and why. An entry naming a
#: file (`id:path`) suppresses that id in that file only.
ALLOWED_SUPPRESSIONS = {
    "missingIncludeSystem":
        "cppcheck is not given a sysroot; it cannot open <stdint.h> and says "
        "so once per translation unit. Not about this code",
    "missingInclude":
        "same, for headers cppcheck resolves through the build system rather "
        "than -I",
    "unusedFunction":
        "every board port defines its ops as file-static functions reached "
        "only through the ops table, which cppcheck does not follow. "
        "Pre-existing; removing it is its own change",
    "unmatchedSuppression:core/recovery.c":
        "cppcheck 2.13 (build.yml) reports uninitvar at the raw_entries loop "
        "and 2.7 (ci.yml) does not, so the inline comment the first needs is "
        "unmatched for the second. Scoped to the one file where the two "
        "versions disagree",
    "unmatchedSuppression:core/bootloader_protect.c":
        "the reverse direction: 2.7 with --enable=all reports "
        "knownConditionTrueFalse on the build-time policy comparison, 2.13 "
        "with --enable=warning,style does not",
    "unmatchedSuppression:core/debug_lock.c":
        "same as core/bootloader_protect.c",
}

#: Severity switches a step may pass. Anything else -- a narrower --enable, a
#: --disable -- would shrink what the step looks at.
ALLOWED_ENABLE = {"all", "warning,style"}


def _workflows():
    return sorted(WORKFLOWS_DIR.glob("*.yml")) + sorted(
        WORKFLOWS_DIR.glob("*.yaml"))


def _cppcheck_steps():
    """(workflow, job, step, command words) for every cppcheck invocation in
    every workflow, with line continuations joined."""
    out = []
    for path in _workflows():
        doc = yaml.safe_load(path.read_text(encoding="utf-8"))
        for job_name, job in (doc.get("jobs") or {}).items():
            for step in job.get("steps") or []:
                script = step.get("run")
                if not script or not re.search(r"\bcppcheck\b", script):
                    continue
                joined = script.replace("\\\n", " ")
                for line in joined.splitlines():
                    stripped = line.strip()
                    if not stripped.startswith("cppcheck "):
                        continue
                    out.append((path.name, job_name, step, script,
                                _command_words(stripped)))
    return out


#: Where the cppcheck command ends and the shell takes over. Without this the
#: redirection and the `tee` of `cppcheck ... 2>&1 | tee cppcheck.log` read as
#: input paths.
_SHELL_OPERATORS = ("|", "||", "&&", ";", ">", ">>", "2>&1", "&")


def _command_words(line):
    words = []
    for word in shlex.split(line, comments=True):
        if word in _SHELL_OPERATORS or re.fullmatch(r"\d?>+&?\d?", word):
            break
        words.append(word)
    return words


def _tracked_c_sources():
    """Tracked .c files, from git. Not a glob: build/ and build_sim/ hold
    generated sources that are not part of the repository."""
    listed = subprocess.run(
        ["git", "ls-files", "-z", "*.c"], cwd=ROOT, check=True,
        capture_output=True, text=True).stdout
    return [Path(name) for name in listed.split("\0") if name]


def _paths_and_options(words):
    """The positional path arguments of a cppcheck command, and its options.
    `-I include/` and `-Iinclude/` are include directories, not inputs."""
    paths, options, expect_value = [], [], False
    for word in words[1:]:
        if expect_value:
            expect_value = False
            continue
        if word.startswith("-"):
            options.append(word)
            if word in ("-I", "-D", "-U", "--include", "--suppressions-list",
                        "--library", "--platform"):
                expect_value = True
            continue
        paths.append(word)
    return paths, options


STEPS = _cppcheck_steps()


def test_there_is_at_least_one_cppcheck_step():
    """If this fails the rest of the file is vacuous."""
    assert STEPS, "no workflow runs cppcheck; the other tests here check nothing"


@pytest.mark.parametrize("workflow,job", [(s[0], s[1]) for s in STEPS])
def test_every_path_handed_to_cppcheck_exists(workflow, job):
    for name, job_name, _step, _script, words in STEPS:
        if (name, job_name) != (workflow, job):
            continue
        paths, _ = _paths_and_options(words)
        assert paths, "%s/%s: cppcheck is given no input path" % (name, job_name)
        for candidate in paths:
            assert (ROOT / candidate).exists(), (
                "%s/%s: cppcheck is given %r, which does not exist. This is "
                "the #208 defect: cppcheck exits with "
                "'could not find or open any of the paths given' and analyses "
                "nothing." % (name, job_name, candidate))


def test_no_directory_of_c_sources_is_left_out_of_static_analysis():
    """The union of what the cppcheck steps analyse must cover every top-level
    directory holding .c files, except those NOT_ANALYSED explains."""
    with_sources = {source.parts[0] for source in _tracked_c_sources()
                    if len(source.parts) > 1}
    assert with_sources, "git lists no .c files; the call above is wrong"

    analysed = set()
    for _name, _job, _step, _script, words in STEPS:
        paths, _ = _paths_and_options(words)
        for candidate in paths:
            analysed.add(Path(candidate).parts[0])

    missing = sorted(with_sources - analysed - set(NOT_ANALYSED))
    assert not missing, (
        "these directories hold C sources that no cppcheck step analyses and "
        "that NOT_ANALYSED does not explain: %s. Add them to a cppcheck step, "
        "or to NOT_ANALYSED with the reason." % ", ".join(missing))


@pytest.mark.parametrize("workflow,job", [(s[0], s[1]) for s in STEPS])
def test_a_cppcheck_step_can_fail_the_job(workflow, job):
    for name, job_name, step, script, words in STEPS:
        if (name, job_name) != (workflow, job):
            continue
        where = "%s/%s" % (name, job_name)
        _, options = _paths_and_options(words)

        assert any(o.startswith("--error-exitcode=") for o in options), (
            "%s: cppcheck without --error-exitcode reports findings and exits "
            "0" % where)
        for option in options:
            if option.startswith("--error-exitcode="):
                assert option != "--error-exitcode=0", (
                    "%s: --error-exitcode=0 is no exit code at all" % where)

        assert "|| true" not in script, (
            "%s: `|| true` cancels --error-exitcode; the step cannot fail on "
            "a finding" % where)
        assert step.get("continue-on-error") is not True, (
            "%s: continue-on-error discards the exit status, so the job "
            "reports success on any finding" % where)

        # `cppcheck ... | tee log` takes tee's status unless pipefail is set.
        piped = any(re.search(r"^\s*cppcheck\b.*\|", line)
                    for line in script.replace("\\\n", " ").splitlines())
        if piped:
            assert re.search(r"set\s+-o\s+pipefail", script), (
                "%s: the cppcheck command is piped, so the step's status is "
                "the last command's. GitHub runs `run:` under `bash -e`, not "
                "`-o pipefail`: add `set -o pipefail`." % where)


@pytest.mark.parametrize("workflow,job", [(s[0], s[1]) for s in STEPS])
def test_a_cppcheck_step_suppresses_nothing_that_is_not_explained(workflow, job):
    for name, job_name, _step, _script, words in STEPS:
        if (name, job_name) != (workflow, job):
            continue
        where = "%s/%s" % (name, job_name)
        _, options = _paths_and_options(words)

        for option in options:
            if option.startswith("--suppress="):
                value = option[len("--suppress="):]
                assert value in ALLOWED_SUPPRESSIONS, (
                    "%s: --suppress=%s is not in ALLOWED_SUPPRESSIONS. A "
                    "finding is suppressed at its line with a "
                    "`cppcheck-suppress` comment and a reason, not from the "
                    "command line. If this one belongs here, add it with the "
                    "reason." % (where, value))
            if option.startswith("--enable="):
                assert option[len("--enable="):] in ALLOWED_ENABLE, (
                    "%s: %s narrows what the step checks" % (where, option))

        assert not any(o.startswith("--suppressions-list") for o in options), (
            "%s: a suppressions file hides findings away from the line they "
            "are about" % where)


def test_inline_suppressions_in_the_tree_are_honoured():
    """A `cppcheck-suppress` comment is ignored without --inline-suppr. If a
    step analyses a file carrying one and does not pass the flag, the finding
    comes back and the step fails for a reason nobody wrote down."""
    carriers = sorted(
        source.as_posix() for source in _tracked_c_sources()
        if "cppcheck-suppress" in (ROOT / source).read_text(
            encoding="utf-8", errors="replace")
    )
    assert carriers, (
        "no source carries a cppcheck-suppress comment; this test and the "
        "--inline-suppr flags are dead weight and should go")

    for name, job_name, _step, _script, words in STEPS:
        paths, options = _paths_and_options(words)
        roots = [Path(candidate).as_posix().rstrip("/") for candidate in paths]
        covered = [c for c in carriers
                   if any(c == r or c.startswith(r + "/") for r in roots)]
        if not covered:
            continue
        assert "--inline-suppr" in options, (
            "%s/%s analyses %s, which carries a cppcheck-suppress comment, "
            "without --inline-suppr: the comment is ignored."
            % (name, job_name, covered[0]))


def test_every_inline_suppression_gives_a_reason():
    """A bare `cppcheck-suppress id` says a finding was dismissed and not why.
    Every one in the tree carries its reason on the same line."""
    bare = []
    for source in sorted(_tracked_c_sources()):
        text = (ROOT / source).read_text(encoding="utf-8", errors="replace")
        for number, line in enumerate(text.splitlines(), 1):
            match = re.search(r"cppcheck-suppress\s+(\S+)(.*)", line)
            if match and not match.group(2).strip(" */"):
                bare.append("%s:%d" % (source.as_posix(), number))
    assert not bare, (
        "these suppressions name an id and no reason: %s" % ", ".join(bare))
