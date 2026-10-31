#!/usr/bin/env bash
# validate_host53.sh — M-6b 可转发子集终表（task-063，2026-10-31）
# A) 复跑 hprobe(budget=200000): BREAK 集与 docs/49 表 A 态逐行一致, 并集=26
# B) docs/49 锚: 双态零分歧终判 + 三批分解
# C) A/B 两列与 B 态复算一致(重跑 B 抽 BREAK 数=26)
# D) README 含 49_forward_subset 且 docs 计数=50
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST53: $*"; }
gcc -m32 -O0 -g -no-pie -I host -o host/build/hprobe tools/hprobe.c || { note "构建失败"; exit 1; }
[ -s /tmp/h53_handlers.txt ] || python3 - <<'PYEOF'
import struct
sl=open('/home/gill/bleem/.analysis/golden/golden_c2_slice_30000_90000.bin','rb').read()
g=open('/home/gill/bleem/.analysis/golden/GOLDEN_RAM_2MB.bin','rb').read()
def WS(a): return struct.unpack_from('<I',sl,a-0x30000)[0]
def W(a): return struct.unpack_from('<I',g,a)[0]
fam={}
for a in range(0x30000,0x90000-8,4):
    if WS(a)>>16==0x240A and WS(a+4)==0x01400008 and WS(a+8)>>16==0x2409:
        fam.setdefault(WS(a)&0xFFFF,set()).add(WS(a+8)&0xFFFF)
bases={0xA0:0x200,0xB0:0x874,0xC0:0x674}
with open('/tmp/h53_handlers.txt','w') as f:
    for V in sorted(fam):
        for C in sorted(fam[V]):
            h=W(bases[V]+C*4)
            if h: f.write(f"{V:x} {C:x} {h&0xFFFFF:x}\n")
PYEOF
PROBEF=/tmp/h53_handlers.txt timeout 300 ./host/build/hprobe /home/gill/bleem/psxbios/SCPH1001.BIN 200000 > /tmp/h53A.log 2>&1
PROBEEX=1 H_BIOSEXN=1 H_C2HOME=1 PROBEF=/tmp/h53_handlers.txt timeout 300 ./host/build/hprobe /home/gill/bleem/psxbios/SCPH1001.BIN 200000 > /tmp/h53B.log 2>&1
python3 - <<'PYEOF' || fail=1
import re
def stat(p):
    d={}
    for l in open(p):
        if l.startswith('HP '):
            m=dict(re.findall(r'(\w+)=(\S+)',l))
            d[(int(m['vec'],16),int(m['cmd'],16))]=('BREAK' if 'BREAK' in l else 'HALT')
    return d
A,B=stat('/tmp/h53A.log'),stat('/tmp/h53B.log')
doc=open('/home/gill/bleem/rewrite/docs/49_forward_subset.md').read()
n=0; uni=0
for l in doc.splitlines():
    mm=re.match(r'\| 0x([0-9a-f]{2}) \| 0x([0-9a-f]{2}) \| (\S+) \| (\w+) \| (\w+) \|',l)
    if not mm: continue
    k=(int(mm.group(1),16),int(mm.group(2),16))
    assert A[k]==mm.group(4) and B[k]==mm.group(5), (k,A[k],B[k],mm.groups())
    n+=1; uni+= (mm.group(4)=='BREAK' or mm.group(5)=='BREAK')
assert n==50 and uni==26,(n,uni)
print(f"HOST53: A/C) 双态复跑与 docs/49 逐行一致 n={n} 并集={uni}")
PYEOF
if grep -q "零分歧" docs/49_forward_subset.md && grep -q "批1(纯转发" docs/49_forward_subset.md && grep -q "BREAK 并集 = 26/50" docs/49_forward_subset.md; then note "B) docs/49 锚齐"; else note "B) 缺锚"; fail=1; fi
d=$(ls docs/*.md | wc -l); grep -q "49_forward_subset" README.md && [ "$d" -eq 55 ] \
  && note "D) README 索引与 docs=$d 一致" || { note "D) FAIL docs=$d"; fail=1; }
[ $fail -eq 0 ] && echo "HOST53 CHECK PASS" || echo "HOST53 CHECK FAIL"
[ $fail -eq 0 ]
