#!/usr/bin/env bash
# validate_host27.sh — task-035 H-16a: 金标准 shell 唤醒(0x83C58 时基) + 设备派发汇流
set -u
cd "$(dirname "$0")/.."
G=/home/gill/bleem/.analysis/golden/gstate.hdr
[ -f "$G" ] || { echo "FAIL: 金标准状态缺失"; exit 1; }
gcc -m32 -O0 -g -no-pie -I host -o host/build/golden_cpu_probe tools/golden_cpu_probe.c 2>/dev/null || { echo "FAIL: 编译"; exit 1; }

grep -qa "0x83C58" docs/27_shell_wake.md || { echo "FAIL: 轮询变量落档"; exit 1; }
echo "PASS A: 轮询变量 RAM 0x83C58 静态判读落档"

OUT=$(POKE=1 GSTATE="$G" ./host/build/golden_cpu_probe ../psxbios/SCPH1001.BIN 20000 2>/dev/null | tail -1)
echo "POKE: $OUT"
B=$(grep -o 'blocks=[0-9]*' <<<"$OUT" | cut -d= -f2)
O=$(grep -o 'outside=[0-9]*' <<<"$OUT" | cut -d= -f2)
P=$(grep -o 'pc=[0-9a-f]*' <<<"$OUT" | cut -d= -f2)
[ "${O:-0}" -gt 200 ] || { echo "FAIL: 越页=$O (要求>200=唤醒脱离空闲环)"; exit 1; }
# task-041 重锚: 旧终点 04500e81 为分支 flag-clobber-ra 伪像(奇数+1); 真语义终点 04500e82
[ "$P" = "04500e82" ] || { echo "FAIL: 汇流终点=$P (要求 04500e82, task-041 重锚)"; exit 1; }
echo "PASS B: 唤醒脱离轮询环 blocks=$B outside=$O, 达设备派发汇流节点 $P"

OUT2=$(GSTATE="$G" ./host/build/golden_cpu_probe ../psxbios/SCPH1001.BIN 2000 2>/dev/null | tail -1)
grep -q "fails=0" <<<"$OUT2" || { echo "FAIL: 无注入活性回归 $OUT2"; exit 1; }
echo "PASS C: 无注入活性回归零失败"

echo "HOST27 CHECK PASS: 金标准轮询唤醒(0x83C58 时基) + 双路径设备派发汇流实证"
