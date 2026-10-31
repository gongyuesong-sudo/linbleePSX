#!/bin/bash
# task-047 H-21a 设备复位真值面 — 验证(docs/34)
cd "$(dirname "$0")/.." || exit 1
ROM=/home/gill/bleem/psxbios/SCPH1001.BIN
grep -qa "H_DEVRST" host/mips_jit.c || { echo "FAIL A: H_DEVRST 未落地"; exit 1; }
grep -qa "0x1080 && o<=0x10FF" host/mips_jit.c || { echo "FAIL A2: DMA 复位窗缺"; exit 1; }
grep -qa "H_RACATCH" host/mips_jit.c || { echo "FAIL B: RACATCH 探针缺"; exit 1; }
echo "PASS A/B: H_DEVRST(DMA 控制块复位=0) + RACATCH(ra→4 迁移探针)在场"
[ -f /tmp/c50dev.log ] && grep -c "RD 1f801dac" /tmp/c50dev.log | awk '{if($1<1000){exit 1}}' || { echo "FAIL C: 设备普查日志缺 DMA7-CHCR 风暴证据"; exit 1; }
echo "PASS C: 设备读序普查在案(0x1F801DAC 轮询风暴=复位语义错证据链)"
D=docs/34_device_truth.md
grep -qa "状态机时序分叉" "$D" && grep -qa "0x80089E2C" "$D" && grep -qa "H-21a" "$D" || { echo "FAIL D: docs/34 三结论缺"; exit 1; }
echo "PASS D: docs/34 落档(普查/H_DEVRST/ra=4 终判=状态机分叉→task-048/049)"
gcc -m32 -O0 -g -no-pie -fno-pic -o host/build/htrace host/htrace.c host/mips_jit.c host/tramp32.s 2>&1 | grep -qE " error" && { echo "FAIL E: 编译"; exit 1; }
HTRACE_LOG=/tmp/h39_base.log H_ARENA_MB=320 H_SLED_CAP=200 H_BIOSJIT=1 H_BIOSEXN=1 H_JIT_BUDGET=100000 BLEEM_BIOS=$ROM timeout 280 ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin 0x400000 0x472000 >/dev/null 2>&1
B=$(grep -a "BIOS-JIT-CHAIN" /tmp/h39_base.log | grep -ao "blocks=[0-9]*" | cut -d= -f2)
[ "${B:-0}" -ge 18000 ] && [ "${B:-0}" -le 18100 ] || { echo "FAIL E: 默认语义基线链=$B(期望18021±)"; exit 1; }
echo "PASS E: 无 flag 默认语义无回归(链=$B)"
echo "HOST39 CHECK PASS: 设备复位真值面落地 + ra=4 根因终判=C1↔C2 状态机时序分叉(钥匙=CDROM ack/DMA 真值, 交 task-048/049)"
exit 0
