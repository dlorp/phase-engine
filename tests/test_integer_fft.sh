#!/bin/bash
# Integer FFT Dogfood Test Suite (host build)
# Proves exact integer reconstruction round trip (forward + inverse) on
# known sine / pseudo-random / DC / impulse inputs, plus spectral peak
# placement and invalid-size handling.
#
# Run: bash tests/test_integer_fft.sh

set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_DIR"

echo "============================================"
echo "Integer FFT Dogfood Test Suite"
echo "============================================"
echo ""

BIN="/tmp/int_fft_dogfood"
if gcc -DPHASE_ENGINE_ENABLED -Ilib/phase -Wall -Wextra \
    -o "$BIN" tests/test_integer_fft.c lib/phase/int_fft.c -lm; then
    echo "[OK] host build of int_fft + harness"
else
    echo "[FAIL] host build failed"
    exit 1
fi

"$BIN"
status=$?

echo ""
if [ $status -eq 0 ]; then
    echo "PASS: all integer FFT scenarios succeeded"
else
    echo "FAIL: $status scenario(s) failed"
fi
exit $status
