from types import SimpleNamespace

import run_all_tests


def test_runner_uses_current_python_interpreter(monkeypatch):
    captured = {}

    def fake_run(command, capture_output):
        captured["command"] = command
        captured["capture_output"] = capture_output
        return SimpleNamespace(returncode=7)

    monkeypatch.setattr(run_all_tests.subprocess, "run", fake_run)
    monkeypatch.setattr(run_all_tests.sys, "executable", "current-python")

    assert run_all_tests.run_tests() == 7
    assert captured == {
        "command": [
            "current-python",
            "-m",
            "pytest",
            *run_all_tests.TEST_PATHS,
            "-v",
        ],
        "capture_output": False,
    }
