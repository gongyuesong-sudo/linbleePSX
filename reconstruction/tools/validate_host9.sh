#!/usr/bin/env bash
# validate_host9.sh — task-016 validator: 172 记录区结构 + 支撑库锚点 + docs/11 布局表
set -u
cd "$(dirname "$0")/.."
fail(){ echo "FAIL: $*"; exit 1; }
rep=$(python3 tools/analyze_jit_tables.py) || fail "analyze_jit_tables.py non-zero"
echo "$rep" | grep -q '^zone172_structure=PASS$' || fail "172-zone structure not PASS"
echo "$rep" | grep -q '^runtime_lib_anchor_0x598aa=PASS$' || fail "runtime-lib anchor missing"
test -s docs/11_template_map.md || fail "docs/11 missing"
grep -q '0x5AC4C' docs/11_template_map.md || fail "docs/11 must fix table geometry"
grep -q '0x2CB80' docs/11_template_map.md || fail "docs/11 must cover 172-record zone"
grep -q '0x598AA' docs/11_template_map.md || fail "docs/11 must document runtime-lib anchor"
grep -q '14574' docs/11_template_map.md || fail "docs/11 n=14574 missing"
echo "HOST9 CHECK PASS: 172-record zone + runtime-lib anchor + docs/11 layout map"
