#!/usr/bin/env bash
# validate_host52.sh — M-6 金标态 handler 直跑探针（task-062，2026-10-31）
# A) 构建+复跑: 50 桩全执行, break+halt=50, break>=27(等值)
# B) HP 元组 (vec,cmd,h,收尾) 与 docs/48 表逐行一致
# C) docs/48 锚: 哨兵 ABI + "27 个 handler 真执行" + 纯转发生成器终判
# D) README 含 48_probe_semantics 且 docs 计数=49
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST52: $*"; }
python3 tools/gen_handler_list.py 2>/dev/null || python3 - <<'PYEOF'
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
with open('/tmp/h52_handlers.txt','w') as f:
    for V in sorted(fam):
        for C in sorted(fam[V]):
            h=W(bases[V]+C*4)
            if h: f.write(f"{V:x} {C:x} {h&0xFFFFF:x}\n")
print("HOST52: 清单重建 ok")
PYEOF
gcc -m32 -O0 -g -no-pie -I host -o host/build/hprobe tools/hprobe.c || { note "A) 构建失败"; exit 1; }
PROBEF=/tmp/h52_handlers.txt timeout 240 ./host/build/hprobe /home/gill/bleem/psxbios/SCPH1001.BIN 60000 > /tmp/h52hp.log 2>&1
s=$(grep "^HPSUM" /tmp/h52hp.log)
[ -n "$s" ] || { note "A) 探针未跑"; exit 1; }
tb=$(echo "$s" | grep -o 'total=[0-9]*' | cut -d= -f2); bk=$(echo "$s" | grep -o 'break=[0-9]*' | cut -d= -f2); hl=$(echo "$s" | grep -o 'halt=[0-9]*' | cut -d= -f2)
[ "$tb" -eq 50 ] && [ "$bk" -eq 27 ] && [ $((bk+hl)) -eq 50 ] && note "A) 复跑一致 total=50 break=27 halt=23" || { note "A) FAIL $s"; fail=1; }
python3 - <<'PYEOF' || fail=1
import re
run={}
for l in open('/tmp/h52hp.log'):
    if l.startswith('HP '):
        m=dict(re.findall(r'(\w+)=(\S+)',l))
        run[(int(m['vec'],16),int(m['cmd'],16))]=('0x'+m['h'],'BREAK' if 'BREAK' in l else 'HALT')
doc=open('/home/gill/bleem/rewrite/docs/48_probe_semantics.md').read()
n=0
for l in doc.splitlines():
    mm=re.match(r'\| (0x[0-9a-f]{1,2}) \| (0x[0-9a-f]{1,2}) \| (0x[0-9a-f]{8}) \| \d+ \| \d+ \| (BREAK|HALT) \|',l)
    if mm:
        k=(int(mm.group(1),16),int(mm.group(2),16))
        assert run.get(k)==(mm.group(3),mm.group(4)), (k,run.get(k),mm.groups())
        n+=1
assert n==50, n
print(f"HOST52: B) docs/48 与复跑逐行一致 {n}/50")
PYEOF
grep -q "BFC4C1B0 哨兵" docs/48_probe_semantics.md && grep -q "27 个 handler 真执行" docs/48_probe_semantics.md && grep -q "纯转发生成器" docs/48_probe_semantics.md \
  && note "C) docs/48 锚齐" || { note "C) 缺锚"; fail=1; }
d=$(ls docs/*.md | wc -l); grep -q "48_probe_semantics" README.md && [ "$d" -eq 56 ] \
  && note "D) README 索引与 docs=$d 一致" || { note "D) FAIL docs=$d"; fail=1; }
[ $fail -eq 0 ] && echo "HOST52 CHECK PASS" || echo "HOST52 CHECK FAIL"
[ $fail -eq 0 ]
