#!/usr/bin/env bash
# validate_host90.sh — x86 楔7 双判据否证+映射构造器(task-099, 2026-10-31)
# A) 唯一交叉环复算(0x10b4e-0x10d15, refs=7)  B) 映射构造器 5 magic imm 锚
# C) table[0] VirtualAlloc 循环(0x1000a×4 槽)  D) 回边 3374/宽引用 133 计数复算
# E) docs/82 锚(含诚实否证) F) README docs=83 G) EXE 只读恒
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST90: $*"; }
E=/home/gill/bleem/demo-v1_4.EXE
python3 - <<'PY' || fail=1
import struct,re
s3=open('decrypted/stream_10a4c_L71cbf.bin','rb').read()
open('/tmp/h90.bin','wb').write(s3[8:])
import subprocess
r=subprocess.run(['objdump','-D','-b','binary','-m','i386','--adjust-vma=0x10a4e','/tmp/h90.bin'],capture_output=True,text=True).stdout
ins=[]
for L in r.splitlines():
    m=re.match(r'\s+([0-9a-f]+):\t([0-9a-f]{2}(?: [0-9a-f]{2})*\s*)\t(\S+)\s*(.*)',L)
    if m: ins.append((int(m.group(1),16),m.group(3),m.group(4).strip()))
base=0x10a4e
backs=[(a,int(re.search(r'0x([0-9a-f]+)$',arg).group(1),16)) for a,op,arg in ins
       if op.startswith('j') and re.match(r'0x[0-9a-f]+$',arg or '')
       and int(re.search(r'0x([0-9a-f]+)$',arg).group(1),16)<a and int(re.search(r'0x([0-9a-f]+)$',arg).group(1),16)>=base]
scr=[(a,op,arg) for a,op,arg in ins if re.search(r'0x2[ab][0-9a-f]{4}',arg or '')]
real=[x for x in scr if 0x10b4e<=x[0]<=0x10d15]
loops={}
for a,op,arg in scr:
    for s,t in backs:
        if t<=a<=s and s-t<0x3000: loops.setdefault((t,s),0); loops[(t,s)]+=1
top=max(loops.items(),key=lambda kv:kv[1])
okA = top==((0x10b4e,0x10d15),7) and len(real)==7
okD = len(backs)==3374 and len(scr)==133
print("HOST90: A) 唯一交叉环 %s refs=%d 真scratch=%d"%('OK' if okA else 'FAIL',top[1],len(real)))
print("HOST90: D) 回边=%d 宽引用=%d %s"%(len(backs),len(scr),'OK' if okD else 'FAIL'))
exit(0 if okA and okD else 1)
PY
b=$(python3 -c "
import struct
s3=open('decrypted/stream_10a4c_L71cbf.bin','rb').read()[8:]
body=s3[0x10b4e-0x10a4e:0x10d16-0x10a4e]
mags=[0xbfc00000,0x1f800400,0x1f800000,0x328400,0x9400]
print(sum(1 for m in mags if struct.pack('<I',m) in body))"
)
[ "$b" = "5" ] && note "B) 映射构造器 5 magic imm 全中(RAM/BIOS 窗/MMIO 常数/0x9400 池)" || { note "B) FAIL n=$b"; fail=1; }
c=$(objdump -D -b binary -m i386 --adjust-vma=0x10a4e --start-address=0x10b4e --stop-address=0x10bb2 /tmp/h90.bin 2>/dev/null | grep -cE "push +\\\$0x1000a")
d=$(objdump -D -b binary -m i386 --adjust-vma=0x10a4e --start-address=0x10b4e --stop-address=0x10bb2 /tmp/h90.bin 2>/dev/null | grep -cE "call   \*0x4022a4")
[ "$c" = "4" ] && [ "$d" = "4" ] && note "C) VirtualAlloc(0x1000a)×4 走 table[0] 循环锚" || { note "C) FAIL $c/$d"; fail=1; }
grep -q "原理性失明" docs/82_mapbuilder_negation.md && grep -q "0x1F800400" docs/82_mapbuilder_negation.md && grep -q "docs/39 契约图的 native 构造代码" docs/82_mapbuilder_negation.md && note "E) docs/82 锚齐(含诚实否证)" || { note "E) 缺锚"; fail=1; }
dd=$(ls docs/*.md | wc -l); if grep -q "82_mapbuilder_negation" README.md && [ "$dd" -eq 86 ]; then note "F) README 与 docs=$dd 一致"; else note "F) FAIL docs=$dd"; fail=1; fi
s=$(sha256sum $E | cut -c1-8); [ "$s" = "b61ee13e" ] && note "G) EXE 只读恒" || { note "G) FAIL"; fail=1; }
[ $fail -eq 0 ] && echo "HOST90 CHECK PASS" || echo "HOST90 CHECK FAIL"
[ $fail -eq 0 ]
