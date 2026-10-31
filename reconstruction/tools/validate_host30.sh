#!/usr/bin/env bash
# validate_host30.sh — task-038 H-17b: homebrew 在真 BIOS+JIT+金标准 RAM 端到端运行
set -u
cd "$(dirname "$0")/.."
G=/home/gill/bleem/.analysis/golden/gstate.hdr
[ -f "$G" ] || { echo "FAIL: 金标准缺失"; exit 1; }
mkdir -p host/build
gcc -m32 -O0 -g -no-pie -I host -o host/build/golden_cpu_probe tools/golden_cpu_probe.c 2>/dev/null || { echo "FAIL: probe 编译"; exit 1; }
python3 tools/hbrew_min.py host/build/hbrew_min.exe >/dev/null || { echo "FAIL: min 组装"; exit 1; }
python3 tools/hbrew_build.py host/build/hbrew.exe >/dev/null || { echo "FAIL: full 组装"; exit 1; }
grep -qa "target_idx-(branch_idx+1)" docs/28_homebrew_e2e.md || { echo "FAIL: 分支公式勘误落档"; exit 1; }
echo "PASS A: 组装器/探针构建 + 分支公式勘误落档"

OUT=$(HBBASE=80020000 HBEXE=host/build/hbrew_min.exe HB=1 GSTATE="$G" ./host/build/golden_cpu_probe ../psxbios/SCPH1001.BIN 20000 2>/dev/null | tail -1); RC=$?
echo "MIN: $OUT"
{ [ $RC -eq 0 ] && grep -q "cnt=2000" <<<"$OUT" && grep -q "gpuw=2000" <<<"$OUT" && grep -q "breaks=1" <<<"$OUT" && grep -q "fails=0" <<<"$OUT"; } || { echo "FAIL: MIN"; exit 1; }
echo "PASS B: 极简程序闭环(内存标记 2000/GP0 trap 2000/程序 break 停机/零失败)"

OUT2=$(HB=1 GSTATE="$G" ./host/build/golden_cpu_probe ../psxbios/SCPH1001.BIN 300000 2>/dev/null | tail -1); RC2=$?
echo "FULL: $OUT2"
{ [ $RC2 -eq 0 ] && grep -q "cnt=10000" <<<"$OUT2" && grep -q "gpuw=10000" <<<"$OUT2" && grep -q "dst=454d4f48" <<<"$OUT2" && grep -q "breaks=1" <<<"$OUT2" && grep -q "fails=0" <<<"$OUT2" && grep -q "pc=bfc4c1" <<<"$OUT2"; } || { echo "FAIL: FULL"; exit 1; }
echo "PASS C: FULL 闭环(10000 拍计数+GP0 trap+t9 核面调用 A(42) strcpy 'HOME' 落地+ROM 哨兵停机)"

TICK=1 GSTATE="$G" ./host/build/golden_cpu_probe ../psxbios/SCPH1001.BIN 300000 >/dev/null 2>&1 || { echo "FAIL: TICK 回归"; exit 1; }
bash tools/validate_host29.sh >/dev/null || { echo "FAIL: host29 回归"; exit 1; }
echo "PASS D: TICK 时基闭环 + host29 回归全绿"
echo "HOST30 CHECK PASS: 第一方 homebrew 端到端(真 BIOS 码+JIT+金标准 RAM+设备层+核面调用)"
