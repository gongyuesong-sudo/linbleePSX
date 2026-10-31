#!/usr/bin/env bash
# validate_host91.sh — x86 楔8 静态封顶(task-100, 2026-10-31)
# A) 三判据计数复算  B) s3 头 table[0]=11 全落 init 域  C) init 无静态被引
# D) call*reg 动态面 37  E) docs/83 锚 F) README docs=84 G) EXE 只读恒
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST91: $*"; }
E=/home/gill/bleem/demo-v1_4.EXE
python3 - <<'PY' || fail=1
import re,bisect,struct,subprocess
s3=open('decrypted/stream_10a4c_L71cbf.bin','rb').read()
open('/tmp/h91s3.bin','wb').write(s3[8:])
r=subprocess.run(['objdump','-D','-b','binary','-m','i386','--adjust-vma=0x10a4e','/tmp/h91s3.bin'],capture_output=True,text=True).stdout
ins=[]
for L in r.splitlines():
    m=re.match(r'\s+([0-9a-f]+):\t([0-9a-f]{2}(?: [0-9a-f]{2})*\s*)\t(\S+)\s*(.*)',L)
    if m: ins.append((int(m.group(1),16),m.group(3),m.group(4).strip()))
base=0x10a4e; adds=sorted(a for a,_,_ in ins)
backs=[(a,int(re.search(r'0x([0-9a-f]+)$',arg).group(1),16)) for a,op,arg in ins
       if op.startswith('j') and re.match(r'0x[0-9a-f]+$',arg or '')
       and int(re.search(r'0x([0-9a-f]+)$',arg).group(1),16)<a and int(re.search(r'0x([0-9a-f]+)$',arg).group(1),16)>=base]
def body(t,s):
    i0=bisect.bisect_left(adds,t); i1=bisect.bisect_right(adds,s); return ins[i0:i1]
sh={a for a,op,arg in ins if op=='call' and arg=='*0x4022a4'}
j2=sum(1 for s,t in backs if 0x100<s-t<0x3000 and t-base>=0x300 and any(a in sh for a,_,_ in body(t,s)))
calls=set()
for a,op,arg in ins:
    if op=='call' and re.match(r'0x[0-9a-f]+$',arg or ''): calls.add(int(arg,16))
has_ref=0x10a4e in calls
DISPS={'0x4','0x8','0xc','0x20','0x14','0x10','0x18','0x28','0x40','0x24','0x2c','0x44','0x48'}
cnt=0
for s,t in backs:
    if not(0x100<s-t<0x30000) or t-base<0x300: continue
    b=body(t,s); nc=sum(1 for a,op,arg in b if op=='call')
    ds=set()
    for a,op,arg in b:
        for mm in re.finditer(r'(0x[0-9a-f]+)\(%',arg or ''):
            if mm.group(1) in DISPS: ds.add(mm.group(1))
    if nc>=20 and len(ds)>=4: cnt+=1
raw=s3[8:]
nreg=sum(1 for i in range(8,len(raw)-1) if raw[i]==0xff and raw[i+1] in (0xd0,0xd1,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7))
ok = len(sh)==11 and all(0x10a4e<=x<=0x10d15 for x in sh) and j2==0 and not has_ref and cnt>=100 and nreg==37
print("HOST91: A) 判据1=%d(无判别力) 判据2环=%d 判据3可达空=%s"%(cnt,j2,not has_ref))
print("HOST91: B) table[0] 引用 %d 全落 init 域"%(len(sh)))
print("HOST91: C) init 静态被引: %s(应 False)"%has_ref)
print("HOST91: D) call*reg 动态面 %d"%nreg)
exit(0 if ok else 1)
PY
grep -q "静态路线封顶\|静态方法边界" docs/83_static_ceiling.md && grep -q "181" docs/83_static_ceiling.md && grep -q "0x10A4E" docs/83_static_ceiling.md && note "E) docs/83 锚齐" || { note "E) 缺锚"; fail=1; }
dd=$(ls docs/*.md | wc -l); if grep -q "83_static_ceiling" README.md && [ "$dd" -eq 90 ]; then note "F) README 与 docs=$dd 一致"; else note "F) FAIL docs=$dd"; fail=1; fi
s=$(sha256sum $E | cut -c1-8); [ "$s" = "b61ee13e" ] && note "G) EXE 只读恒" || { note "G) FAIL"; fail=1; }
[ $fail -eq 0 ] && echo "HOST91 CHECK PASS" || echo "HOST91 CHECK FAIL"
[ $fail -eq 0 ]
