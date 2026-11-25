"""A static-analysis step must be pointed at source that exists.

`ci.yml` ran cppcheck against `-I include/ src/`. This repository has no
`src/` -- the C sources are under `core/`, `hal/`, `stage0/`, `stage1/` and
`boards/`. cppcheck printed "could not find or open any of the paths given"
and exited 1; `continue-on-error: true` swallowed that, so the job reported
success and the uploaded `cppcheck.log` artifact held the error line rather
than a report. No file was analysed between 2026-05-27 (1bc7641) and the
commit carrying this test.

That is the same shape as a gate that cannot fail: a check whose name a
maintainer trusts, reporting on nothing. A wrong path is also the easy way to
regress it again -- renaming or moving a source directory leaves the workflow
pointing at a directory that has gone, and nothing in CI notices, because a
cppcheck run over zero files is indistinguishable from a clean one.

So the path list is checked here rather than trusted. This test needs no C
toolchain and no cppcheck: it reads the workflows and the filesystem.
"""

import re
import shlex

import pytest
import yaml
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
WORKFLOWS_DIR = REPO / ".github" / "workflows"

#: Options that take a separate value naming a directory or file we can check.
_PATH_VALUE_OPTS = {"-I", "--includes-file", "--include", "--file-list"}

#: Options that take a separate value we should consume but not check as a
#: path (suppression ids, template strings, job counts and the like).
_OPAQUE_VALUE_OPTS = {"-j", "-D", "-U", "--template", "--platform", "--std",
                      "--suppress", "--language", "--output-file"}


def _workflows():
    found = sorted(WORKFLOWS_DIR.glob("*.yml"))
    assert found, f"no workflows under {WORKFLOWS_DIR}"
    return found


#: Everything from a pipe, a redirect (with or without a leading fd) or a
#: `&&`/`||`/`;` belongs to another command -- `| tee cppcheck.log`, `|| true`
#: -- and is not part of cppcheck's argument list.
_TAIL = re.compile(r"\s(?:\d*[<>]|\|\||\||&&|;)")


def _cppcheck_argv(run_text):
    """Token lists for each line of `run_text` that *invokes* cppcheck.

    `sudo apt-get install -y cppcheck clang-tidy` names cppcheck without
    running it, so the first token has to be the tool itself.
    """
    found = []
    for raw_line in run_text.replace("\\\n", " ").splitlines():
        line = _TAIL.split(raw_line.strip(), 1)[0].strip()
        # Cheap prefix test first. Other steps mention cppcheck without
        # running it (`apt-get install ... cppcheck`) and other lines are not
        # shell-parseable once their pipeline tail is removed, so only an
        # actual invocation is handed to shlex.
        if not re.match(r"cppcheck(\s|$)", line):
            continue
        try:
            tokens = shlex.split(line)
        except ValueError:  # pragma: no cover - unbalanced quotes in a workflow
            pytest.fail(f"cannot parse this cppcheck command line: {line!r}")
        found.append(tokens[1:])
    return found


def _steps_running_cppcheck():
    """Every (workflow, job, step, run-text) whose shell runs cppcheck."""
    for path in _workflows():
        doc = yaml.safe_load(path.read_text(encoding="utf-8"))
        if not isinstance(doc, dict):
            continue
        for job_id, job in (doc.get("jobs") or {}).items():
            for step in job.get("steps") or []:
                run = str(step.get("run", ""))
                if _cppcheck_argv(run):
                    yield path.name, job_id, step.get("name", "<unnamed>"), run


def _cppcheck_paths(run_text):
    """The filesystem paths the cppcheck command lines are pointed at."""
    paths = []
    for tokens in _cppcheck_argv(run_text):
        expect_value = None
        for token in tokens:
            if expect_value is not None:
                if expect_value in _PATH_VALUE_OPTS:
                    paths.append(token)
                expect_value = None
                continue
            if token in _PATH_VALUE_OPTS or token in _OPAQUE_VALUE_OPTS:
                expect_value = token
                continue
            if token.startswith("-"):
                continue
            paths.append(token)
    return paths


def test_a_cppcheck_step_exists():
    """If every static-analysis step disappears, the tests below pass vacuously."""
    steps = list(_steps_running_cppcheck())
    assert steps, (
        "no workflow step runs cppcheck. The project stack names cppcheck as "
        "its C static analyser; if it has been removed deliberately, remove "
        "this test with it rather than leaving a suite that checks nothing."
    )


def test_every_cppcheck_step_names_at_least_one_path():
    offenders = []
    for workflow, job_id, step_name, run in _steps_running_cppcheck():
        if not _cppcheck_paths(run):
            offenders.append(f"{workflow}:{job_id}:{step_name!r}")
    assert not offenders, (
        "these cppcheck steps name no input path, so cppcheck has nothing to "
        f"analyse: {offenders}"
    )


def test_every_path_given_to_cppcheck_exists():
    """A missing path makes cppcheck analyse nothing and say so, not fail loudly."""
    missing = []
    for workflow, job_id, step_name, run in _steps_running_cppcheck():
        for candidate in _cppcheck_paths(run):
            if "$" in candidate or "*" in candidate:
                continue  # expanded at run time; not resolvable here
            if not (REPO / candidate).exists():
                missing.append(
                    f"{workflow}:{job_id}:{step_name!r} -> {candidate!r}"
                )
    assert not missing, (
        "these static-analysis steps are pointed at paths that do not exist "
        "in this repository, so they analyse nothing while reporting success:"
        "\n  " + "\n  ".join(missing)
    )


def test_cppcheck_covers_every_directory_holding_c_sources():
    """Analysing one source root and calling it static analysis overstates it.

    `boards/` alone is 83 ports; `core/` is the shared boot logic and `stage0/`
    and `stage1/` are the trusted startup path. A path list that quietly drops
    one of them narrows the check without changing its name.
    """
    with_c = {
        child.name for child in REPO.iterdir()
        if child.is_dir()
        and not child.name.startswith(".")
        and child.name not in {"tests", "build_sim", "poc", "promo", "docs"}
        and any(child.rglob("*.c"))
    }
    assert with_c, "no directory under the repository root holds C sources"

    covered = set()
    for _workflow, _job_id, _step_name, run in _steps_running_cppcheck():
        for candidate in _cppcheck_paths(run):
            covered.add(candidate.strip("/").split("/")[0])

    uncovered = sorted(with_c - covered)
    assert not uncovered, (
        "these directories hold C sources that no cppcheck step analyses: "
        f"{uncovered}. Either add them to a path list or exclude them here "
        f"with a reason."
    )
