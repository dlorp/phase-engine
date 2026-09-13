#!/bin/bash
set -euo pipefail
# Test script for PR #66: Lux + Temperature Integration
# Validates build-time board detection and compilation for different boards

set -e  # Exit on error

echo "================================================"
echo "PR #66: Lux + Temperature Integration Test"
echo "================================================"
echo ""

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test counter
TESTS_PASSED=0
TESTS_TOTAL=0

# Function to run a build test
run_build_test() {
    local board=$1
    local display=$2
    local description=$3
    
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    echo -e "${YELLOW}Test $TESTS_TOTAL:${NC} $description"
    echo "Building for BOARD=$board DISPLAY=$display PHASE_ENGINE_ENABLED=1..."
    
    if make clean > /dev/null 2>&1 && \
       make BOARD=$board DISPLAY=$display PHASE_ENGINE_ENABLED=1 > /dev/null 2>&1; then
        echo -e "${GREEN}✅ PASSED${NC}: $board build successful"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        echo "❌ FAILED: $board build failed"
        return 1
    fi
    echo ""
}

# Test 1: Pro board (HAS_LIGHT_SENSOR=1)
run_build_test "sensorwatch_pro" "classic" \
    "Pro board with lux sensor support"

# Test 2: Green board (HAS_LIGHT_SENSOR=0)
run_build_test "sensorwatch_green" "classic" \
    "Green board without lux sensor (fallback to 0)"

# Test 3: Red board (HAS_LIGHT_SENSOR=0)
run_build_test "sensorwatch_red" "classic" \
    "Red board without lux sensor (fallback to 0)"

# Test 4: Blue board (HAS_LIGHT_SENSOR=0)
run_build_test "sensorwatch_blue" "classic" \
    "Blue board without lux sensor (fallback to 0)"

# Verify build artifacts exist
echo "================================================"
echo "Verifying build artifacts..."
echo "================================================"

if [ -f "build/watch.elf" ]; then
    echo -e "${GREEN}✅${NC} build/watch.elf exists"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo "❌ build/watch.elf not found"
fi
TESTS_TOTAL=$((TESTS_TOTAL + 1))

echo ""
echo "================================================"
echo "Summary"
echo "================================================"
echo "Tests passed: $TESTS_PASSED/$TESTS_TOTAL"
echo ""

if [ $TESTS_PASSED -eq $TESTS_TOTAL ]; then
    echo -e "${GREEN}✅ All builds successful!${NC}"
    echo ""
    echo "Manual testing checklist:"
    echo "========================="
    echo ""
    echo "Pro board (with light sensor):"
    echo "  1. Flash firmware to sensorwatch_pro"
    echo "  2. Cover light sensor → lux_ema should decrease (EMA, ~4 samples to 95%)"
    echo "  3. Expose to bright light → lux_ema should increase (converges faster than rolling avg)"
    echo "  4. Quick flash with flashlight → lux_ema resists spike (EMA pole at 0.75)"
    echo "  5. Indoor lighting: expect lux_ema ~50-500"
    echo "  6. Outdoor sunlight: expect lux_ema ~2000-10000"
    echo ""
    echo "Non-Pro boards (Green/Red/Blue - no light sensor):"
    echo "  1. Flash firmware to board"
    echo "  2. Verify lux_ema stays at 0 (no sensor, sensor_healthy = false)"
    echo "  3. Verify no runtime overhead from disabled lux code"
    echo ""
    echo "All boards (temperature):"
    echo "  1. Read temperature value"
    echo "  2. Room temperature: expect ~200 (20.0°C) ± 50 (5°C)"
    echo "  3. Warm hand over board → temperature should rise"
    echo "  4. If thermistor not detected, should fall back to 200"
    echo ""
    echo "Power validation:"
    echo "  - Lux sampling: <0.1 µA average (1/min ADC wake)"
    echo "  - Temperature: negligible (sampled every 15 min)"
    echo "  - Measure with multimeter during normal operation"
    echo ""
    exit 0
else
    echo "❌ Some tests failed"
    exit 1
fi
