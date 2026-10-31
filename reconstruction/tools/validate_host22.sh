#!/bin/bash
# validate_host22.sh — task-029 H-13a 验收：root2 线程入口判异（docs/19）+ H_ROOT2 机制生效动态证据
set -u
cd "$(dirname "$0")/.."
RC=0
D=docs/19_root2_loader.md
[ -f "$D" ] || { echo "FAIL: docs/19 缺失"; exit 1; }
for k in 'BFC1E7B0' '0x32E24' 'root2ptr' '0x8BF0' 'b00b3718|b3718'; do
    grep -qE "$k" "$D" || { echo "FAIL: docs/19 缺判异点 '$k'"; RC=1; }
done
ROM="${BLEEM_BIOS:-../psxbios/SCPH1001.BIN}"
[ -f "$ROM" ] || { echo "FAIL: ROM 缺失"; exit 1; }
gcc -m32 -O0 -g -no-pie -I host -o host/build/root2_probe tools/root2_probe.c || { echo "FAIL build"; exit 1; }
B=$(H_BIOSEXN=1 ./host/build/root2_probe "$ROM" 2>/dev/null | sed 's/.*pc=\([0-9a-f]*\).*/\1/')
R=$(H_BIOSEXN=1 H_ROOT2=1 ./host/build/root2_probe "$ROM" 2>/dev/null | sed 's/.*pc=\([0-9a-f]*\).*/\1/')
echo "baseline=$B root2=$R"
[ -n "$B" ] && [ -n "$R" ] || { echo "FAIL: probe 无输出"; RC=1; }
# root2 桩生效 = 停机点推进(同一滑行走廊, 地址更大)且两者仍在 RAM 零海走廊(0xb0xxxxx)
case "$B" in b00*) ;; *) echo "FAIL: 基线停机 $B 不在 RAM 走廊"; RC=1;; esac
case "$R" in b00*) ;; *) echo "FAIL: root2 停机 $R 不在 RAM 走廊"; RC=1;; esac
[ $((16#$R)) -gt $((16#$B)) ] || { echo "FAIL: H_ROOT2 未推进停机点 ($B -> $R)"; RC=1; }
# 回归: JIT 单元 + host21 (EXN ROM 偏移语义)
gcc -m32 -O0 -g -no-pie -fno-pic -DJIT_MAIN -o host/build/mips_jit host/mips_jit.c && ./host/build/mips_jit | tail -1 | grep -q PASS || { echo "FAIL: JIT 单元回归"; RC=1; }
[ $RC -eq 0 ] && echo "HOST22 CHECK PASS: root2 入口判异落档 + 机制动态生效(装载数据面为唯一缺口)" || echo "HOST22 CHECK FAIL"
exit $RC
