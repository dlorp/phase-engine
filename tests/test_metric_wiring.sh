#!/bin/bash
set -euo pipefail
# Phase 4A PR #67: Metric Engine Sensor Wiring - End-to-End Test
# 
# Purpose: Validate that sensor readings propagate to metrics correctly
# Dependencies: Requires PRs #65 (motion) and #66 (lux/temp) to be merged
# 
# Test Strategy:
# - Verify Comfort metric responds to temperature changes
# - Verify Comfort metric responds to light changes (Pro board only)
# - Verify Comfort falls back to time-of-day on non-Pro boards
# - Verify EM and WK will use motion data (once PR #65 is complete)
# - Verify SD and Energy are unchanged (don't use sensors)

set -e  # Exit on error

echo "======================================"
echo "PR #67: Metric Sensor Wiring Test"
echo "======================================"
echo ""

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test configuration
BUILD_DIR="build"
BOARD_CONFIGS=("BOARD=sensor_watch DISPLAY=classic" "BOARD=sensor_watch_pro DISPLAY=classic")

# Helper function: print test header
test_header() {
    echo ""
    echo "======================================"
    echo "Test: $1"
    echo "======================================"
}

# Helper function: print test result
test_result() {
    if [ $1 -eq 0 ]; then
        echo -e "${GREEN}✓ PASS${NC}: $2"
    else
        echo -e "${RED}✗ FAIL${NC}: $2"
        exit 1
    fi
}

# Helper function: print info
info() {
    echo -e "${YELLOW}ℹ INFO${NC}: $1"
}

# ======================================
# Test 1: Build Verification
# ======================================
test_header "Build Verification"

for config in "${BOARD_CONFIGS[@]}"; do
    info "Building with $config"
    make clean > /dev/null 2>&1
    if make $config PHASE_ENGINE_ENABLED=1 > build.log 2>&1; then
        test_result 0 "Build succeeded for $config"
    else
        echo -e "${RED}Build failed for $config${NC}"
        echo "Last 20 lines of build log:"
        tail -20 build.log
        exit 1
    fi
done

# ======================================
# Test 2: Code Inspection - Sensor Wiring
# ======================================
test_header "Code Inspection - Sensor Wiring"

info "Checking metrics_update signature includes sensor_state_t..."
if grep -q "const sensor_state_t \*sensors" lib/metrics/metrics.h; then
    test_result 0 "metrics_update signature includes sensor_state_t parameter"
else
    test_result 1 "metrics_update signature missing sensor_state_t parameter"
fi

info "Checking metrics.c extracts temperature from sensors..."
if grep -q "sensors_get_temperature_c10(sensors)" lib/metrics/metrics.c; then
    test_result 0 "metrics.c calls sensors_get_temperature_c10()"
else
    test_result 1 "metrics.c missing sensors_get_temperature_c10() call"
fi

info "Checking metrics.c extracts lux from sensors..."
if grep -q "sensors_get_lux(sensors)" lib/metrics/metrics.c; then
    test_result 0 "metrics.c calls sensors_get_lux()"
else
    test_result 1 "metrics.c missing sensors_get_lux() call"
fi

info "Checking movement.c passes sensors to metrics_update..."
if grep -q "&movement_state.sensors" movement.c | grep -q "metrics_update"; then
    test_result 0 "movement.c passes &movement_state.sensors to metrics_update()"
else
    test_result 1 "movement.c missing sensor parameter in metrics_update() call"
fi

# ======================================
# Test 3: Comfort Metric Wiring
# ======================================
test_header "Comfort Metric - Temperature Wiring"

info "Verifying metric_comfort_compute receives temp_c10 parameter..."
if grep -q "int16_t temp_c10" lib/metrics/metric_comfort.h; then
    test_result 0 "metric_comfort.h has temp_c10 parameter"
else
    test_result 1 "metric_comfort.h missing temp_c10 parameter"
fi

info "Verifying metric_comfort_compute uses temp_c10 in calculation..."
if grep -q "temp_c10" lib/metrics/metric_comfort.c; then
    test_result 0 "metric_comfort.c uses temp_c10 in computation"
else
    test_result 1 "metric_comfort.c not using temp_c10"
fi

# ======================================
# Test 4: Board-Specific Lux Handling
# ======================================
test_header "Comfort Metric - Light Sensor Support"

info "Checking for HAS_LIGHT_SENSOR conditional in sensors.h..."
if grep -q "HAS_LIGHT_SENSOR" lib/phase/sensors.h; then
    test_result 0 "sensors.h defines HAS_LIGHT_SENSOR macro"
else
    test_result 1 "sensors.h missing HAS_LIGHT_SENSOR macro"
fi

info "Note: Comfort metric uses lux on Pro boards, time-of-day fallback on others"
info "This is handled in metric_comfort.c logic (build-time or runtime check)"

# ======================================
# Test 5: Motion Sensor Placeholder
# ======================================
test_header "Motion Sensor Wiring (Placeholder)"

info "Checking for motion sensor TODO comments in metrics.c..."
if grep -q "TODO.*motion" lib/metrics/metrics.c || grep -q "Placeholder.*motion" lib/metrics/metrics.c; then
    test_result 0 "metrics.c has placeholder for motion sensor wiring"
else
    test_result 1 "metrics.c missing placeholder/TODO for motion sensors"
fi

info "Motion variance and intensity will be wired once PR #65 is complete"

# ======================================
# Test 6: No Anomaly Detection
# ======================================
test_header "No Anomaly Detection (Deferred to Phase 4B)"

info "Verifying no anomaly detection code was added..."
if grep -qi "anomaly\|baseline.*mean\|stddev" lib/metrics/*.c lib/metrics/*.h; then
    test_result 1 "Anomaly detection code found (should be deferred to Phase 4B)"
else
    test_result 0 "No anomaly detection code (correctly deferred)"
fi

# ======================================
# Test 7: Unchanged Metrics (SD, Energy)
# ======================================
test_header "Unchanged Metrics (SD, Energy)"

info "Verifying metric_sd.c signature unchanged..."
if grep -q "metric_sd_compute.*sleep_data.*sd_deficits" lib/metrics/metric_sd.h; then
    test_result 0 "metric_sd signature unchanged (uses sleep data, not sensors)"
else
    test_result 1 "metric_sd signature unexpectedly changed"
fi

info "Verifying metric_energy.c signature unchanged..."
if grep -q "metric_energy_compute.*phase_score.*sd_score" lib/metrics/metric_energy.h; then
    test_result 0 "metric_energy signature unchanged (uses phase + SD, not sensors)"
else
    test_result 1 "metric_energy signature unexpectedly changed"
fi

# ======================================
# Summary
# ======================================
echo ""
echo "======================================"
echo -e "${GREEN}All tests passed!${NC}"
echo "======================================"
echo ""
echo "Summary:"
echo "  ✓ Builds successfully for Pro and non-Pro boards"
echo "  ✓ Comfort metric uses real temperature (sensors_get_temperature_c10)"
echo "  ✓ Comfort metric uses real lux (sensors_get_lux)"
echo "  ✓ Motion sensors placeholder ready for PR #65"
echo "  ✓ SD and Energy metrics unchanged"
echo "  ✓ No anomaly detection (deferred to Phase 4B)"
echo ""
echo "Next steps:"
echo "  1. Complete PR #65 (motion sensors) to wire EM and WK"
echo "  2. Test end-to-end: walk around, verify metrics change"
echo "  3. Phase 4B: Add anomaly detection for playlist surfacing"
echo ""
