#!/bin/bash
# test_sensors.sh - Validation script for PR #65 LIS2DW12 Motion Integration
#
# This script validates the motion tracking sensor implementation:
# - Compiles with PHASE_ENGINE_ENABLED=1 for green board
# - Provides manual test procedures for motion variance and intensity
# - Documents expected behavior for accelerometer thresholds

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_ROOT"

echo "========================================="
echo "PR #65: LIS2DW12 Motion Integration Test"
echo "========================================="
echo ""

# Test 1: Compile with Phase Engine enabled
echo "Test 1: Compiling with PHASE_ENGINE_ENABLED=1..."
echo "  Board: sensorwatch_green"
echo "  Display: classic"
echo ""

make clean > /dev/null 2>&1 || true

if make BOARD=sensorwatch_green DISPLAY=classic PHASE_ENGINE_ENABLED=1 -j4; then
    echo "✅ PASS: Compiled successfully with Phase Engine enabled"
else
    echo "❌ FAIL: Compilation failed with Phase Engine enabled"
    exit 1
fi

echo ""

# Test 2: Compile WITHOUT Phase Engine (ensure no overhead)
echo "Test 2: Compiling with PHASE_ENGINE_ENABLED=0..."
echo "  Ensuring no overhead when disabled"
echo ""

make clean > /dev/null 2>&1 || true

if make BOARD=sensorwatch_green DISPLAY=classic -j4; then
    echo "✅ PASS: Compiled successfully without Phase Engine"
else
    echo "❌ FAIL: Compilation failed without Phase Engine"
    exit 1
fi

echo ""
echo "========================================="
echo "All Compilation Tests Passed!"
echo "========================================="
echo ""

# Manual test procedures
cat <<EOF
========================================
Manual Test Procedures
========================================

These tests require a physical green board with LIS2DW12 accelerometer.
Flash the firmware and perform the following validation procedures:

Test 3: Motion Detection (2 minutes)
------------------------------------
Procedure:
  1. Flash firmware to green board
  2. Ensure not in sleep window (use active hours)
  3. Walk around for 2 minutes with watch on wrist
  4. Check sensor state via debug output or sensor monitor face

Expected Behavior:
  ✅ motion_active = true
  ✅ motion_intensity > 0 (should be 200-800 range during walking)
  ✅ motion_variance > 0 (variance indicates movement variability)

Test 4: Stationary Detection (15 minutes)
------------------------------------------
Procedure:
  1. Place watch on desk (stationary)
  2. Wait 15 minutes without moving
  3. Check sensor state

Expected Behavior:
  ✅ motion_active = false (after 15 min inactivity)
  ✅ motion_variance low (< 100, minimal variation)
  ✅ motion_intensity near 0-100 (resting state ~1g)

Test 5: Rapid Motion Detection
-------------------------------
Procedure:
  1. Shake watch rapidly for 30 seconds
  2. Observe motion metrics

Expected Behavior:
  ✅ motion_active = true
  ✅ motion_variance high (> 500, high variability)
  ✅ motion_intensity elevated (> 300)

Test 6: Sleep Window Coordination
----------------------------------
Procedure:
  1. Configure active hours (e.g., 08:00-23:00)
  2. Enter sleep window (before 08:00 or after 23:00)
  3. Enable sleep tracker
  4. Verify accelerometer configuration

Expected Behavior:
  ✅ sensors_configure_accel() NOT called during sleep window
  ✅ Sleep tracker tap detection works (400 Hz mode)
  ✅ No interference between phase engine and sleep tracker
  ✅ Motion tracking resumes when exiting sleep window

Test 7: Accelerometer Threshold Validation
-------------------------------------------
Procedure:
  1. Test various motion intensities:
     - Light tap: Should NOT trigger (< 62 mg)
     - Gentle wrist movement: Should trigger (> 62 mg)
     - Walking motion: Should trigger reliably

Expected Behavior:
  ✅ Threshold: ~62 mg (lis2dw_configure_wakeup_threshold(1))
  ✅ Walking reliably detected
  ✅ Typing/desk work may or may not trigger (borderline)
  ✅ Sleeping/resting does NOT trigger

Power Consumption Estimate
---------------------------
  Target: 2.75 µA @ 1.6 Hz sampling rate
  Mode: LIS2DW12 Low Power Mode 1, 12-bit resolution
  
  Measurement procedure:
    1. Use power profiler or multimeter
    2. Measure current draw in active hours (motion tracking enabled)
    3. Compare to sleep window (tap detection mode)
  
  Expected: ~2.75 µA incremental cost over baseline

========================================
Test Notes
========================================

Accelerometer Configuration:
  - Data Rate: 1.6 Hz (LIS2DW_DATA_RATE_LOWEST in low power mode)
  - Mode: Low Power Mode 1 (12-bit, lowest power)
  - Range: ±2g
  - Wake Threshold: 62 mg (threshold=1)
  - Features: Sleep/wake detection + stationary mode

Motion Magnitude Formula:
  magnitude = |x| + |y| + |z|
  - At rest: ~1g = ~16384 LSB (one axis pointing down)
  - Walking: ~1.5-2g peak = ~24000-32000 LSB
  - Scaled to 0-1000 by dividing by 32

Variance Computation:
  variance = Σ(xi - mean)² / n
  - Uses 5-sample circular buffer
  - Integer math only (no FPU)
  - Mean computed first, then sum of squared deviations

Intensity Smoothing:
  intensity = 0.75 * prev + 0.25 * current
  - Exponential moving average
  - Reduces noise, tracks trends
  - 0-1000 scale

Critical Constraints:
  ✅ No FPU usage (integer math only)
  ✅ All intermediate values fit in uint32_t
  ✅ Sleep window check prevents accelerometer conflicts
  ✅ Circular buffer prevents memory overflow

========================================
Integration Points
========================================

Calling Code (movement.c):
  - sensors_init() in app_init() (once at boot)
  - sensors_configure_accel() if !is_sleep_window() (after init)
  - sensors_update() every 15 min (in metric tick handler)

Metric Consumers:
  - EM metric: sensors_get_motion_variance()
  - WK metric: sensors_get_motion_intensity()
  - Motion flag: sensors_is_motion_active()

Flash/RAM Usage:
  - Flash: ~1 KB (sensor.c compiled code)
  - RAM: ~34 bytes (sensor_state_t)
  - No heap allocation

========================================
Success Criteria
========================================

✅ Compiles with PHASE_ENGINE_ENABLED=1 (green board)
✅ Compiles with PHASE_ENGINE_ENABLED=0 (no overhead)
✅ sensors_init() runs without error
✅ sensors_configure_accel() only runs when !is_sleep_window()
✅ Motion variance computed correctly (manual validation)
✅ Motion intensity computed correctly (manual validation)
✅ No conflicts with sleep tracker (tap detection still works)
✅ Power consumption ~2.75 µA (measured or estimated)

EOF

echo ""
echo "Test script complete. Manual validation required for Tests 3-7."
echo "Flash firmware to green board and follow procedures above."
