#!/usr/bin/env bash
# validate_host42.sh — H-22 attempt1 验证（task-050，2026-10-31）
# A) 机制在源码(JALZ/JZSLED/jit_flush_range/EMITFAIL 日志修复); B) 金标 0x1014=0x42000010 可复算;
# C) 冷跳链运行到界且 EMITFAIL 现场可打印(带 ra 字段); D) docs/37 关键结论在。
set -u
cd "$(dirname "$0")/.."
fail=0
note(){ echo "HOST42: $*"; }
grep -q "H_JALZ" host/mips_jit.c && grep -q "H_JZSLED" host/mips_jit.c && grep -q "jit_flush_range" host/mips_jit.c && grep -q "H_JALRZ" host/mips_jit.c \
  && grep -q 'EMITFAIL pc=%08x op=%08x ra=' host/mips_jit.c && note "A) 机制 OK" || { note "A) 机制 FAIL"; fail=1; }
python3 - <<'PY' || { note "B) 金标词 FAIL"; fail=1; }
import struct
g=open('/home/gill/bleem/.analysis/golden/GOLDEN_RAM_2MB.bin','rb').read()
w=struct.unpack_from('<I',g,0x1014)[0]
print(f"HOST42: B) gold[0x1014]={w:08x}")
assert w==0x42000010
PY
[ -f docs/37_ra4_seed.md ] && grep -q "jalr" docs/37_ra4_seed.md && grep -q "00001014" docs/37_ra4_seed.md && note "D) docs/37 OK" || { note "D) docs FAIL"; fail=1; }
ROM=/home/gill/bleem/psxbios/SCPH1001.BIN
H_JZSLED=1 H_C2HOME=1 H_C2GOLD=1 H_C2END=0x89E00 H_DEVRST=1 H_DEVRST2=1 H_JITUNIQ=1 H_LOOPREP=3000000 H_MAXB=8000000 HTRACE_LOG=/tmp/h42.log H_ARENA_MB=320 H_SLED_CAP=200 H_BIOSJIT=1 H_BIOSEXN=1 H_JIT_BUDGET=10000000 BLEEM_BIOS=$ROM timeout 600 ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin 0x400000 0x472000 >/dev/null 2>&1
grep -qa "EMITFAIL pc=00001014 op=42000010" /tmp/h42.log && grep -qa "BIOS-JIT-CHAIN blocks=" /tmp/h42.log && note "C) halt 现场可复现(0x1014) OK" || { note "C) halt FAIL"; fail=1; }
grep -qa "EXN cmd=1 epc=" /tmp/h42.log && grep -qa "sp=" /tmp/h42.log && note "E) EXN 诊断字段 OK" || { note "E) EXN 字段 FAIL"; fail=1; }
[ "$fail" -eq 0 ] && echo "HOST42 CHECK PASS" || echo "HOST42 CHECK FAIL"
[ "$fail" -eq 0 ]
