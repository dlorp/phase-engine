#!/bin/bash
# Playlist Controller Test Suite
# Tests zone determination, hysteresis, relevance calculation, and face rotation

set -euo pipefail

echo "=== Playlist Controller Test Suite ==="
echo ""

# Test 1: Zone Determination
echo "Test 1: Zone Determination"
echo "  phase_score=10  → ZONE_EMERGENCE (0-25)"
echo "  phase_score=40  → ZONE_MOMENTUM (26-50)"
echo "  phase_score=60  → ZONE_ACTIVE (51-75)"
echo "  phase_score=80  → ZONE_DESCENT (76-100)"
echo "  Expected: Correct zone mapping for all scores"
echo "  [Manual verification required - check zone boundaries in code]"
echo ""

# Test 2: Hysteresis
echo "Test 2: Zone Transition Hysteresis"
echo "  Zone change requires 3 consecutive readings in new zone"
echo "  Scenario:"
echo "    - Start in ZONE_EMERGENCE (phase=20)"
echo "    - Read 1: phase=30 → ZONE_MOMENTUM pending"
echo "    - Read 2: phase=30 → ZONE_MOMENTUM pending (count=2)"
echo "    - Read 3: phase=30 → ZONE_MOMENTUM confirmed!"
echo "  Expected: No zone flicker at boundaries"
echo "  [Manual verification required - needs runtime test]"
echo ""

# Test 3: Relevance Calculation
echo "Test 3: Relevance Calculation"
echo "  Formula: relevance = weight * abs(metric - 50) / 50"
echo "  Examples:"
echo "    - Metric at 50 (neutral) → deviation=0  → relevance=0"
echo "    - Metric at 0  (extreme) → deviation=50 → relevance=weight"
echo "    - Metric at 100 (extreme) → deviation=50 → relevance=weight"
echo "    - Metric at 75 (moderate) → deviation=25 → relevance=weight/2"
echo "  Expected: Extreme values surface strongly, neutral values suppressed"
echo "  [Manual verification required - check compute_relevance() logic]"
echo ""

# Test 4: Face Rotation
echo "Test 4: Face Rotation Logic"
echo "  Weight tables (SD, EM, WK, Energy, Comfort):"
echo "    EMERGENCE: [30, 25,  5, 10, 30]  → SD + Comfort priority"
echo "    MOMENTUM:  [20, 20, 30, 10, 20]  → WK is key"
echo "    ACTIVE:    [15, 20,  5, 40, 20]  → Energy dominates"
echo "    DESCENT:   [10, 35,  0, 10, 45]  → EM + Comfort (WK weight=0)"
echo ""
echo "  Test Case: EMERGENCE zone, metrics=(SD:90, EM:60, WK:50, Energy:40, Comfort:85)"
echo "    Relevances: SD=24, EM=5, WK=0, Energy=2, Comfort=21"
echo "    Expected rotation: [SD, Comfort] (both >= 10 threshold)"
echo "    Expected order: SD first (relevance=24 > 21)"
echo "  [Manual verification required - needs runtime test]"
echo ""

# Test 5: Auto-Advance
echo "Test 5: Auto-Advance"
echo "  Default dwell_limit = 30 ticks"
echo "  Expected: After 30 updates without zone change, face advances"
echo "  Manual advance (ALARM button): Immediately cycles to next face"
echo "  [Manual verification required - needs runtime test]"
echo ""

# Test 6: Compilation Test
echo "Test 6: Compilation Test"
echo "  Building with PHASE_ENGINE_ENABLED..."

# Check if we can build successfully
if make BOARD=sensorwatch_blue DISPLAY=classic PHASE_ENGINE_ENABLED=1 -j4 > /dev/null 2>&1; then
    echo "  ✅ Build succeeded!"
    
    # Check binary size
    if command -v arm-none-eabi-size &> /dev/null; then
        echo ""
        echo "Binary size:"
        arm-none-eabi-size build/watch.elf | tail -n 1 | awk '{printf "  text: %d bytes (%.1f KB)\n", $1, $1/1024}'
        arm-none-eabi-size build/watch.elf | tail -n 1 | awk '{printf "  data: %d bytes\n", $2}'
        arm-none-eabi-size build/watch.elf | tail -n 1 | awk '{printf "  bss:  %d bytes\n", $3}'
    fi
else
    echo "  ❌ Build failed!"
    echo "  Run 'make BOARD=sensorwatch_blue DISPLAY=classic PHASE_ENGINE_ENABLED=1' to see errors"
    exit 1
fi

echo ""
echo "=== Test Summary ==="
echo "✅ Compilation test passed"
echo "⚠️  Runtime tests require manual verification:"
echo "   - Zone determination logic"
echo "   - Hysteresis behavior"
echo "   - Relevance calculation"
echo "   - Face rotation ordering"
echo "   - Auto-advance timing"
echo ""
echo "To test on hardware/emulator:"
echo "  1. Flash firmware with PHASE_ENGINE_ENABLED=1"
echo "  2. Monitor zone transitions over 24h simulation"
echo "  3. Verify face rotation matches metric relevance"
echo "  4. Test manual advance with ALARM button"
echo ""
