"""The CI workflow must expose one job that summarises all the others.

`required_status_checks` is null on this repository's `master`. Branch protection can only
require a check by name, and the names this workflow produces are not usable
for that directly: `release` is skipped on every pull request, so requiring it would leave
every pull request waiting for a status that never arrives.

So `ci-gate` exists to be the one name to require. These tests keep it honest
-- specifically, they fail if someone adds a job to the workflow and does not
wire it into the gate, which would otherwise silently create a job that the
required check does not cover.
"""

import re

import json
import subprocess

import yaml
import pytest
from pathlib import Path


WORKFLOWS_DIR = Path(__file__).resolve().parents[2] / ".github" / "workflows"
WORKFLOW = WORKFLOWS_DIR / "ci.yml"

#: Every workflow that produces a pull-request status, and the display name a
#: maintainer must require for it. `CI Gate` covers ci.yml and nothing else --
#: cross-workflow `needs` is not something GitHub offers -- so the required
#: set is this mapping, not one name. checks.txt on a PR head here lists 26
#: checks across five runs; ci.yml produces five of them.
#:
#: A workflow may sit in NO_GATE only with a reason about the workflow itself.
REQUIRED_CHECKS = {
    "ci.yml": "CI Gate",
}

NO_GATE = {
    "auto-assign.yml":
        "assigns a reviewer; it verifies nothing, so requiring it would block "
        "merges on a housekeeping step",
    "claude-code-review.yml":
        "posts advisory review comments and never fails on content",
    "codeql.yml":
        "already reports a single stable name, `CodeQL`, which should be "
        "required directly rather than wrapped in a gate",
    "book-build.yml":
        "builds documentation; a docs failure should not block a code merge",
    "build.yml":
        "produces `Host Build & Tests` and the cross-compile legs under "
        "stable names that can be required directly; wrapping them adds a "
        "layer without adding coverage",
    "simulation-test.yml":
        "its `Simulation Gate` job tests only needs.simulate.result and then "
        "prints 'All simulation checks passed', so a red or skipped "
        "cross-platform leg passes it. Requiring it today would assert more "
        "than it checks -- fixed below rather than excused",
}


def _load(path):
    return yaml.safe_load(path.read_text(encoding="utf-8"))


def _runs_on_pull_request(doc):
    # PyYAML parses a bare `on:` key as the boolean True.
    triggers = doc.get("on", doc.get(True, {}))
    if isinstance(triggers, (dict, list)):
        return "pull_request" in triggers
    return triggers == "pull_request"


def _pr_workflows():
    found = {}
    for path in sorted(WORKFLOWS_DIR.glob("*.yml")):
        doc = _load(path)
        if isinstance(doc, dict) and _runs_on_pull_request(doc):
            found[path.name] = doc
    assert found, f"no pull-request workflows found under {WORKFLOWS_DIR}"
    return found

# The name branch protection is pointed at. Changing it silently un-requires
# the check, so it is pinned here rather than merely read.
GATE_ID = "ci-gate"
GATE_NAME = "CI Gate"


@pytest.fixture(scope="module")
def workflow():
    assert WORKFLOW.is_file(), f"{WORKFLOW} does not exist"
    return yaml.safe_load(WORKFLOW.read_text(encoding="utf-8"))


@pytest.fixture(scope="module")
def jobs(workflow):
    return workflow["jobs"]


def _only_runs_on_tags(job):
    """Is this job gated to tag builds, and therefore skipped on every PR?"""
    condition = str(job.get("if", ""))
    return "refs/tags" in condition


def test_gate_job_exists(jobs):
    assert GATE_ID in jobs, (
        f"no {GATE_ID!r} job; branch protection has no single name to require"
    )


def test_gate_display_name_is_pinned(jobs):
    assert jobs[GATE_ID]["name"] == GATE_NAME, (
        "the gate's display name is what branch protection matches on; "
        "renaming it un-requires the check without failing anything"
    )


def test_gate_runs_even_when_an_earlier_job_fails(jobs):
    condition = str(jobs[GATE_ID].get("if", "")).strip()
    assert condition == "always()", (
        "the gate needs `if: always()`. Without it the gate is skipped when an "
        "earlier job fails, and a skipped required check never reports -- the "
        "pull request waits for a status that never arrives instead of showing "
        "a failure"
    )


def test_gate_covers_every_job_that_runs_on_a_pull_request(jobs):
    expected = {
        name for name, job in jobs.items()
        if name != GATE_ID and not _only_runs_on_tags(job)
    }
    declared = set(jobs[GATE_ID].get("needs", []))

    missing = expected - declared
    assert not missing, (
        f"these jobs run on pull requests but the gate does not wait for them: "
        f"{sorted(missing)}. A job outside the gate is a job the required "
        f"check does not cover."
    )

    unknown = declared - set(jobs)
    assert not unknown, f"the gate needs jobs that do not exist: {sorted(unknown)}"


def test_jobs_left_out_of_the_gate_are_genuinely_tag_only(jobs):
    """Excluding a job from the gate must be justified, not just convenient."""
    declared = set(jobs[GATE_ID].get("needs", []))
    for name, job in jobs.items():
        if name == GATE_ID or name in declared:
            continue
        assert _only_runs_on_tags(job), (
            f"job {name!r} is not in the gate and is not tag-only; either add "
            f"it to `needs` or give it an `if:` that explains why it cannot run "
            f"on a pull request"
        )


