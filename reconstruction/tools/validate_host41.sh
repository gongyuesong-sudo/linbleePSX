#!/usr/bin/env bash
# validate_host41.sh — H-21c attempt1 验证（task-049，2026-10-31）
# A) H_DEVRST2 CHCR 完成语义在源码; B) C1→C2 直 jal 普查=741 可复算;
# C) 冷跳链 DEVRST2 全链运行到界(墙界 final=00000000)且 BIOS-JIT-CHAIN 存在;
# D) docs/36 存在且含 C2main 缺席结论。
set -u
cd "$(dirname "$0")/.."
fail=0
note(){ echo "HOST41: $*"; }
[ -f docs/36_c2main_absent.md ] && grep -q "741" docs/36_c2main_absent.md && grep -q "撤回" docs/36_c2main_absent.md && grep -q "UNIQ" docs/36_c2main_absent.md && note "D) docs/36 OK(含 SESSION-53 修正)" || { note "D) docs FAIL"; fail=1; }
grep -q "H_JITUNIQ" host/mips_jit.c && grep -q "UNIQ-TAIL" host/mips_jit.c && note "E) UNIQUE 清单机制 OK" || { note "E) UNIQUE FAIL"; fail=1; }
grep -q "H_C2END" host/mips_jit.c && grep -q "线程调度面" docs/36_c2main_absent.md && note "F) C2END 界门 + §6 线程面终判 OK" || { note "F) FAIL"; fail=1; }
grep -q "H_DEVRST2" host/mips_jit.c && grep -q "0x02000000u" host/mips_jit.c && note "A) DEVRST2 OK" || { note "A) DEVRST2 FAIL"; fail=1; }
ROM=/home/gill/bleem/psxbios/SCPH1001.BIN
python3 - "$ROM" <<'PY' || fail=1
import struct,sys
rom=open(sys.argv[1],'rb').read(); tg=set()
for i in range(0,len(rom)-4,4):
    w=struct.unpack_from('<I',rom,i)[0]
    if w>>26==3:
        t=((i*4+4)&0xf0000000)|((w&0xffffff)<<2); p=t&0x1FFFFF
        if 0x30000<=p<0x97FF0: tg.add(p)
print(f"HOST41: B) C1→C2 直 jal 目标数={len(tg)}")
sys.exit(0 if len(tg)==741 else 1)
PY
if [ "$fail" -eq 0 ]; then
  H_RACATCH=1 H_C2HOME=1 H_C2GOLD=1 H_DEVRST=1 H_DEVRST2=1 H_LOOPREP=3000000 H_MAXB=8000000 HTRACE_LOG=/tmp/h41.log H_ARENA_MB=320 H_SLED_CAP=200 H_BIOSJIT=1 H_BIOSEXN=1 H_JIT_BUDGET=10000000 BLEEM_BIOS=$ROM timeout 600 ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin 0x400000 0x472000 >/dev/null 2>&1
  grep -qa "BIOS-JIT-CHAIN blocks=" /tmp/h41.log && grep -qa "final=00000000" /tmp/h41.log && note "C) 全链到墙界 OK（未达 Execute!=既定 attempt1 结果）" || { note "C) 链 FAIL"; fail=1; }
fi
if [ "$fail" -eq 0 ]; then
  grep -q "H_IRQ" host/mips_jit.c && [ -f docs/38_h22_final.md ] && grep -q "census 与基线逐位差分 = 0" docs/38_h22_final.md \
    && note "G) H_IRQ 机制 + docs/38 终判 OK" || { note "G) FAIL"; fail=1; }
fi
ROM2=/home/gill/bleem/psxbios/SCPH1001.BIN
if [ "$fail" -eq 0 ]; then
  ENVB="H_C2HOME=1 H_C2GOLD=1 H_C2END=0x89E00 H_DEVRST=1 H_DEVRST2=1 H_JITUNIQ=1 H_LOOPREP=3000000 H_MAXB=8000000 H_ARENA_MB=320 H_SLED_CAP=200 H_BIOSJIT=1 H_BIOSEXN=1 H_JIT_BUDGET=10000000 BLEEM_BIOS=$ROM2"
  env $ENVB HTRACE_LOG=/tmp/h41_b.log timeout 600 ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin 0x400000 0x472000 >/dev/null 2>&1
  env $ENVB H_IRQ=3000 HTRACE_LOG=/tmp/h41_i.log timeout 600 ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin 0x400000 0x472000 >/dev/null 2>&1
  grep -qa "IRQINJ#8" /tmp/h41_i.log && [ "$(diff <(grep -a '^UNIQ' /tmp/h41_b.log) <(grep -a '^UNIQ' /tmp/h41_i.log) | wc -l)" -eq 0 ] \
    && note "H) IRQ 注入 8 次 + census 零差分可复算" || { note "H) FAIL"; fail=1; }
fi
[ "$fail" -eq 0 ] && echo "HOST41 CHECK PASS" || echo "HOST41 CHECK FAIL"
[ "$fail" -eq 0 ]
