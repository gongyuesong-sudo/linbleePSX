#!/bin/bash
# task-043 H-20 homebrew C2 根协议面供给首攻 — 验证(docs/32)
cd "$(dirname "$0")/.." || exit 1
ROM=/home/gill/bleem/psxbios/SCPH1001.BIN
[ -f "$ROM" ] || { echo "FAIL A: ROM 缺失"; exit 1; }
gcc -m32 -O0 -g -no-pie -DJIT_MAIN -o host/build/mips_jit host/mips_jit.c 2>&1 | grep -qE " error" && { echo "FAIL: JIT 编译"; exit 1; }
./host/build/mips_jit | grep -q "JIT-CHECK: PASS" || { echo "FAIL B: JIT 单测(含 bltz/div 新语义)"; exit 1; }
echo "PASS B: JIT 单测 PASS(REGIMM bltz/MIPS-I funct MFHI/DIVU 新面并入, 无回归)"
gcc -m32 -O0 -g -no-pie -fno-pic -o host/build/htrace host/htrace.c host/mips_jit.c host/tramp32.s 2>&1 | grep -qE " error" && { echo "FAIL: htrace 编译"; exit 1; }
run(){ local tag=$1; shift; HTRACE_LOG=/tmp/h35_$tag.log H_ARENA_MB=320 H_SLED_CAP=200 H_BIOSJIT=1 H_BIOSEXN=1 H_JITBLOCKS=1 H_JIT_BUDGET=100000 BLEEM_BIOS=$ROM timeout 280 ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin 0x400000 0x472000 >/dev/null 2>&1; }
run base
B=$(grep -a "BIOS-JIT-CHAIN" /tmp/h35_base.log | grep -ao "blocks=[0-9]*" | cut -d= -f2)
[ "${B:-0}" -ge 18000 ] && [ "${B:-0}" -le 18100 ] || { echo "FAIL C: 基线链=$B(期望18021±)"; exit 1; }
echo "PASS C: 无 C2HOME 基线链=$B(rootcall NewRootCmd 停机面不变)"
H_C2HOME=1 run c2h
C=$(grep -a "BIOS-JIT-CHAIN" /tmp/h35_c2h.log | grep -ao "blocks=[0-9]*" | cut -d= -f2)
N=$(grep -ac "BLK 00040100" /tmp/h35_c2h.log)
[ "${C:-0}" -ge 21000 ] || { echo "FAIL D: C2HOME 链=$C(期望>=21000)"; exit 1; }
[ "${N:-0}" -ge 100 ] || { echo "FAIL E: handler 执行=$N(期望>=100)"; exit 1; }
grep -aq "C2HOME install" /tmp/h35_c2h.log || { echo "FAIL E2: 无安装日志"; exit 1; }
echo "PASS D/E: C2HOME 链=$C handler=${N}次 — 冷跳链越过 rootcall NewRootCmd 原停机面"
F=$(grep -a "BIOS-JIT-CHAIN" /tmp/h35_c2h.log | grep -ao "final=[0-9a-f]*" | cut -d= -f2)
case "$F" in bfc012a0|00000100|bfc01280) echo "PASS F: 新边界终态 final=$F(RAM 向量表数据面/深 ROM 块, 与 docs/32 §2.3 一致)";; *) echo "FAIL F: 新停机面漂移 final=$F"; exit 1;; esac
grep -aq "divu" docs/32_c2home_supply.md && grep -aq "0x674+t1" docs/32_c2home_supply.md || { echo "FAIL G: docs/32 缺关键结论"; exit 1; }
echo "PASS G: docs/32 判据/教训落档"
echo "HOST35 CHECK PASS: C2 根槽懒填(不 flush 保缓存派发块)+bltz/MFHI/DIVU JIT 勘误 + 链 18021->$C + NewRootCmd 结构 CB 为下一步前置(task-044)"
exit 0
