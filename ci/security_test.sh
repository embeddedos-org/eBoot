#!/bin/bash
# SPDX-License-Identifier: MIT
# ci/security_test.sh — CI security testing for eBootloader
#
# Builds with sanitizers, runs unit tests, runs fuzz targets for 60 s each,
# and checks for memory errors with Valgrind.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build-security"

echo "=== eBootloader Security Test Suite ==="
echo "Project root: ${PROJECT_ROOT}"

# ---- Step 1: Build with AddressSanitizer + UndefinedBehaviorSanitizer ----
echo ""
echo "--- Building with ASAN + UBSAN ---"
cmake -B "${BUILD_DIR}" -S "${PROJECT_ROOT}" \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
    -DEBLDR_BUILD_FUZZ=ON \
    -DCMAKE_BUILD_TYPE=Debug

cmake --build "${BUILD_DIR}" --parallel "$(nproc)"

# ---- Step 2: Run unit tests under sanitizers ----
echo ""
echo "--- Running unit tests (ASAN+UBSAN) ---"
cd "${BUILD_DIR}"
ctest --output-on-failure --timeout 120

# ---- Step 3: Fuzz each target for 60 seconds ----
FUZZ_TARGETS=(
    fuzz_image_verify
    fuzz_recovery_protocol
    fuzz_fw_update
    fuzz_crypto
    fuzz_bootctl
)

echo ""
echo "--- Running fuzz targets (60 s each) ---"
for target in "${FUZZ_TARGETS[@]}"; do
    fuzz_bin="${BUILD_DIR}/tests/fuzz/${target}"
    if [ -x "${fuzz_bin}" ]; then
        echo "  Fuzzing: ${target}"
        mkdir -p "${BUILD_DIR}/fuzz-corpus/${target}"
        "${fuzz_bin}" \
            "${BUILD_DIR}/fuzz-corpus/${target}" \
            -max_total_time=60 \
            -print_final_stats=1 \
            2>&1 | tail -5
    else
        echo "  SKIP: ${target} (binary not found)"
    fi
done

# ---- Step 4: Valgrind memcheck on unit tests ----
UNIT_TESTS=(
    test_crypto
    test_ed25519
    test_keystore
)

echo ""
echo "--- Running Valgrind memcheck ---"
for test_bin_name in "${UNIT_TESTS[@]}"; do
    test_bin="${BUILD_DIR}/tests/${test_bin_name}"
    if [ -x "${test_bin}" ]; then
        echo "  Valgrind: ${test_bin_name}"
        valgrind --leak-check=full \
                 --error-exitcode=1 \
                 --track-origins=yes \
                 --quiet \
                 "${test_bin}"
    else
        echo "  SKIP: ${test_bin_name} (binary not found)"
    fi
done

echo ""
echo "=== All security tests passed ==="
