#!/bin/bash
set -euo pipefail
# Phase 3 PR 6: End-to-End Integration Test
# Tests the complete Phase 3 system integration

set -e  # Exit on error

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO_DIR"

echo "========================================"
echo "Phase 3 End-to-End Test Suite"
echo "========================================"
echo "Tests: Build verification, zero-cost, size budget,"
echo "       component tests, integration, documentation"
echo ""

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

pass_count=0
fail_count=0

# Test result tracking
test_pass() {
    echo -e "${GREEN}✓ PASS${NC}: $1"
    ((pass_count++))
}

test_fail() {
    echo -e "${RED}✗ FAIL${NC}: $1"
    ((fail_count++))
}

test_warn() {
    echo -e "${YELLOW}⚠ WARN${NC}: $1"
}

# ====================
# Test 1: Full build with phase enabled
# ====================
echo "Test 1: Full build with PHASE_ENGINE_ENABLED=1"
echo "---------------------------------------------"

make clean > /dev/null 2>&1 || true

if make BOARD=sensorwatch_pro DISPLAY=classic PHASE_ENGINE_ENABLED=1 > build_phase.log 2>&1; then
    test_pass "Build with PHASE_ENGINE_ENABLED succeeded"
    
    # Check for key symbols
    if nm build/watch.elf | grep -q "metrics_init"; then
        test_pass "metrics_init symbol found in binary"
    else
        test_fail "metrics_init symbol not found"
    fi
    
    if nm build/watch.elf | grep -q "playlist_init"; then
        test_pass "playlist_init symbol found in binary"
    else
        test_fail "playlist_init symbol not found"
    fi
    
    if nm build/watch.elf | grep -q "playlist_advance"; then
        test_pass "playlist_advance symbol found in binary"
    else
        test_fail "playlist_advance symbol not found"
    fi
else
    test_fail "Build with PHASE_ENGINE_ENABLED failed (see build_phase.log)"
    cat build_phase.log
fi

echo ""

# ====================
# Test 2: Verify zero-cost when disabled
# ====================
echo "Test 2: Zero-cost verification (PHASE_ENGINE_ENABLED=0)"
echo "--------------------------------------------------------"

make clean > /dev/null 2>&1 || true

if make BOARD=sensorwatch_pro DISPLAY=classic > build_disabled.log 2>&1; then
    test_pass "Build without PHASE_ENGINE_ENABLED succeeded"
    
    # Check that phase symbols are NOT present
    if ! nm build/watch.elf | grep -q "metrics_init"; then
        test_pass "metrics_init symbol correctly absent"
    else
        test_fail "metrics_init symbol found (should be absent)"
    fi
    
    if ! nm build/watch.elf | grep -q "playlist_init"; then
        test_pass "playlist_init symbol correctly absent"
    else
        test_fail "playlist_init symbol found (should be absent)"
    fi
else
    test_fail "Build without PHASE_ENGINE_ENABLED failed (see build_disabled.log)"
    cat build_disabled.log
fi

echo ""

# ====================
# Test 3: Size verification
# ====================
echo "Test 3: Flash and RAM size verification"
echo "----------------------------------------"

# Build with phase engine enabled again for size check
make clean > /dev/null 2>&1 || true
if make BOARD=sensorwatch_pro DISPLAY=classic PHASE_ENGINE_ENABLED=1 > /dev/null 2>&1; then
    
    if command -v arm-none-eabi-size &> /dev/null; then
        echo "Binary size with PHASE_ENGINE_ENABLED:"
        arm-none-eabi-size build/watch.elf
        echo ""
        
        # Extract sizes (text = flash, data+bss = RAM)
        SIZES=$(arm-none-eabi-size build/watch.elf | tail -1)
        TEXT=$(echo $SIZES | awk '{print $1}')
        DATA=$(echo $SIZES | awk '{print $2}')
        BSS=$(echo $SIZES | awk '{print $3}')
        
        FLASH_USED=$TEXT
        RAM_USED=$((DATA + BSS))
        
        FLASH_LIMIT=262144  # 256 KB
        RAM_LIMIT=32768     # 32 KB
        
        FLASH_PCT=$((FLASH_USED * 100 / FLASH_LIMIT))
        RAM_PCT=$((RAM_USED * 100 / RAM_LIMIT))
        
        echo "Flash: $FLASH_USED / $FLASH_LIMIT bytes ($FLASH_PCT%)"
        echo "RAM: $RAM_USED / $RAM_LIMIT bytes ($RAM_PCT%)"
        echo ""
        
        if [ $FLASH_USED -lt $FLASH_LIMIT ]; then
            test_pass "Flash usage within budget"
        else
            test_fail "Flash usage exceeds budget"
        fi
        
        if [ $RAM_USED -lt $RAM_LIMIT ]; then
            test_pass "RAM usage within budget"
        else
            test_fail "RAM usage exceeds budget"
        fi
    else
        test_warn "arm-none-eabi-size not found, skipping size check"
    fi
else
    test_fail "Could not build for size verification"
fi

echo ""

# ====================
# Test 4: Compiler warnings check
# ====================
echo "Test 4: Compiler warnings check"
echo "--------------------------------"

make clean > /dev/null 2>&1 || true
if make BOARD=sensorwatch_pro DISPLAY=classic PHASE_ENGINE_ENABLED=1 > build_warnings.log 2>&1; then
    WARNING_COUNT=$(grep -i "warning:" build_warnings.log | wc -l)
    
    if [ $WARNING_COUNT -eq 0 ]; then
        test_pass "No compiler warnings"
    else
        test_warn "Found $WARNING_COUNT compiler warnings"
        echo "First 5 warnings:"
        grep -i "warning:" build_warnings.log | head -5
    fi
