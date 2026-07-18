#!/usr/bin/env bash
# Usage: ./generate_expectations.sh examples/my_script.lox

FILE=$1
VM_BIN="./bin/slox"

# 1. Run the VM and filter out stack traces (using the same logic as your runner)
# This keeps the expected output clean and portable
"$VM_BIN" "$FILE" 2>&1 | grep -vE '^\[.*:[0-9]+\] in .+' > temp_output.txt

# 2. Add the '// expect: ' prefix to every line
sed 's/^/\/\/ expect: /' temp_output.txt > expectations.txt

# 3. Append to the original file
cat expectations.txt >> "$FILE"

# 4. Cleanup
rm temp_output.txt expectations.txt
echo "Added expectations to $FILE"
