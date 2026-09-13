#!/bin/bash
# Test script for Phase 3C PR 4: Zone Faces
# Verifies compilation, display constraints, and basic structure

set -euo pipefail

echo "=== Zone Faces Test Suite ==="
echo ""

# Color codes for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

FACE_NAMES=("emergence_face" "momentum_face" "active_face" "descent_face")
ZONE_INDICATORS=("EM" "MO" "AC" "DE")
EXPECTED_VIEWS=(3 3 3 2)  # Number of views per face

echo "Test 1: Display String Constraints"
echo "-----------------------------------"

# Check that all display strings fit 7-segment constraints (10 chars max)
for i in "${!FACE_NAMES[@]}"; do
    FACE="${FACE_NAMES[$i]}"
    ZONE="${ZONE_INDICATORS[$i]}"
    
    echo -n "  ${FACE}: "
    
    # Extract all snprintf format strings from the .c file
    STRINGS=$(grep -o 'snprintf.*"[^"]*"' "watch-faces/complication/${FACE}.c" | grep -o '"[^"]*"' | tr -d '"')
    
    MAX_LEN=0
    LONGEST_STR=""
    while IFS= read -r str; do
        # Calculate actual length (accounting for format specifiers)
        # %2d = 2 chars, %d = up to 3 chars (for 0-100)
        ACTUAL_LEN=$(echo "$str" | sed 's/%2d/XX/g' | sed 's/%d/XXX/g' | wc -c)
        ACTUAL_LEN=$((ACTUAL_LEN - 1))  # Remove newline
        
        if [ "$ACTUAL_LEN" -gt "$MAX_LEN" ]; then
            MAX_LEN=$ACTUAL_LEN
            LONGEST_STR="$str"
        fi
    done <<< "$STRINGS"
    
    if [ "$MAX_LEN" -le 10 ]; then
        echo -e "${GREEN}✓${NC} Max length: $MAX_LEN chars"
    else
        echo -e "${RED}✗${NC} FAIL: Max length $MAX_LEN > 10 (string: '$LONGEST_STR')"
        exit 1
    fi
done

echo ""
echo "Test 2: Zone Indicator Verification"
echo "------------------------------------"

for i in "${!FACE_NAMES[@]}"; do
    FACE="${FACE_NAMES[$i]}"
    ZONE="${ZONE_INDICATORS[$i]}"
    
    echo -n "  ${FACE}: "
    
    # Check that zone indicator appears in code
    if grep -q "\"${ZONE}\"" "watch-faces/complication/${FACE}.c"; then
        echo -e "${GREEN}✓${NC} Zone indicator '${ZONE}' present"
    else
        echo -e "${RED}✗${NC} FAIL: Zone indicator '${ZONE}' not found"
        exit 1
    fi
done

echo ""
echo "Test 3: View Count Verification"
echo "--------------------------------"

for i in "${!FACE_NAMES[@]}"; do
    FACE="${FACE_NAMES[$i]}"
    EXPECTED="${EXPECTED_VIEWS[$i]}"
    
    echo -n "  ${FACE}: "
    
    # Count case statements in switch (view_index)
    ACTUAL=$(grep -A 20 "switch (state->view_index)" "watch-faces/complication/${FACE}.c" | grep -c "case [0-9]:" || true)
    
    if [ "$ACTUAL" -eq "$EXPECTED" ]; then
        echo -e "${GREEN}✓${NC} ${ACTUAL} views (expected ${EXPECTED})"
    else
        echo -e "${RED}✗${NC} FAIL: Found ${ACTUAL} views, expected ${EXPECTED}"
        exit 1
    fi
done

echo ""
echo "Test 4: Structure Verification"
echo "-------------------------------"

for FACE in "${FACE_NAMES[@]}"; do
    echo -n "  ${FACE}: "
    
    ERRORS=0
    
    # Check for required functions
    for FUNC in "setup" "activate" "loop" "resign"; do
        if ! grep -q "${FACE}_${FUNC}" "watch-faces/complication/${FACE}.c"; then
            echo -e "${RED}✗${NC} Missing function: ${FACE}_${FUNC}"
            ERRORS=$((ERRORS + 1))
        fi
    done
    
    # Check for state struct
    if ! grep -q "${FACE}_state_t" "watch-faces/complication/${FACE}.h"; then
        echo -e "${RED}✗${NC} Missing state struct in header"
        ERRORS=$((ERRORS + 1))
    fi
    
    # Check for PHASE_ENGINE_ENABLED guard
    if ! grep -q "#ifdef PHASE_ENGINE_ENABLED" "watch-faces/complication/${FACE}.c"; then
        echo -e "${RED}✗${NC} Missing PHASE_ENGINE_ENABLED guard"
        ERRORS=$((ERRORS + 1))
    fi
    
    # Check for metrics include
    if ! grep -q '#include.*metrics.h' "watch-faces/complication/${FACE}.c"; then
        echo -e "${RED}✗${NC} Missing metrics.h include"
        ERRORS=$((ERRORS + 1))
    fi
    
    if [ "$ERRORS" -eq 0 ]; then
        echo -e "${GREEN}✓${NC} All structure checks passed"
    else
        exit 1
    fi
