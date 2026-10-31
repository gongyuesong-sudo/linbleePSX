#!/usr/bin/env bash
# validate_host50.sh — M-4 BIOS 侧 rootcall 44-cmd 实现映射（task-060，2026-10-31）
# A) 三族派发块形态复算(golden RAM): 0x5C4/0x5E0/0x600 均为 lui/addiu/sll/add/lw/jr 形, 表基 0x200/0x874/0x674
# B) 50/50 桩→handler 非零完备性(独立复算, C1核≥48, 空槽=0)
# C) docs/46 映射表 50 行 + 关键锚
# D) README 索引含 46_cmd_handlers 且 docs 计数=47
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST50: $*"; }
python3 - <<'PYEOF' || fail=1
import struct,sys
g=open('/home/gill/bleem/.analysis/golden/GOLDEN_RAM_2MB.bin','rb').read()
def W(a): return struct.unpack_from('<I',g,a)[0]
for blk,base,has_lui in ((0x5C4,0x200,False),(0x5E0,0x874,True),(0x600,0x674,True)):
    k=0
    if has_lui:
        w=W(blk); assert (w>>26)==0x0F and (w&0xFFFF)==0,(hex(blk),'lui'); k=4
    w=W(blk+k)
    assert (w>>26)==0x09 and ((w>>21)&31)==(8 if has_lui else 0) and ((w>>16)&31)==8 and (w&0xFFFF)==base,(hex(blk),hex(w))
    assert W(blk+k+4)==0x00094880 and W(blk+k+8)==0x01094020 and (W(blk+k+0xC)>>26)==0x23 and W(blk+k+0x14)==0x01000008,(hex(blk),'form')
sl=open('/home/gill/bleem/.analysis/golden/golden_c2_slice_30000_90000.bin','rb').read()
def WS(a): return struct.unpack_from('<I',sl,a-0x30000)[0]
fam={}
for a in range(0x30000,0x90000-8,4):
    if WS(a)>>16==0x240A and WS(a+4)==0x01400008 and WS(a+8)>>16==0x2409:
        fam.setdefault(WS(a)&0xFFFF,set()).add(WS(a+8)&0xFFFF)
bases={0xA0:0x200,0xB0:0x874,0xC0:0x674}
tot=0; zero=0; c1=0
for V in fam:
    for C in fam[V]:
        h=W(bases[V]+C*4); tot+=1
        if h==0: zero+=1
        a=h&0xFFFFF
        if (h>>20)==0xBFC or ((h>>28) in (0x8,0x0) and a<0x30000): c1+=1
assert tot==50 and zero==0 and c1>=48,(tot,zero,c1)
print(f"HOST50: A) 三族派发块形态复算一致; B) handler 完备性 {tot}/50 非零, C1核={c1}")
PYEOF
n=$(grep -cE '^\| 0x[0-9a-f]{2} \| 0x[0-9a-f]{2} \|' docs/46_cmd_handlers.md)
[ "$n" -eq 50 ] && grep -q "50/50 全非零" docs/46_cmd_handlers.md && grep -q "完备性实证" docs/46_cmd_handlers.md \
  && note "C) docs/46 表 $n 行+锚齐" || { note "C) FAIL 表行=$n"; fail=1; }
d=$(ls docs/*.md | wc -l); grep -q "46_cmd_handlers" README.md && [ "$d" -eq 54 ] \
  && note "D) README 索引与 docs=$d 一致" || { note "D) FAIL docs=$d README 锚"; fail=1; }
[ $fail -eq 0 ] && echo "HOST50 CHECK PASS" || echo "HOST50 CHECK FAIL"
[ $fail -eq 0 ]
