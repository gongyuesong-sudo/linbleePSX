#!/usr/bin/env bash
# validate_host43.sh — H-23a 验证（task-051，2026-10-31）
# A) docs/39 关键结论在; B) 金标 ra→调用点溯源可复算(3条); C) C2SEC(0x8003xxxx) 址物化点普查;
# D) harness JSON 含 task-051/052/053 H-23 任务链。
set -u
cd "$(dirname "$0")/.."
fail=0
note(){ echo "HOST43: $*"; }
if [ -f docs/39_c2_contract_map.md ] && grep -q "jalr" docs/39_c2_contract_map.md && grep -q "blanket-ack" docs/39_c2_contract_map.md \
  && grep -q "jalr ra,t6" docs/39_c2_contract_map.md; then note "A) docs/39 OK"; else note "A) docs FAIL"; fail=1; fi
python3 <<'PYEOF' || { note "B/C) 复算 FAIL"; fail=1; }
import struct
rom=open('/home/gill/bleem/psxbios/SCPH1001.BIN','rb').read()
def W(o): return struct.unpack_from('<I',rom,o)[0]
w=W(0x6990); assert w>>26==3 and (0xBFC00000|((w&0x3FFFFFF)<<2))==0xBFC06FF0, 'B1'
assert (W(0x7020)>>26)==0x0F and (W(0x7020)&0xFFFF)==0x8003 and (W(0x7024)&0x3F)==9 and ((W(0x7024)>>11)&0x1F)==31, 'B2'
w=W(0x67D0); assert w>>26==3 and (0xBFC00000|((w&0x3FFFFFF)<<2))==0xBFC067E8, 'B3'
print("HOST43: B) 金标 ra 三条溯源复算 OK")
hits=[o for o in range(0,len(rom)-4,4) if (W(o)>>26)==0x0F and (W(o)&0xFFFF)==0x8003]
assert [hex(h) for h in hits]==['0x7010','0x7020','0x26254','0x263b4'], f'C hits={[hex(h) for h in hits]}'
w1=W(0x26254+4); assert (w1>>26)==9 and (w1&0xFFFF)==0x48EC and ((w1>>21)&0x1F)==25, 'C2 t9'
w2=W(0x263b4+4); assert (w2>>26)==9 and (w2&0xFFFF)==0x48EC and ((w2>>16)&0x1F)==7 and ((w2>>21)&0x1F)==7, 'C2 t7'
print(f"HOST43: C) lui 0x8003 全ROM恰 4 处(0x7010/0x7020 包装器, 0x26254/0x263b4 t9/t7 间接派发族)")
PYEOF
python3 <<'PYEOF' || { note "D) JSON FAIL"; fail=1; }
import json
d=json.load(open('harness-tasks.json'))
ids={t['id'] for t in d['tasks']}
assert {'task-051','task-052','task-053'} <= ids and len(d['tasks'])==53
t52=[t for t in d['tasks'] if t['id']=='task-052'][0]; t53=[t for t in d['tasks'] if t['id']=='task-053'][0]
assert t52['depends_on']==['task-051'] and t53['depends_on']==['task-052']
print("HOST43: D) H-23 任务链在 JSON(53 任务, 依赖 051→052→053)")
PYEOF
if [ "$fail" -eq 0 ]; then
  ROM=/home/gill/bleem/psxbios/SCPH1001.BIN
  [ -f docs/40_rootcmd_census.md ] && grep -q "内存分配包装器" docs/40_rootcmd_census.md && grep -q "16 个 cmd" docs/40_rootcmd_census.md \
    && note "E) docs/40 更正与 census 在" || { note "E) docs/40 FAIL"; fail=1; }
  if [ "$fail" -eq 0 ]; then
    env H_C2HOME=1 H_C2GOLD=1 H_C2END=0x89E00 H_DEVRST=1 H_DEVRST2=1 H_LOOPREP=3000000 H_MAXB=8000000 H_ARENA_MB=320 H_SLED_CAP=200 H_BIOSJIT=1 H_BIOSEXN=1 H_JIT_BUDGET=10000000 BLEEM_BIOS=$ROM HTRACE_LOG=/tmp/h43v.log timeout 600 ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin 0x400000 0x472000 >/dev/null 2>&1
    N=$(grep -ac "visit \[674+110\]" /tmp/h43v.log || true); M=$(grep -ac "rootvec" /tmp/h43v.log || true)
    [ "${N:-0}" -ge 1000 ] && [ "${M:-0}" -ge 3 ] && note "F) census 复算 visit110=$N rootvec=$M" || { note "F) census FAIL N=$N M=$M"; fail=1; }
  fi
fi
[ "$fail" -eq 0 ] && echo "HOST43 CHECK PASS" || echo "HOST43 CHECK FAIL"
[ "$fail" -eq 0 ]
