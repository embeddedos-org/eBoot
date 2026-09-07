"""Regression tests for the Python dependencies the test suite needs to run.

tests/unit/test_sign_image.py guards itself with
pytest.importorskip("cryptography"). That is the right behaviour for a
contributor who has not installed the signing tools, but it also means an
undeclared dependency fails nothing: the 14 cases that pin the signed .efw
header wire format are skipped and the run still reports success.

cryptography is imported by tools/sign_image.py and tools/eos_sign.py, but it
was named neither in requirements.txt nor by the CI job that runs pytest --
that job installed a hand-written list instead of the repository requirements
-- so those 14 tests had never executed in CI.

A hand-maintained dependency list beside requirements.txt is the thing that
goes stale, so these check that the list is the requirements file, rather than
checking any particular package name twice over.

These parse requirements.txt and the workflow files as text, in the style of
test_cmake_test_registration.py, so no cmake, compiler, network or GitHub API
is needed.
"""

import re
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
REQUIREMENTS = REPO_ROOT / "requirements.txt"
WORKFLOWS_DIR = REPO_ROOT / ".github" / "workflows"
SIGN_IMAGE_SUITE = Path(__file__).resolve().parent / "test_sign_image.py"

# A requirement line: the distribution name, up to any extras, version
# specifier or environment marker.
REQUIREMENT_NAME_RE = re.compile(r"^\s*([A-Za-z0-9][A-Za-z0-9._-]*)")

# Job ids are the only keys at two-space indent inside a workflow's jobs: block.
JOB_RE = re.compile(r"^  ([A-Za-z0-9_-]+):$", re.M)

# `pytest tests/` or `python3 -m pytest tests/ -v ...`
RUNS_PYTEST_RE = re.compile(r"\bpytest\b[^\n]*\btests/")

# `pip install -r requirements.txt`, with or without the pip3 spelling.
INSTALLS_REQUIREMENTS_RE = re.compile(r"\bpip3?\s+install\b[^\n]*-r\s+requirements\.txt")


def _strip_comments(text):
    """Drop whole-line YAML comments so prose about pytest is not mistaken
    for a step that runs it."""
    return "\n".join(
        line for line in text.splitlines() if not line.lstrip().startswith("#")
    )


def _declared_requirements():
    """Distribution names declared in requirements.txt, lowercased."""
    names = set()
    for line in REQUIREMENTS.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if not line or line.startswith("-"):
            continue
        match = REQUIREMENT_NAME_RE.match(line)
        if match:
            names.add(match.group(1).lower())
    return names


def _workflow_jobs():
    """(workflow name, job id, job text) for every job in .github/workflows."""
    jobs = []
    for workflow in sorted(WORKFLOWS_DIR.glob("*.yml")):
        text = _strip_comments(workflow.read_text(encoding="utf-8"))

        start = text.find("\njobs:")
        if start == -1:
            continue
        body = text[start:]

        headers = list(JOB_RE.finditer(body))
        for i, header in enumerate(headers):
            end = headers[i + 1].start() if i + 1 < len(headers) else len(body)
            jobs.append((workflow.name, header.group(1), body[header.start():end]))
    return jobs


def _importorskip_modules():
    """Module names test_sign_image.py refuses to run without."""
    text = SIGN_IMAGE_SUITE.read_text(encoding="utf-8")
    return set(re.findall(r"importorskip\(\s*[\"']([A-Za-z0-9._-]+)[\"']", text))


def test_cryptography_is_declared_in_requirements():
    """Pin the specific dependency that was missing, by name."""
    assert "cryptography" in _declared_requirements(), (
        "cryptography is not declared in requirements.txt -- it is imported by "
        "tools/sign_image.py and tools/eos_sign.py, and without it "
        "tests/unit/test_sign_image.py skips all 14 of its cases instead of "
        "running them"
    )


def test_every_importorskip_dependency_is_declared():
    """A suite may only opt out of running for a dependency the repo declares.

    Guards the general case: adding a new importorskip for some package that
    requirements.txt never names would silently park that suite in the skipped
    column, exactly as happened to the signing tests.
    """
    modules = _importorskip_modules()
    assert modules, (
        "expected test_sign_image.py to guard itself with "
        "pytest.importorskip(...); if that guard is gone this test no longer "
        "checks anything and should be updated"
    )

    declared = _declared_requirements()
    undeclared = sorted(name for name in modules if name.lower() not in declared)

    assert not undeclared, (
        "test_sign_image.py skips itself when these modules are missing, but "
        f"requirements.txt does not declare them, so they will never be "
        f"installed and the suite will never run: {undeclared}"
    )


def test_jobs_that_run_pytest_install_the_repository_requirements():
    """The environment the tests run in has to come from requirements.txt.

    Installing a hand-written package list beside requirements.txt is what let
    cryptography go missing: the list was complete enough for the suite to
    collect and import, so nothing failed -- the signing cases just skipped.
    """
    pytest_jobs = [
        (workflow, job, text)
        for workflow, job, text in _workflow_jobs()
        if RUNS_PYTEST_RE.search(text)
    ]

    assert pytest_jobs, (
        "no workflow job appears to run pytest over tests/; if the Python "
        "suite moved, this guard needs to move with it"
    )

    missing = [
        f"{workflow}:{job}"
        for workflow, job, text in pytest_jobs
        if not INSTALLS_REQUIREMENTS_RE.search(text)
    ]

    assert not missing, (
        "these jobs run pytest over tests/ but never install from "
        "requirements.txt, so a declared dependency is absent at run time and "
        f"the suites that need it skip while the job still passes: {missing}"
    )
