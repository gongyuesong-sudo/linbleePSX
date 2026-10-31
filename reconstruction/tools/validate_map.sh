#!/bin/bash
# validate_map.sh — task-004 validation: docs/07_engine_map.md completeness
set -e
cd "$(dirname "$0")/.."
F=docs/07_engine_map.md
[ -s "$F" ] || { echo "FAIL: $F missing/empty"; exit 1; }
for sec in "函数×字符串" "Win32 API 依赖" "内存布局"; do
  grep -q "$sec" "$F" || { echo "FAIL: missing section $sec"; exit 1; }
done
# quantified floors
nstr=$(grep -c '^\- `' "$F" || true)
napi=$(grep -c '^| ' "$F" || true)
[ "$nstr" -ge 40 ] || { echo "FAIL: string refs too few ($nstr < 40)"; exit 1; }
[ "$napi" -ge 40 ] || { echo "FAIL: table rows too few ($napi < 40)"; exit 1; }
grep -q '0x4022a4' "$F" || { echo "FAIL: API slot table base absent"; exit 1; }
grep -q '0xa0000' "$F" || { echo "FAIL: 640KB RAM layout absent"; exit 1; }
echo "MAP CHECK PASS: string-refs=$nstr table-rows=$napi"
