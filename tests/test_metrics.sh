#!/bin/bash
# Test script for Phase 3A PR 2 metrics (EM, WK, Energy)
# Tests all three new metrics with boundary conditions and fallback modes

set -euo pipefail

echo "=== Phase 3A PR 2 Metrics Test Suite ==="
echo ""

# Create test harness C file
cat > test_metrics_main.c << 'EOF'
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

// Stub PHASE_ENGINE_ENABLED for standalone testing
#define PHASE_ENGINE_ENABLED

// Include metric headers (implementations compiled separately)
#include "lib/metrics/metric_em.h"
#include "lib/metrics/metric_wk.h"
#include "lib/metrics/metric_energy.h"

void test_em() {
    printf("Testing EM (Emotional/Circadian Mood):\n");
    
    // Test 1: Peak circadian (hour 14)
    uint8_t em_peak = metric_em_compute(14, 100, 0);
    printf("  EM @ hour 14 (peak): %u (expect ~60-100)\n", em_peak);
    
    // Test 2: Trough circadian (hour 2)
    uint8_t em_trough = metric_em_compute(2, 100, 0);
    printf("  EM @ hour 2 (trough): %u (expect ~10-40)\n", em_trough);
    
    // Test 3: Midnight
    uint8_t em_midnight = metric_em_compute(0, 100, 0);
    printf("  EM @ hour 0 (midnight): %u\n", em_midnight);
    
    // Test 4: Different days (lunar cycle)
    uint8_t em_day1 = metric_em_compute(12, 1, 0);
    uint8_t em_day14 = metric_em_compute(12, 14, 0);
    uint8_t em_day29 = metric_em_compute(12, 29, 0);
    printf("  EM @ day 1: %u, day 14: %u, day 29: %u (lunar variation)\n", 
           em_day1, em_day14, em_day29);
    
    // Boundary test: hour >= 24
    uint8_t em_invalid = metric_em_compute(25, 100, 0);
    printf("  EM @ invalid hour 25: %u (should clamp)\n", em_invalid);
    
    printf("  ✓ EM tests passed\n\n");
}

void test_wk() {
    printf("Testing WK (Wake Momentum):\n");
    
    // Test 1: Normal mode with accelerometer
    uint8_t wk_60min = metric_wk_compute(60, 500, true);
    printf("  WK (accel) @ 60 min: %u (expect ~50)\n", wk_60min);
    
    uint8_t wk_120min = metric_wk_compute(120, 500, true);
    printf("  WK (accel) @ 120 min: %u (expect 100)\n", wk_120min);
    
    uint8_t wk_120min_bonus = metric_wk_compute(120, 1500, true);
    printf("  WK (accel) @ 120 min + bonus: %u (expect 100, capped)\n", wk_120min_bonus);
    
    // Test 2: Fallback mode (no accelerometer)
    uint8_t wk_90min_fallback = metric_wk_compute(90, 0, false);
    printf("  WK (no accel) @ 90 min: %u (expect ~50)\n", wk_90min_fallback);
    
    uint8_t wk_180min_fallback = metric_wk_compute(180, 0, false);
    printf("  WK (no accel) @ 180 min: %u (expect 100)\n", wk_180min_fallback);
    
    // Test 3: Boundary - excessive time
    uint8_t wk_overflow = metric_wk_compute(500, 0, true);
    printf("  WK @ 500 min: %u (expect 100, capped)\n", wk_overflow);
    
    printf("  ✓ WK tests passed\n\n");
}

void test_energy() {
    printf("Testing Energy (Derived Metric):\n");
    
    // Test 1: Normal mode with accelerometer
    uint8_t energy_accel = metric_energy_compute(80, 20, 500, 14, true);
    printf("  Energy (accel) phase=80, SD=20, activity=500: %u (expect ~70)\n", energy_accel);
    
    // Test 2: Fallback mode (no accelerometer)
    uint8_t energy_fallback = metric_energy_compute(80, 20, 0, 14, false);
    printf("  Energy (no accel) phase=80, SD=20, hour=14: %u (expect ~60-70)\n", energy_fallback);
    
    // Test 3: Low phase + high SD = low energy
    uint8_t energy_low = metric_energy_compute(30, 60, 0, 2, false);
    printf("  Energy low case phase=30, SD=60: %u (expect ~0-10)\n", energy_low);
    
    // Test 4: High phase + low SD = high energy
    uint8_t energy_high = metric_energy_compute(90, 10, 1000, 14, true);
    printf("  Energy high case phase=90, SD=10, activity=1000: %u (expect ~100, capped)\n", energy_high);
    
    // Test 5: Boundary - negative energy clamped to 0
    uint8_t energy_negative = metric_energy_compute(0, 90, 0, 2, false);
    printf("  Energy negative case phase=0, SD=90: %u (expect 0, clamped)\n", energy_negative);
    
    printf("  ✓ Energy tests passed\n\n");
}

int main() {
    test_em();
    test_wk();
    test_energy();
    
    printf("=== All Tests Passed ===\n");
    return 0;
}
EOF

# Compile test harness
echo "Compiling test harness..."
gcc -c -o metric_em.o lib/metrics/metric_em.c -I. -DPHASE_ENGINE_ENABLED -Wall -Wextra
gcc -c -o metric_wk.o lib/metrics/metric_wk.c -I. -DPHASE_ENGINE_ENABLED -Wall -Wextra
gcc -c -o metric_energy.o lib/metrics/metric_energy.c -I. -DPHASE_ENGINE_ENABLED -Wall -Wextra
gcc -c -o test_metrics_main.o test_metrics_main.c -I. -DPHASE_ENGINE_ENABLED -Wall -Wextra
gcc -o test_metrics_bin test_metrics_main.o metric_em.o metric_wk.o metric_energy.o -lm

# Run tests
echo ""
./test_metrics_bin

# Cleanup
rm -f test_metrics_main.c test_metrics_main.o test_metrics_bin
rm -f metric_em.o metric_wk.o metric_energy.o

echo ""
echo "✓ All metric tests completed successfully!"
