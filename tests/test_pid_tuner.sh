#!/bin/bash
# PID Auto-Tuner Dogfood Test Suite (host build)
# Simulates FOPDT plants, auto-tunes PID gains from step response, then
# closes the loop and reports settling time / overshoot / steady-state error.
#
# Run: bash tests/test_pid_tuner.sh

set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_DIR"

echo "============================================"
echo "PID Auto-Tuner Dogfood Test Suite"
echo "============================================"
echo ""

BIN="/tmp/pid_dogfood"
if gcc -DPHASE_ENGINE_ENABLED -Ilib/phase -Wall -Wextra \
    -o "$BIN" tests/test_pid_tuner.c lib/phase/pid_tuner.c -lm; then
    echo "[OK] host build of tuner + harness"
else
    echo "[FAIL] host build failed"
    exit 1
fi

"$BIN"
status=$?

echo ""
if [ $status -eq 0 ]; then
    echo "PASS: all PID auto-tuner scenarios succeeded"
else
    echo "FAIL: $status scenario(s) failed"
fi
exit $status
