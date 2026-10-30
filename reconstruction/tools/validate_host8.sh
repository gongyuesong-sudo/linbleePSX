#!/usr/bin/env bash
# validate_host8.sh — task-015 validator: 槽表几何 + 区带分类实证在案
set -u
cd "$(dirname "$0")/.."
fail(){ echo "FAIL: $*"; exit 1; }
rep=$(python3 tools/analyze_jit_tables.py) || fail "analyze_jit_tables.py non-zero"
echo "$rep" | grep -q '^table_geometry=PASS$' || fail "table geometry not PASS"
echo "$rep" | grep -q 'table 0x5ac4c..0x69004 n=14574 stride=4 arena=0x7680..0x40560' || fail "table spec line mismatch"
echo "$rep" | grep -Eq '^slots fillA=[0-9]+ fillB=[0-9]+ zero=[0-9]+ real=[0-9]+$' || fail "slot zone counts line missing"
echo "HOST8 CHECK PASS: slot-offset table spec (n=14574 stride=4) + slot classification counts proven"
