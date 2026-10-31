#!/bin/bash
# validate_host20.sh — task-027 H-12c 验收：分发器(EXN)激活后 htrace 引擎 trace 深段推进
#   硬指标：引擎侧 deep(>0x4002b7) 唯一 eip > 4 且 max > 004002cf；基线卫生 host7。
set -u
cd "$(dirname "$0")/.."
RC=0
ROM="${BLEEM_BIOS:-../psxbios/SCPH1001.BIN}"
[ -f "$ROM" ] || { echo "FAIL: ROM 缺失"; exit 1; }
gcc -m32 -O0 -g -no-pie -fno-pic -o host/build/htrace host/htrace.c host/mips_jit.c host/tramp32.s || { echo "FAIL build"; exit 1; }
HTRACE_LOG=host/build/htrace_host20.log H_ARENA_MB=320 H_SLED_CAP=3000 H_BIOSJIT=1 H_BIOSEXN=1 H_JIT_BUDGET=8000 BLEEM_BIOS="$ROM" \
  timeout 280 ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin 0x400000 0x472000 >/dev/null 2>&1 || true
L=host/build/htrace_host20.log
grep -aq 'EXN .*-> h=bfc1e988' "$L" || { echo "FAIL: EXN 分发器未激活"; RC=1; }
grep -aE '^004' "$L" | cut -c1-8 | sort -u > /tmp/h20e.$$
deep=$(awk '$0>"004002b7"' /tmp/h20e.$$ | wc -l); mx=$(tail -1 /tmp/h20e.$$); rm -f /tmp/h20e.$$
echo "deep=$deep max=$mx"
[ "$deep" -gt 4 ]       || { echo "FAIL: deep=$deep (要求 >4)"; RC=1; }
[ "$mx" \> "004002cf" ] || { echo "FAIL: max=$mx"; RC=1; }
HTRACE_LOG=host/build/htrace.log timeout 200 env -u BLEEM_BIOS -u H_BIOSJIT ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin 0x400000 0x472000 >/dev/null 2>&1 || true
bash tools/validate_host7.sh >/dev/null 2>&1 || { echo "FAIL: host7 基线"; RC=1; }
[ $RC -eq 0 ] && echo "HOST20 CHECK PASS: 分发器激活推动引擎深段推进" || echo "HOST20 CHECK FAIL"
exit $RC
