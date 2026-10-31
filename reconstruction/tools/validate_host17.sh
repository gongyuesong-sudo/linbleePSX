#!/bin/bash
# validate_host17.sh — task-024 H-11b 验收：
#   A) rootcall 放行激活：warm 链 >134（新停机面前进）、ROOTCALL pass>=2、回填 >31
#   B) 引擎侧 BIOS-call 假说检验证据：BIOS-JIT-CALL 记录在场
#   C) 硬验收（任务原文）：引擎深段 deep(>0x4002b7) 唯一 eip>4 且 max>0x4002cf，且非 H_BIOSJIT 基线无回归
set -u
cd "$(dirname "$0")/.."
RC=0
ROM="${BLEEM_BIOS:-../psxbios/SCPH1001.BIN}"
[ -f "$ROM" ] || { echo "FAIL: ROM 缺失"; exit 1; }
gcc -m32 -O0 -g -no-pie -fno-pic -o host/build/htrace host/htrace.c host/mips_jit.c host/tramp32.s || { echo "FAIL build"; exit 1; }

echo "== A/B) H_BIOSJIT 引擎侧 + rootcall 放行"
HTRACE_LOG=host/build/htrace_host17.log H_ARENA_MB=320 H_SLED_CAP=200 H_BIOSJIT=1 H_JIT_BUDGET=8000 BLEEM_BIOS="$ROM" \
  timeout 280 ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin 0x400000 0x472000 >/dev/null 2>&1 || true
L=host/build/htrace_host17.log
CH=$(grep -a 'BIOS-JIT-CHAIN blocks=' "$L" | head -1 | sed 's/.*blocks=\([0-9]*\).*/\1/')
RCALL=$(grep -ac '^ROOTCALL' "$L")
BN=$(grep -a 'BIOS-BACKFILL n=' "$L" | head -1 | sed 's/.*n=\([0-9]*\).*/\1/')
JC=$(grep -ac '^BIOS-JIT-CALL' "$L")
echo "chain=$CH rootcall-pass=$RCALL backfill=$BN jit-calls=$JC"
[ "${CH:-0}" -gt 134 ]     || { echo "FAIL: 放行后链未超过旧停机面 134"; RC=1; }
[ "${RCALL:-0}" -ge 2 ]    || { echo "FAIL: rootcall 放行 <2 次"; RC=1; }
[ "${BN:-0}" -gt 31 ]      || { echo "FAIL: 回填未超过旧值 31"; RC=1; }
[ "${JC:-0}" -ge 1 ]       || { echo "FAIL: 引擎侧 BIOS-JIT-CALL 证据缺失"; RC=1; }

echo "== C) 引擎深段硬验收"
grep -aE '^004' "$L" | cut -c1-8 | sort -u > /tmp/h17_eips.$$
deep=$(awk '$0>"004002b7"' /tmp/h17_eips.$$ | wc -l)
mx=$(tail -1 /tmp/h17_eips.$$); rm -f /tmp/h17_eips.$$
echo "deep=$deep max=$mx"
[ "$deep" -gt 4 ]        || { echo "FAIL: deep=$deep (要求 >4)"; RC=1; }
[ "$mx" \> "004002cf" ]  || { echo "FAIL: max=$mx (要求 >004002cf)"; RC=1; }

# 基线卫生：重建无 ROM 131-eip 基线并跑 host7
HTRACE_LOG=host/build/htrace.log timeout 200 ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin 0x400000 0x472000 >/dev/null 2>&1 || true
bash tools/validate_host7.sh >/dev/null 2>&1 || { echo "FAIL: host7 基线回归"; RC=1; }

[ $RC -eq 0 ] && echo "HOST17 CHECK PASS: rootcall 放行 + 引擎深段推进达标" || echo "HOST17 CHECK FAIL"
exit $RC
