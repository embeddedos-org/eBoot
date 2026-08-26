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
    result = subprocess.run(
        [
            sys.executable,
            "-m",
            "pytest", 
            "tests/unit", 
            "tests/functional", 
            "tests/performance", 
            "tests/simulation", 
            "-v"
        ], 
        capture_output=False
    )
    if result.returncode != 0:
        return result.returncode
        
    print("\n=== Running native C tests via CMake/CTest ===")
    try:
        subprocess.run(["cmake", "-S", ".", "-B", "build", "-DEBLDR_BUILD_TESTS=ON"], check=True)
        subprocess.run(["cmake", "--build", "build"], check=True)
        ctest_result = subprocess.run(["ctest", "--test-dir", "build", "-C", "Debug", "--output-on-failure"])
        return ctest_result.returncode
    except subprocess.CalledProcessError as e:
        return e.returncode
  
if __name__ == "__main__":
    raise SystemExit(run_tests())
