#!/usr/bin/env bash

# Terminal Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

VM_BIN="./bin/slox"  # Change to your compiled VM binary name
TEST_DIR="./tests"
PASSED=0
FAILED=0

# Ensure the VM binary exists
if [ ! -f "$VM_BIN" ]; then
    echo -e "${RED}Error: VM binary '$VM_BIN' not found. Compile your project first!${NC}"
    exit 1
fi

echo -e "${YELLOW}Running VM Test Suite...${NC}"
echo "=================================================="

# Use find to locate all .lox files (handles directories recursively)
while IFS= read -r -d '' test_file; do
    # 1. Create secure temp files for comparison
    expected_tmp=$(mktemp)
    actual_tmp=$(mktemp)

    # 2. Extract '// expect: <value>' lines into the expected file
    grep '// expect:' "$test_file" | sed 's/.*\/\/ expect: //' > "$expected_tmp"

    # If the test file doesn't have any expectations, skip it
    if [ ! -s "$expected_tmp" ]; then
        rm -f "$expected_tmp" "$actual_tmp"
        continue
    fi

    # 3. Run the VM and direct stdout/stderr to the actual file
    "$VM_BIN" "$test_file" > "$actual_tmp" 2>&1

    # 4. Compare expected vs actual output
    if diff -u "$expected_tmp" "$actual_tmp" > /dev/null; then
        echo -e "  [${GREEN}PASS${NC}] $test_file"
        ((PASSED++))
    else
        echo -e "  [${RED}FAIL${NC}] $test_file"
        echo -e "  ${YELLOW}--------------------------------------------------${NC}"
        echo "  Expected (Left) vs Actual Output (Right):"
        # Print a side-by-side diff of the mismatch
        diff -y -W 60 "$expected_tmp" "$actual_tmp" | sed 's/^/  /'
        echo -e "  ${YELLOW}--------------------------------------------------${NC}"
        ((FAILED++))
    fi

    # Clean up temp files
    rm -f "$expected_tmp" "$actual_tmp"

done < <(find "$TEST_DIR" -name "*.lox" -print0)

echo "=================================================="
echo -e "Tests Passed: ${GREEN}$PASSED${NC}"
echo -e "Tests Failed: ${RED}$FAILED${NC}"

if [ "$FAILED" -eq 0 ]; then
    echo -e "Status:       ${GREEN}ALL PASSED! 🎉${NC}"
    exit 0
else
    echo -e "Status:       ${RED}SOME TESTS FAILED ❌${NC}"
    exit 1
fi
