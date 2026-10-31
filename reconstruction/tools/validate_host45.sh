#!/usr/bin/env bash
# validate_host45.sh — M-2 文档终稿验证（task-055，2026-10-31）
# A) docs/01..42 + evidence_ledger 全存在(43 篇); B) README 关键锚; C) 长度合理; D) SHA 行在; E) 旧"5 篇"表述已清除
set -u
cd "$(dirname "$0")/.."
fail=0
note(){ echo "HOST45: $*"; }
n=0
for i in $(seq -w 1 58); do [ -f docs/${i}_*.md ] && n=$((n+1)); done
[ -f docs/evidence_ledger.md ] && n=$((n+1))
[ "$n" -eq 59 ] && note "A) docs 全 59 篇在" || { note "A) docs 计数=$n != 59"; fail=1; }
for pat in "59 篇" "H-23" "DuckStation" "validate_host44" "X11 selftest PASS"; do
  grep -q "$pat" README.md || { note "B) README 缺锚: $pat"; fail=1; }
done
[ "$fail" -eq 0 ] && note "B) README 五锚全在"
L=$(wc -l < README.md); [ "$L" -ge 60 ] && [ "$L" -le 200 ] && note "C) README 行数=$L 合理" || { note "C) 行数异常 $L"; fail=1; }
grep -q "b61ee13e2377e8305ede4e8be9752f83163368fadb19f0b3e40774d64e7e09da" README.md && note "D) 目标 SHA 行在" || { note "D) SHA 丢失"; fail=1; }
grep -q "5 篇分析报告" README.md && { note "E) 旧表述残留"; fail=1; } || note "E) 旧'5 篇'已清除"
[ "$fail" -eq 0 ] && echo "HOST45 CHECK PASS" || echo "HOST45 CHECK FAIL"
[ "$fail" -eq 0 ]
