#!/bin/bash
# SPDX-License-Identifier: MIT
# scripts/check_board_config.sh — board dispatch configure check
#
# Regression test for the CMake board dispatch: every board port under
# boards/ must configure successfully with -DEBLDR_BOARD=<name>, and an
# unknown board name must still be rejected with a fatal error.
#
# This is configure-only (no compilation), so it needs no cross toolchains
# and runs on any host with CMake.
#
# Usage:
#     scripts/check_board_config.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"' EXIT

fail=0

echo "=== eBoot board dispatch configure check ==="

for board_dir in "${PROJECT_ROOT}"/boards/*/; do
    board="$(basename "${board_dir}")"
    if cmake -S "${PROJECT_ROOT}" -B "${WORK_DIR}/${board}" \
         -DEBLDR_BOARD="${board}" > "${WORK_DIR}/${board}.log" 2>&1; then
        printf '  %-20s OK\n' "${board}"
    else
        printf '  %-20s FAIL (configure rejected)\n' "${board}"
        tail -n 5 "${WORK_DIR}/${board}.log" >&2
        fail=1
    fi
done

# An unknown board must be rejected with a fatal error.
if cmake -S "${PROJECT_ROOT}" -B "${WORK_DIR}/unknown" \
     -DEBLDR_BOARD=no_such_board > "${WORK_DIR}/unknown.log" 2>&1; then
    echo "  unknown board        FAIL (configure unexpectedly succeeded)"
    fail=1
else
    echo "  unknown board        OK (rejected as expected)"
fi

if [ "${fail}" -ne 0 ]; then
    echo "RESULT: FAIL"
    exit 1
fi

echo "RESULT: PASS"
