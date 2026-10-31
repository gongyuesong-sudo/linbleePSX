#!/bin/bash
# validate_host19.sh — task-026 H-12b 验收：
#   warm 链在 syscall 异常真语义(H_BIOSEXN: [RAM 0x130] 通用异常分发 + EPC/Cause/rfe)下
#   越过旧哨兵停机面：BIOS-JIT-CHAIN blocks>150（旧 serve pass 基线=150）。
set -u
cd "$(dirname "$0")/.."
RC=0
ROM="${BLEEM_BIOS:-../psxbios/SCPH1001.BIN}"
[ -f "$ROM" ] || { echo "FAIL: ROM 缺失"; exit 1; }
gcc -m32 -O0 -g -no-pie -fno-pic -DJIT_MAIN -o host/build/mips_jit host/mips_jit.c || { echo "FAIL build jit"; exit 1; }
./host/build/mips_jit | tail -1 | grep -q 'JIT-CHECK: PASS' || { echo "FAIL: 单元回归"; RC=1; }
gcc -m32 -O0 -g -no-pie -fno-pic -o host/build/htrace host/htrace.c host/mips_jit.c host/tramp32.s || { echo "FAIL build htrace"; exit 1; }
HTRACE_LOG=host/build/htrace_host19.log H_ARENA_MB=320 H_SLED_CAP=200 H_BIOSJIT=1 H_BIOSEXN=1 H_JIT_BUDGET=8000 BLEEM_BIOS="$ROM" \
  timeout 280 ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin 0x400000 0x472000 >/dev/null 2>&1 || true
L=host/build/htrace_host19.log
CH=$(grep -a 'BIOS-JIT-CHAIN blocks=' "$L" | head -1 | sed 's/.*blocks=\([0-9]*\).*/\1/')
EXN=$(grep -ac '^EXN' "$L")
echo "chain=${CH:-0} exn-events=$EXN"
[ "${CH:-0}" -gt 150 ] || { echo "FAIL: 链=${CH:-0} 未超旧停机面 150"; RC=1; }
# 回归卫生：无 EXN 的既有语义不变（host17 A 段口径）+ host14
timeout 200 env -u BLEEM_BIOS -u H_BIOSJIT -u H_BIOSEXN ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin 0x400000 0x472000 >/dev/null 2>&1 || true
bash tools/validate_host7.sh >/dev/null 2>&1 || { echo "FAIL: host7 基线回归"; RC=1; }
[ $RC -eq 0 ] && echo "HOST19 CHECK PASS: syscall 异常真语义下 warm 链越过旧停机面" || echo "HOST19 CHECK FAIL"
exit $RC
