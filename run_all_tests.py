#!/usr/bin/env python3
import sys
import subprocess

TEST_PATHS = (
    "tests/unit",
    "tests/functional",
    "tests/performance",
    "tests/simulation",
)


def run_tests():
    print("=== Running all production-ready tests via pytest ===")
    command = [sys.executable, "-m", "pytest", *TEST_PATHS, "-v"]
    result = subprocess.run(command, capture_output=False)
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(run_tests())
