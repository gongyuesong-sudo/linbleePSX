#!/usr/bin/env bash
# validate_host86.sh — x86 楔3 二段图谱(task-095, 2026-10-31)
# A) 二段规模/交权行复现  B) 14 ret/16 call/1026 指令计数复算
# C) thunk 解析模式锚(0x4022a4+VirtualAlloc 0x80000400)  D) MZ×2 验伪复算
# E) docs/78 锚; F) README docs=79; G) EXE 只读恒
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST86: $*"; }
E=/home/gill/bleem/demo-v1_4.EXE
dd if=$E of=/tmp/h86_s2.bin bs=1 skip=348984 2>/dev/null
objdump -D -b binary -m i386 --adjust-vma=0x55338 /tmp/h86_s2.bin 2>/dev/null > /tmp/h86_s2.asm
python3 - <<'PY' || fail=1
import re
ins=[]
for L in open('/tmp/h86_s2.asm'):
    m=re.match(r'\s+([0-9a-f]+):\t([0-9a-f]{2}(?: [0-9a-f]{2})*\s*)\t(\S+)\s*(.*)',L)
    if m: ins.append((int(m.group(1),16),m.group(3),m.group(4).strip()))
n=len(ins); r=sum(1 for _,op,_ in ins if op.startswith('ret'))
c=len({int(re.search(r'0x([0-9a-f]+)',a).group(1),16) for _,op,a in ins if op=='call' and (a or '').startswith('0x55')})
ok = n==1026 and r==14 and c==16
print(f"HOST86: B) 指令={n} ret={r} 本地call={c} {'OK' if ok else '!= 1026/14/16'}")
exit(0 if ok else 1)
PY
a=$(objdump -D -b binary -m i386 --adjust-vma=0x55338 --start-address=0x55396 --stop-address=0x553a2 /tmp/h86_s2.bin 2>/dev/null)
echo "$a" | grep -q "add    \$0xfa00,%esi" && echo "$a" | grep -q "call   \*%esi" && note "A) 交权行锚(add 0xfa00+call *esi)" || { note "A) FAIL"; fail=1; }
t=$(objdump -D -b binary -m i386 --adjust-vma=0x55338 --start-address=0x55716 --stop-address=0x55737 /tmp/h86_s2.bin 2>/dev/null)
echo "$t" | grep -q "0x4022a4" && echo "$t" | grep -q "0x80000400" && note "C) thunk 解析+VirtualAlloc 锚" || { note "C) FAIL"; fail=1; }
python3 - <<'PY' || fail=1
d=open('/home/gill/bleem/demo-v1_4.EXE','rb').read()
bad=0
for off in (0x1b64f,0x29b3d):
    a=0xfa00+off; pe=int.from_bytes(d[a+0x3c:a+0x40],'little')
    if d[a:a+2]!=b'MZ' or d[a+pe:a+pe+4]==b'PE\x00\x00': bad=1
print("HOST86: D) MZ×2 验伪复算", "OK" if not bad else "FAIL")
exit(bad)
PY
grep -q "交权行" docs/78_second_stage_map.md && grep -q "0xfa00" docs/78_second_stage_map.md && grep -q "验伪" docs/78_second_stage_map.md && note "E) docs/78 锚齐" || { note "E) 缺锚"; fail=1; }
dd2=$(ls docs/*.md | wc -l); if grep -q "78_second_stage_map" README.md && [ "$dd2" -eq 84 ]; then note "F) README 与 docs=$dd2 一致"; else note "F) FAIL docs=$dd2"; fail=1; fi
s=$(sha256sum $E | cut -c1-8); [ "$s" = "b61ee13e" ] && note "G) EXE 只读恒" || { note "G) FAIL"; fail=1; }
[ $fail -eq 0 ] && echo "HOST86 CHECK PASS" || echo "HOST86 CHECK FAIL"
[ $fail -eq 0 ]