def test_every_pull_request_workflow_is_accounted_for():
    """A new workflow must be gated or explicitly excused, not silently added.

    `CI Gate` summarises ci.yml only. Requiring that one name -- which is what
    this PR's description asked a maintainer to do -- leaves every other
    workflow's checks unrequired, and the rot-guard above cannot see them
    either. This is the test that notices.
    """
    unaccounted = [
        name for name in _pr_workflows()
        if name not in REQUIRED_CHECKS and name not in NO_GATE
    ]
    assert not unaccounted, (
        f"these workflows produce pull-request checks but are neither gated "
        f"nor excused: {sorted(unaccounted)}. Add a gate job and list it in "
        f"REQUIRED_CHECKS, or add it to NO_GATE with the reason."
    )


def test_gated_workflows_really_have_their_gate():
    workflows = _pr_workflows()
    for filename, display in REQUIRED_CHECKS.items():
        assert filename in workflows, (
            f"{filename} is in REQUIRED_CHECKS but produces no pull-request "
            f"checks; the required set names a check that never reports"
        )
        jobs = workflows[filename]["jobs"]
        assert [j for j in jobs.values() if j.get("name") == display], (
            f"{filename} has no job displaying as {display!r}, so branch "
            f"protection would wait for a status that never arrives"
        )


def test_no_aggregating_gate_ignores_part_of_its_needs():
    """A gate that summarises N jobs must fail on any of them.

    `Simulation Gate` declared needs: [simulate, cross-platform] and tested
    only needs.simulate.result before printing "All simulation checks passed",
    so a red or skipped cross-platform leg -- three OS legs -- passed it. That
    is the same fail-open shape ci.yml's gate was written to remove, in a job
    whose name a maintainer would plausibly require.
    """
    offenders = []
    for filename, doc in _pr_workflows().items():
        for job_id, job in doc["jobs"].items():
            needs = job.get("needs")
            if not isinstance(needs, list) or len(needs) < 2:
                continue
            script = "\n".join(str(s.get("run", "")) for s in job.get("steps", []))
            if "needs" not in script:
                continue

            # Only jobs that *adjudicate*. A job that writes a step summary
            # and never claims a verdict -- book-build.yml's `summary` -- is
            # reporting, not gating, and holding it to this rule would be
            # noise. The tell is that it either fails the run or asserts that
            # everything passed.
            gates = "exit 1" in script or re.search(
                r"(all .*(check|test|job)s? .*(passed|succeeded))", script, re.I)
            if not gates:
                continue
            # A gate that names its dependencies one at a time can fall out of
            # step with `needs`; one that iterates toJSON(needs) cannot.
            iterates = "toJSON(needs)" in str(job.get("steps", ""))
            if iterates:
                continue
            # Printing a result is not testing it. Simulation Gate echoed
            # needs.cross-platform.result and then branched on
            # needs.simulate.result alone, so a check that merely greps for
            # the name would have passed it. Only a line that *compares* the
            # result counts.
            compared = set()
            for line in script.splitlines():
                if "!=" not in line and "==" not in line:
                    continue
                for n in needs:
                    if f"needs.{n}.result" in line:
                        compared.add(n)
            unchecked = [n for n in needs if n not in compared]
            if unchecked:
                offenders.append(
                    f"{filename}:{job_id} declares needs {needs} but never "
                    f"tests {unchecked}"
                )
    assert not offenders, (
        "an aggregating gate must fail on any non-success among its "
        "dependencies:\n  " + "\n  ".join(offenders)
    )


# ---- the rule itself, executed rather than pattern-matched -------------------
#
# This test used to grep the gate's `run:` text for `!= "success"` and
# `exit 1`. That is a string match on an implementation, not a check of
# behaviour: it passes for those tokens sitting in a comment or an unreachable
# branch, and fails for a correct rewrite that expresses the same rule
# differently. The rule now lives in .github/scripts/ci-gate-check.sh and is
# run here against real inputs, so the fork experiment that first demonstrated
# it does not have to be repeated by hand.

GATE_SCRIPT = Path(__file__).resolve().parents[2] / ".github" / "scripts" / "ci-gate-check.sh"


def _run_gate(payload):
    return subprocess.run(
        ["bash", str(GATE_SCRIPT)], input=payload,
        capture_output=True, text=True,
    ).returncode


def test_gate_script_exists():
    assert GATE_SCRIPT.is_file(), f"{GATE_SCRIPT} is missing"


@pytest.mark.parametrize("results,expected", [
    ({"a": {"result": "success"}}, 0),
    ({"a": {"result": "success"}, "b": {"result": "success"}}, 0),
    ({"a": {"result": "failure"}}, 1),
    ({"a": {"result": "skipped"}}, 1),
    ({"a": {"result": "cancelled"}}, 1),
    ({"a": {"result": "success"}, "b": {"result": "skipped"}}, 1),
])
def test_gate_script_accepts_only_all_success(results, expected):
    assert _run_gate(json.dumps(results)) == expected


@pytest.mark.parametrize("payload", ["", "null"])
def test_gate_script_refuses_an_empty_context(payload):
    """No results is not the same as no failures."""
    assert _run_gate(payload) == 1


def test_every_gate_invokes_the_shared_script():
    """One rule in one place, so the tests above cover every gate."""
    for workflow, display in REQUIRED_CHECKS.items():
        doc = yaml.safe_load((WORKFLOWS_DIR / workflow).read_text(encoding="utf-8"))
        job = next(j for j in doc["jobs"].values()
                   if j.get("name") == display)
        if not job.get("needs"):
            continue
        script = "\n".join(str(st.get("run", "")) for st in job.get("steps", []))
        assert "ci-gate-check.sh" in script, (
            f"{workflow}: {display!r} does not call "
            f".github/scripts/ci-gate-check.sh, so its behaviour is not "
            f"covered by the tests above."
        )
