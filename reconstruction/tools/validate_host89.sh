#!/usr/bin/env bash
# validate_host89.sh — x86 楔6 服务表 ABI(task-098, 2026-10-31)
# A) 0x55459 分派器指令序列复现  B) IAT 全表 119 槽落盘+分布
# C) s3 table[0] 11 点聚簇(头 0x300 内)  D) 位引擎状态字段串(2A0010/20/21/0D/29/2A)
# E) docs/81 锚; F) README docs=82; G) EXE 只读恒
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST89: $*"; }
E=/home/gill/bleem/demo-v1_4.EXE
dd if=$E of=/tmp/h89_s2.bin bs=1 skip=348984 2>/dev/null
a=$(objdump -D -b binary -m i386 --adjust-vma=0x55338 --start-address=0x55459 --stop-address=0x5546d /tmp/h89_s2.bin 2>/dev/null)
echo "$a" | grep -q "mov    \$0x4022a4,%eax" && echo "$a" | grep -q "call   \*(%eax)" && echo "$a" | grep -q "ret" && note "A) 服务分派器 0x55459 序列锚" || { note "A) FAIL"; fail=1; }
b=$(python3 -c "
import json
m=json.load(open('tools/x86_iat_map.json'))
print(len(m), any('WaitForSingleObject'==v for v in m.values()), sum(1 for v in m.values() if 'ord)' in v))" 2>/dev/null)
[ "$b" = "119 True 5" ] && note "B) IAT 全表 119 槽落盘(含 WaitForSingleObject/5 ordinal)" || { note "B) FAIL $b"; fail=1; }
c=$(python3 -c "
import struct
s3=open('decrypted/stream_10a4c_L71cbf.bin','rb').read()
pats=[]; off=0; mx=0
while True:
    i=s3.find(b'\xff\x15\xa4\x22\x40\x00',off)
    if i<0: break
    pats.append(i); off=i+2
print(len(pats), max(pats) if pats else -1)")
set -- $c
[ "$1" = "11" ] && [ "$2" -lt 768 ] && note "C) s3 table[0] 11 点全聚簇头壳(<0x300)" || { note "C) FAIL $c"; fail=1; }
d=$(python3 -c "
s3=open('decrypted/stream_10a4c_L71cbf.bin','rb').read()
import struct
need={0x2a0000,0x2a0010,0x2a0020,0x2a0021,0x2a000d,0x2a0029,0x2a002a}
hit=set()
for i in range(0x200,0x300):
    v=struct.unpack_from('<I',s3,i)[0]
    if v in need: hit.add(v)
print(len(hit))")
[ "$d" -ge 6 ] && note "D) 位引擎状态字段串 s3 头命中 $d/7" || { note "D) FAIL n=$d"; fail=1; }
grep -q "服务分派器\|索引调用 ABI" docs/81_service_table_abi.md && grep -q "guest 侧投影" docs/81_service_table_abi.md && grep -q "未闭环" docs/81_service_table_abi.md && note "E) docs/81 锚齐(含诚实未闭环)" || { note "E) 缺锚"; fail=1; }
dd=$(ls docs/*.md | wc -l); if grep -q "81_service_table_abi" README.md && [ "$dd" -eq 82 ]; then note "F) README 与 docs=$dd 一致"; else note "F) FAIL docs=$dd"; fail=1; fi
s=$(sha256sum $E | cut -c1-8); [ "$s" = "b61ee13e" ] && note "G) EXE 只读恒" || { note "G) FAIL"; fail=1; }
[ $fail -eq 0 ] && echo "HOST89 CHECK PASS" || echo "HOST89 CHECK FAIL"
[ $fail -eq 0 ]
