#!/bin/bash
# AEMA / RLS Filter Dogfood Test Suite (host build)
# Checks AEMA alpha bounds/tracking and RLS diagonal-P boundedness across
# stationary, large-amplitude, and learnable-ramp signals.
#
# Run: bash tests/test_rls_filter.sh

set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_DIR"

echo "============================================"
echo "AEMA / RLS Filter Dogfood Test Suite"
echo "============================================"
echo ""

BIN="/tmp/rls_dogfood"
if gcc -DPHASE_ENGINE_ENABLED -Ilib/phase -Wall -Wextra \
    -o "$BIN" tests/test_rls_filter.c lib/phase/rls_filter.c; then
    echo "[OK] host build of filters + harness"
else
    echo "[FAIL] host build failed"
    exit 1
fi

"$BIN"
status=$?

echo ""
if [ $status -eq 0 ]; then
    echo "PASS: all AEMA/RLS checks succeeded"
else
    echo "FAIL: $status check(s) failed"
fi
exit $status
