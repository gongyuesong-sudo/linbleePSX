#!/usr/bin/env bash
# validate_host28.sh — task-036 H-16b: 时基闭环长跑 30M 块 + 周期计数下界
set -u
cd "$(dirname "$0")/.."
G=/home/gill/bleem/.analysis/golden/gstate.hdr
[ -f "$G" ] || { echo "FAIL: 金标准缺失"; exit 1; }
gcc -m32 -O0 -g -no-pie -I host -o host/build/golden_cpu_probe tools/golden_cpu_probe.c 2>/dev/null || { echo "FAIL: 编译"; exit 1; }
grep -qa "0x044F4C01" docs/27_shell_wake.md || { echo "FAIL: 死模块判异落档"; exit 1; }
echo "PASS A: 死模块 ra=0x044F4C01 判异落档"
OUT=$(TICK=1 GSTATE="$G" ./host/build/golden_cpu_probe ../psxbios/SCPH1001.BIN 30000000 2>/dev/null | tail -1); RC=$?
echo "RUN: $OUT"
[ $RC -eq 0 ] || { echo "FAIL: TICK rc=$RC"; exit 1; }
C=$(grep -o 'cycles=[0-9]*' <<<"$OUT" | cut -d= -f2); B=$(grep -o 'blocks=[0-9]*' <<<"$OUT" | cut -d= -f2)
grep -q "fails=0" <<<"$OUT" || { echo "FAIL: fails!=0"; exit 1; }
[ "${B:-0}" -ge 30000000 ] || { echo "FAIL: blocks=$B"; exit 1; }
[ "${C:-0}" -ge 5000000 ] || { echo "FAIL: cycles=$C (要求>=5e6)"; exit 1; }
echo "PASS B: 30M 块连续运行 ${C} 唤醒周期零失败"
timeout 300 bash tools/validate_host26.sh >/dev/null && timeout 300 bash tools/validate_host27.sh >/dev/null && echo "PASS C: host26/27 回归" || { echo "FAIL: 回归"; exit 1; }
echo "HOST28 CHECK PASS: 宿主时基闭环(唤醒-哨兵-重导引) 30M 块量化动态性"