else
    test_fail "Build failed during warning check"
fi

echo ""

# ====================
# Test 5: Header guard verification
# ====================
echo "Test 5: Header guard verification"
echo "----------------------------------"

GUARD_ERRORS=0

# Check metrics headers
for header in lib/metrics/*.h; do
    if [ -f "$header" ]; then
        BASENAME=$(basename "$header")
        GUARD=$(echo "${BASENAME}" | tr '[:lower:]' '[:upper:]' | tr '.' '_')
        
        if grep -q "#ifndef ${GUARD}_" "$header" && grep -q "#define ${GUARD}_" "$header"; then
            # Guard found
            :
        else
            test_warn "Header guard missing or incorrect in $header"
            ((GUARD_ERRORS++))
        fi
    fi
done

# Check playlist header
if grep -q "#ifndef PLAYLIST_H_" lib/phase/playlist.h && grep -q "#define PLAYLIST_H_" lib/phase/playlist.h; then
    # Guard found
    :
else
    test_warn "Header guard missing or incorrect in lib/phase/playlist.h"
    ((GUARD_ERRORS++))
fi

if [ $GUARD_ERRORS -eq 0 ]; then
    test_pass "All header guards present"
else
    test_warn "$GUARD_ERRORS header guard issues found"
fi

echo ""

# ====================
# Test 6: PHASE_ENGINE_ENABLED ifdef coverage
# ====================
echo "Test 6: PHASE_ENGINE_ENABLED ifdef coverage"
echo "--------------------------------------------"

# Check that all Phase 3 code is properly guarded
UNGUARDED=0

# Check movement.h
if grep -A 5 "metrics_engine_t metrics" movement.h | grep -q "#ifdef PHASE_ENGINE_ENABLED"; then
    test_pass "movement.h Phase 3 fields properly guarded"
else
    test_fail "movement.h Phase 3 fields not properly guarded"
    ((UNGUARDED++))
fi

# Check movement.c includes
if grep -B 1 "#include \"metrics.h\"" movement.c | grep -q "#ifdef PHASE_ENGINE_ENABLED"; then
    test_pass "movement.c Phase 3 includes properly guarded"
else
    test_fail "movement.c Phase 3 includes not properly guarded"
    ((UNGUARDED++))
fi

if [ $UNGUARDED -eq 0 ]; then
    test_pass "All Phase 3 code properly guarded with ifdefs"
fi

echo ""

# ====================
# Test 7: Component test scripts
# ====================
echo "Test 7: Component Test Scripts"
echo "-------------------------------"

# Test metrics
if [ -f test_metrics.sh ]; then
    test_pass "test_metrics.sh exists"
    # Note: Don't run it here (requires specific build setup)
else
    test_warn "test_metrics.sh not found"
fi

# Test playlist
if [ -f test_playlist.sh ]; then
    test_pass "test_playlist.sh exists"
else
    test_warn "test_playlist.sh not found"
fi

# Test zone faces
if [ -f test_zone_faces.sh ]; then
    test_pass "test_zone_faces.sh exists"
else
    test_warn "test_zone_faces.sh not found"
fi

echo ""

# ====================
# Test 8: Documentation completeness
# ====================
echo "Test 8: Documentation Completeness"
echo "-----------------------------------"

DOC_ERRORS=0

# Check critical docs
if [ -f lib/metrics/README.md ]; then
    test_pass "lib/metrics/README.md exists"
else
    test_fail "lib/metrics/README.md missing"
    ((DOC_ERRORS++))
fi

if [ -f PHASE3_BUDGET_REPORT.md ]; then
    test_pass "PHASE3_BUDGET_REPORT.md exists"
else
    test_fail "PHASE3_BUDGET_REPORT.md missing"
    ((DOC_ERRORS++))
fi

if [ -f docs/PHASE3_COMPLETE.md ]; then
    test_pass "docs/PHASE3_COMPLETE.md exists"
else
    test_fail "docs/PHASE3_COMPLETE.md missing"
    ((DOC_ERRORS++))
fi

if [ -f PHASE3_PREWORK.md ]; then
    test_pass "PHASE3_PREWORK.md exists"
else
    test_warn "PHASE3_PREWORK.md missing (expected)"
fi

if [ $DOC_ERRORS -eq 0 ]; then
    test_pass "All critical documentation present"
fi

echo ""

# ====================
# Test Summary
# ====================
echo "========================================"
echo "Test Summary"
echo "========================================"
echo -e "${GREEN}Passed: $pass_count${NC}"
echo -e "${RED}Failed: $fail_count${NC}"
echo ""

if [ $fail_count -eq 0 ]; then
    echo -e "${GREEN}✓✓✓ Phase 3 End-to-End Tests PASSED! ✓✓✓${NC}"
    echo ""
    echo "Next steps:"
    echo "  1. Flash to hardware: make flash BOARD=sensorwatch_pro"
    echo "  2. Manual testing: Verify metrics update every 15min"
    echo "  3. BKUP persistence: Test across sleep/wake cycles"
    echo "  4. Zone transitions: Verify hysteresis (no flicker)"
    echo ""
    echo "See docs/PHASE3_COMPLETE.md for full testing checklist."
    exit 0
else
    echo -e "${RED}✗✗✗ Some tests failed. Review output above. ✗✗✗${NC}"
    exit 1
fi
