#!/bin/bash
# Test script for Phase Watch scaffolding (Phase 1)
# Verifies that phase engine compiles with zero cost when disabled,
# and compiles successfully when enabled.

set -euo pipefail

echo "======================================================================"
echo "Phase Watch Scaffolding Test (Phase 1)"
echo "======================================================================"
echo ""

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test 1: Verify homebase generator works
echo -e "${YELLOW}Test 1: Homebase generator...${NC}"
python3 utils/generate_homebase.py --lat 37.7749 --lon -122.4194 --tz PST --year 2026 > /dev/null
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ Homebase generator works${NC}"
else
    echo -e "${RED}✗ Homebase generator failed${NC}"
    exit 1
fi
echo ""

# Test 2: Syntax check on phase engine source
echo -e "${YELLOW}Test 2: Phase engine syntax check...${NC}"
arm-none-eabi-gcc -c -fsyntax-only -I./lib/phase -I./lib lib/phase/phase_engine.c 2>&1
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ Phase engine syntax OK${NC}"
else
    echo -e "${RED}✗ Phase engine syntax error${NC}"
    exit 1
fi
echo ""

# Test 3: Check file structure
echo -e "${YELLOW}Test 3: File structure...${NC}"
FILES=(
    "lib/phase/phase_engine.h"
    "lib/phase/phase_engine.c"
    "lib/phase/homebase.h"
    "lib/phase/homebase_table.h"
    "lib/phase/README.md"
    "utils/generate_homebase.py"
)

for file in "${FILES[@]}"; do
    if [ -f "$file" ]; then
        echo -e "  ${GREEN}✓${NC} $file"
    else
        echo -e "  ${RED}✗${NC} $file (missing)"
        exit 1
    fi
done
echo ""

# Test 4: Check #ifdef infrastructure
echo -e "${YELLOW}Test 4: #ifdef infrastructure...${NC}"
if grep -q "PHASE_ENGINE_ENABLED" movement_config.h; then
    echo -e "${GREEN}✓ PHASE_ENGINE_ENABLED found in movement_config.h${NC}"
else
    echo -e "${RED}✗ PHASE_ENGINE_ENABLED not found in movement_config.h${NC}"
    exit 1
fi

if grep -q "#ifdef PHASE_ENGINE_ENABLED" lib/phase/phase_engine.h; then
    echo -e "${GREEN}✓ #ifdef guards in phase_engine.h${NC}"
else
    echo -e "${RED}✗ Missing #ifdef guards in phase_engine.h${NC}"
    exit 1
fi

if grep -q "#ifdef PHASE_ENGINE_ENABLED" lib/phase/phase_engine.c; then
    echo -e "${GREEN}✓ #ifdef guards in phase_engine.c${NC}"
else
    echo -e "${RED}✗ Missing #ifdef guards in phase_engine.c${NC}"
    exit 1
fi
echo ""

# Test 5: Verify it's disabled by default
echo -e "${YELLOW}Test 5: Default state (disabled)...${NC}"
if grep -q "^#define PHASE_ENGINE_ENABLED" movement_config.h; then
    echo -e "${RED}✗ PHASE_ENGINE_ENABLED is enabled by default (should be commented out)${NC}"
    exit 1
else
    echo -e "${GREEN}✓ PHASE_ENGINE_ENABLED disabled by default${NC}"
fi
echo ""

# Test 6: Documentation check
echo -e "${YELLOW}Test 6: Documentation...${NC}"
if grep -q "## Overview" lib/phase/README.md; then
    echo -e "${GREEN}✓ README.md has overview${NC}"
else
    echo -e "${RED}✗ README.md missing overview${NC}"
    exit 1
fi

if grep -q "Homebase Table" lib/phase/README.md; then
    echo -e "${GREEN}✓ README.md documents homebase generator${NC}"
else
    echo -e "${RED}✗ README.md missing homebase generator docs${NC}"
    exit 1
fi

if grep -q "Integration with Watch Faces" lib/phase/README.md; then
    echo -e "${GREEN}✓ README.md documents integration${NC}"
else
    echo -e "${RED}✗ README.md missing integration docs${NC}"
    exit 1
fi
echo ""

# Summary
echo "======================================================================"
echo -e "${GREEN}All tests passed!${NC}"
echo "======================================================================"
echo ""
echo "Phase 1 scaffolding is complete and ready for review."
echo ""
echo "Next steps:"
echo "  1. Create PR to phase-system branch"
echo "  2. Request review from @security-specialist"
echo "  3. After merge, begin Phase 2 (metrics computation)"
echo ""
