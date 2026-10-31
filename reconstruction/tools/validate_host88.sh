#!/usr/bin/env bash
# validate_host88.sh — x86 楔5 零漂移审计+s3 真身(task-097, 2026-10-31)
# A) 六语义锚指令复算(shr/mov/cmp 系)  B) 23 锚全可反汇编(n=23)
# C) s1/s2/s3 头 dword0=0xe/0x9e/0x322  D) s3 串链(SYSTEM.CNF/PSX.EXE/KERNEL.EXE/App Paths)
# E) docs/80 锚; F) README docs=81; G) EXE 只读恒
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST88: $*"; }
E=/home/gill/bleem/demo-v1_4.EXE
python3 - <<'PY' || fail=1
import re,subprocess
d=open('/home/gill/bleem/demo-v1_4.EXE','rb').read()
open('/tmp/h88_s2.bin','wb').write(d[0x55338:])
anchors=set()
for f in ('docs/06_decryption.md','tools/native_unpack/unpack_lib.c'):
    anchors|=set(int(a,16) for a in re.findall(r'0x(55[0-9a-f]{3})',open(f).read()))
anchors.discard(0x55df4)  # 区界锚=EOF, 非指令
def first(a):
    r=subprocess.run(['objdump','-D','-b','binary','-m','i386','--adjust-vma=0x55338',
      '--start-address=0x%x'%a,'--stop-address=0x%x'%(a+16),'/tmp/h88_s2.bin'],capture_output=True,text=True).stdout
    for L in r.splitlines():
        m=re.match(r'\s+([0-9a-f]+):\t([0-9a-f]{2}(?: [0-9a-f]{2})*\s*)\t(\S+)\s*(.*)',L)
        if m: return (m.group(3),m.group(4).strip())
    return None
got={a:first(a) for a in sorted(anchors)}
n_ok=sum(1 for v in got.values() if v)
sem=[(0x55787,'shr','$1,%ebx'),(0x5578b,'mov','(%esi),%ebx'),(0x557a4,'shr','$1,%ebx'),
     (0x55960,'cmp','$0x100,%ebx'),(0x5599f,'mov','(%esi),%ebx'),(0x55986,'shr','$1,%ebx')]
bad=[hex(a) for a,op,arg in sem if not (got.get(a) and got[a][0]==op and got[a][1]==arg)]
print("HOST88: B) 锚可反汇编 %d/%d(0x55df4 区界豁免)"%(n_ok,len(anchors)))
print("HOST88: A) 六语义锚", "全中" if not bad and n_ok==len(anchors)==22 else "FAIL "+str(bad))
exit(0 if (not bad and n_ok==len(anchors)==22) else 1)
PY
h=$(python3 -c "
import struct
vals=[]
for f in ('decrypted/stream_0facc_Lba3.bin','decrypted/stream_1015c_L10e7.bin','decrypted/stream_10a4c_L71cbf.bin'):
    vals.append(struct.unpack_from('<I',open(f,'rb').read(),0)[0])
print(hex(vals[0]),hex(vals[1]),hex(vals[2]))")
[ "$h" = "0xe 0x9e 0x322" ] && note "C) 三流头数据表指针 0xe/0x9e/0x322" || { note "C) FAIL $h"; fail=1; }
c=$(strings -a decrypted/stream_10a4c_L71cbf.bin | grep -cE "SYSTEM\.CNF|PSX\.EXE|KERNEL\.EXE|App Paths")
[ "$c" -ge 4 ] && note "D) s3 CD 路径串链 $c/4" || { note "D) FAIL n=$c"; fail=1; }
grep -q "零漂移" docs/80_host_engine_s3.md && grep -q "0x4022a4" docs/80_host_engine_s3.md && grep -q "十进制" docs/80_host_engine_s3.md && note "E) docs/80 锚齐" || { note "E) 缺锚"; fail=1; }
dd=$(ls docs/*.md | wc -l); if grep -q "80_host_engine_s3" README.md && [ "$dd" -eq 84 ]; then note "F) README 与 docs=$dd 一致"; else note "F) FAIL docs=$dd"; fail=1; fi
s=$(sha256sum $E | cut -c1-8); [ "$s" = "b61ee13e" ] && note "G) EXE 只读恒" || { note "G) FAIL"; fail=1; }
[ $fail -eq 0 ] && echo "HOST88 CHECK PASS" || echo "HOST88 CHECK FAIL"
[ $fail -eq 0 ]
