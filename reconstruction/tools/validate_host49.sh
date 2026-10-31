#!/usr/bin/env bash
# validate_host49.sh — H-24 终局评估（task-059a3，2026-10-31）
# A) 契约全集复算: C2 切片根call桩普查 = 44 cmd / 50 桩 / 3 族
# B) docs/45 锚: §2 四层表 + "H-24 关门" + "重写级" + "DuckStation"
# C) README 索引含 docs/44/45 且篇数=46(45 编号+台账)
# D) docs/44 §4 四路终证锚在
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST49: $*"; }
python3 - <<'PYEOF' || fail=1
import struct
sl=open('/home/gill/bleem/.analysis/golden/golden_c2_slice_30000_90000.bin','rb').read()
def W(a): return struct.unpack_from('<I',sl,a-0x30000)[0]
import collections
fam=collections.Counter(); cmds=collections.defaultdict(set)
for a in range(0x30000,0x90000-8,4):
    if W(a)>>16==0x240A and W(a+4)==0x01400008 and W(a+8)>>16==0x2409:
        V=W(a)&0xFFFF; fam[V]+=1; cmds[V].add(W(a+8)&0xFFFF)
tot=set().union(*cmds.values())
assert len(tot)==44 and sum(fam.values())==50 and len(fam)==3, (len(tot),sum(fam.values()),len(fam))
print(f"HOST49: A) 契约全集复算一致 cmd={len(tot)} 桩={sum(fam.values())} 族={len(fam)}")
PYEOF
grep -q "H-24 关门" docs/45_h24_final.md && grep -q "44 个 cmd" docs/45_h24_final.md && grep -q "DuckStation" docs/45_h24_final.md && grep -q "重写级" docs/45_h24_final.md \
  && note "B) docs/45 锚齐" || { note "B) docs/45 缺锚"; fail=1; }
n=$(ls docs/*.md | wc -l); grep -q "45_h24_final" README.md && grep -q "44_tfr_bounce" README.md && [ "$n" -eq 46 ] \
  && note "C) README 索引与 docs 篇数=$n 一致" || { note "C) FAIL docs=$n README锚缺"; fail=1; }
grep -q "四路独立注入" docs/44_tfr_bounce.md && note "D) docs/44 §4 在" || { note "D) docs/44 §4 缺"; fail=1; }
[ $fail -eq 0 ] && echo "HOST49 CHECK PASS" || echo "HOST49 CHECK FAIL"
[ $fail -eq 0 ]
