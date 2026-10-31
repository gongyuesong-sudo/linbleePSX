#!/bin/bash
# validate_host16.sh — task-023 H-11a 验收：
#   A) 普查脚本复跑 rc=0（A 表/rootcall/停机面断言内建）
#   B) docs/14 存在且含关键结论（A 表 @0x4300 34 真实项、停机面 BFC0D964、无 COP 依赖）
#   C) 事实新鲜度：host14 JIT 验收仍 PASS（链 134 块停机面未因 H-10b 集成而漂移）
set -u
cd "$(dirname "$0")/.."
RC=0
python3 tools/scan_bios_syscalls.py > /tmp/h16_scan.txt 2>&1 || { echo "FAIL: 普查脚本 rc!=0"; tail -5 /tmp/h16_scan.txt; RC=1; }
grep -q 'A-TABLE base=0x04300 entries=44 real=34' /tmp/h16_scan.txt || { echo "FAIL: A 表结构断言缺失"; RC=1; }
grep -q 'VERDICT ok' /tmp/h16_scan.txt || { echo "FAIL: 普查 VERDICT 缺失"; RC=1; }
test -s docs/14_bios_calls.md || { echo "FAIL: docs/14 缺失"; RC=1; }
for kw in '0x4300' 'BFC0D964' '无 COP0/COP2 依赖' 'rootcall'; do
  grep -q "$kw" docs/14_bios_calls.md || { echo "FAIL: docs/14 缺少结论 '$kw'"; RC=1; }
done
bash tools/validate_host14.sh >/tmp/h16_jit.txt 2>&1 || { echo "FAIL: host14 JIT 回归"; tail -3 /tmp/h16_jit.txt; RC=1; }
[ $RC -eq 0 ] && echo "HOST16 CHECK PASS: BIOS A-call 静态破译落档（A表0x4300×44/34真实、rootcall桩6、warm停机面=BFC0D964 cmd=1、A处理器无COP依赖→现有JIT可执行）" || echo "HOST16 CHECK FAIL"
exit $RC
