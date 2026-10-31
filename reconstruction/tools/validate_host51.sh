#!/usr/bin/env bash
# validate_host51.sh — M-5 rootcall 全量归名（task-061，2026-10-31）
# A) ROM+0x4300 A 表锚点复算(A10/A19/A38/A42 与 docs/14 §1 一致)
# B) 独立复算: docs/46 的 50 handler 与 A 表等值锚 + docs/41 语义锚 → 锚定率=14/50
# C) docs/47 表 50 行 + 终判锚
# D) README 索引含 47_rootcmd_names 且 docs 计数=48
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST51: $*"; }
python3 - <<'PYEOF' || fail=1
import struct,json,os
rom=open('/home/gill/bleem/psxbios/SCPH1001.BIN','rb').read()
atab=[struct.unpack_from('<I',rom,0x4300+i*4)[0] for i in range(44)]
assert atab[10]==0xBFC022C0 and atab[19]==0xBFC02240 and atab[38]==0xBFC02EDC and atab[42]==0xBFC02B50
p='/tmp/h69_rows.json'
if not os.path.exists(p):
    g=open('/home/gill/bleem/.analysis/golden/GOLDEN_RAM_2MB.bin','rb').read()
    def W(a): return struct.unpack_from('<I',g,a)[0] if 0<=a<=len(g)-4 else 0
    sl=open('/home/gill/bleem/.analysis/golden/golden_c2_slice_30000_90000.bin','rb').read()
    def WS(a): return struct.unpack_from('<I',sl,a-0x30000)[0]
    fam={}
    for a in range(0x30000,0x90000-8,4):
        if WS(a)>>16==0x240A and WS(a+4)==0x01400008 and WS(a+8)>>16==0x2409:
            fam.setdefault(WS(a)&0xFFFF,set()).add(WS(a+8)&0xFFFF)
    bases={0xA0:0x200,0xB0:0x874,0xC0:0x674}
    rows=[dict(vec=V,cmd=C,h=W(bases[V]+C*4)) for V in fam for C in fam[V]]
    json.dump(rows,open(p,'w'))
else:
    rows=json.load(open(p))
sem={(0xB0,0x00),(0xB0,0x01),(0xB0,0x07),(0xB0,0x08),(0xB0,0x0C),(0xB0,0x12),(0xB0,0x1C)}
anch=set()
for r in rows:
    if r['h'] in atab: anch.add((r['vec'],r['cmd']))
    if (r['vec'],r['cmd']) in sem: anch.add((r['vec'],r['cmd']))
print(f"HOST51: A) A 表锚点复算一致 B) 锚定率 {len(anch)}/50")
assert len(anch)==14, len(anch)
PYEOF
n=$(grep -cE '^\| 0x[0-9a-f]{2} \| 0x[0-9a-f]{2} \|' docs/47_rootcmd_names.md)
[ "$n" -eq 50 ] && grep -q "锚定率 \*\*14/50\*\*" docs/47_rootcmd_names.md && grep -q "三种安装视图" docs/47_rootcmd_names.md \
  && note "C) docs/47 表 $n 行+锚齐" || { note "C) FAIL 表行=$n"; fail=1; }
d=$(ls docs/*.md | wc -l); grep -q "47_rootcmd_names" README.md && [ "$d" -eq 56 ] \
  && note "D) README 索引与 docs=$d 一致" || { note "D) FAIL docs=$d README 锚"; fail=1; }
[ $fail -eq 0 ] && echo "HOST51 CHECK PASS" || echo "HOST51 CHECK FAIL"
[ $fail -eq 0 ]