done

echo ""
echo "Test 5: Button Handler Verification"
echo "------------------------------------"

for FACE in "${FACE_NAMES[@]}"; do
    echo -n "  ${FACE}: "
    
    ERRORS=0
    
    # Check for ALARM button handler (cycles views)
    if ! grep -q "EVENT_ALARM_BUTTON_UP" "watch-faces/complication/${FACE}.c"; then
        echo -e "${RED}✗${NC} Missing ALARM button handler"
        ERRORS=$((ERRORS + 1))
    fi
    
    # Check for LIGHT button handler (LED)
    if ! grep -q "EVENT_LIGHT_BUTTON_UP" "watch-faces/complication/${FACE}.c"; then
        echo -e "${RED}✗${NC} Missing LIGHT button handler"
        ERRORS=$((ERRORS + 1))
    fi
    
    # Check for illuminate LED call
    if ! grep -q "movement_illuminate_led" "watch-faces/complication/${FACE}.c"; then
        echo -e "${RED}✗${NC} Missing LED illuminate call"
        ERRORS=$((ERRORS + 1))
    fi
    
    if [ "$ERRORS" -eq 0 ]; then
        echo -e "${GREEN}✓${NC} All button handlers present"
    else
        exit 1
    fi
done

echo ""
echo "Test 6: Build Integration"
echo "-------------------------"

# Check watch-faces.mk
echo -n "  watch-faces.mk: "
ERRORS=0
for FACE in "${FACE_NAMES[@]}"; do
    if ! grep -q "${FACE}.c" watch-faces.mk; then
        echo -e "${RED}✗${NC} ${FACE}.c not in watch-faces.mk"
        ERRORS=$((ERRORS + 1))
    fi
done
if [ "$ERRORS" -eq 0 ]; then
    echo -e "${GREEN}✓${NC} All faces in watch-faces.mk"
else
    exit 1
fi

# Check movement_faces.h
echo -n "  movement_faces.h: "
ERRORS=0
for FACE in "${FACE_NAMES[@]}"; do
    if ! grep -q "${FACE}.h" movement_faces.h; then
        echo -e "${RED}✗${NC} ${FACE}.h not in movement_faces.h"
        ERRORS=$((ERRORS + 1))
    fi
done
if [ "$ERRORS" -eq 0 ]; then
    echo -e "${GREEN}✓${NC} All headers in movement_faces.h"
else
    exit 1
fi

echo ""
echo "Test 7: Metric Display Verification"
echo "------------------------------------"

# Verify each face displays its documented metrics (using arrays compatible with older bash)
METRICS_emergence_face="SD EM CMF"
METRICS_momentum_face="WK SD TE"
METRICS_active_face="NRG EM SD"
METRICS_descent_face="CMF EM"

for FACE in "${FACE_NAMES[@]}"; do
    echo -n "  ${FACE}: "
    
    # Get expected metrics for this face
    VARNAME="METRICS_${FACE}"
    EXPECTED_METRICS="${!VARNAME}"
    ERRORS=0
    
    for METRIC in $EXPECTED_METRICS; do
        # Check for metric access in code (check both metric struct and display strings)
        METRIC_LOWER=$(echo "$METRIC" | tr '[:upper:]' '[:lower:]')
        if ! grep -q "metrics\\.${METRIC_LOWER}" "watch-faces/complication/${FACE}.c" && \
           ! grep -q "\"${METRIC}" "watch-faces/complication/${FACE}.c"; then
            echo -e "${RED}✗${NC} Metric ${METRIC} not displayed"
            ERRORS=$((ERRORS + 1))
        fi
    done
    
    if [ "$ERRORS" -eq 0 ]; then
        echo -e "${GREEN}✓${NC} All metrics displayed: ${EXPECTED_METRICS}"
    else
        exit 1
    fi
done

echo ""
echo "=== All Tests Passed ==="
echo ""
echo -e "${GREEN}✓ Display constraints verified (≤10 chars)${NC}"
echo -e "${GREEN}✓ Zone indicators correct${NC}"
echo -e "${GREEN}✓ View counts match spec${NC}"
echo -e "${GREEN}✓ Face structure complete${NC}"
echo -e "${GREEN}✓ Button handlers implemented${NC}"
echo -e "${GREEN}✓ Build integration confirmed${NC}"
echo -e "${GREEN}✓ Metrics display validated${NC}"
echo ""
echo "Ready for compilation test:"
echo "  make clean"
echo "  make"
echo ""
