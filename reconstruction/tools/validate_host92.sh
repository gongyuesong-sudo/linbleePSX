#!/usr/bin/env bash
# validate_host92.sh — x86 楔9 Win16 API 发现+wine 边界(task-101, 2026-10-31)
# A) imports 修正版: 119 槽 + ORD#1/35/36/37 + MapLS/UnMapLS/QT_Thunk
# B) win16 屏蔽补丁可重放(sha 前缀 3b3e110e), 原 EXE 恒 b61ee13e
# C) 补丁副本 14 条目全替换复算  D) docs/84 锚 E) README docs=85 F) EXE 只读恒
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST92: $*"; }
E=/home/gill/bleem/demo-v1_4.EXE
a=$(python3 -c "
import json
imp=json.load(open('tools/x86_imports.json'))
k=imp['KERNEL32.DLL']; u=imp['USER32.DLL']
tots=sum(len(v) for v in imp.values())
ords=[x for x in k if x.startswith('ORD#')]
w16=[x for x in k if x in ('MapLS','UnMapLS','QT_Thunk')]
print(tots, sorted(ords)==['ORD#1','ORD#35','ORD#36','ORD#37'], sorted(w16)==['MapLS','QT_Thunk','UnMapLS'])")
[ "$a" = "119 True True" ] && note "A) imports 修正版 119 槽+Win16 七处锚" || { note "A) FAIL $a"; fail=1; }
b=$(python3 - <<'PY'
import struct,hashlib
d=bytearray(open('/home/gill/bleem/demo-v1_4.EXE','rb').read())
pe=struct.unpack_from('<I',d,0x3c)[0]; optsize=struct.unpack_from('<H',d,pe+20)[0]; so=pe+24+optsize
n=struct.unpack_from('<H',d,pe+6)[0]
secs=[]
for i in range(n):
    vs,va=struct.unpack_from('<II',d,so+i*40+8); rs,ro=struct.unpack_from('<II',d,so+i*40+16)
    secs.append((va,vs,ro,rs))
def v2o(v):
    for va,vs,ro,rs in secs:
        if va<=v<va+max(vs,rs): return ro+(v-va)
def nm_at(th):
    o=v2o(th&0x7fffffff)+2
    return d[o:d.index(b'\0',o)].decode()
ORDMAP={1:0x284e,35:0x28a4,36:0x2768,37:0x284e}
NAME={'MapLS':0x284e,'UnMapLS':0x284e,'QT_Thunk':0x2768}
ido=v2o(struct.unpack_from('<II',d,pe+24+96+8)[0])
i=0; log=[]
while True:
    ilt,rft,toc,name,iat=struct.unpack('<5I',d[ido+i*20:ido+i*20+20])
    if name==0: break
    dll=d[v2o(name):d.index(b'\0',v2o(name))].decode()
    if 'kernel' in dll.lower():
        for base in ({ilt,iat} if ilt and iat else {ilt or iat}):
            o=v2o(base)
            while True:
                th=struct.unpack_from('<I',d,o)[0]
                if th==0: break
                if th>>31 and (th&0xffff) in ORDMAP:
                    log.append(1); struct.pack_into('<I',d,o,ORDMAP[th&0xffff])
                elif not th>>31 and nm_at(th) in NAME:
                    log.append(1); struct.pack_into('<I',d,o,NAME[nm_at(th)])
                o+=4
    i+=1
open('/tmp/h92_patched.EXE','wb').write(bytes(d))
print(len(log), hashlib.sha256(bytes(d)).hexdigest()[:8])
PY
)
[ "$b" = "14 3b3e110e" ] && note "B) 补丁重放 14 条目 sha 3b3e110e 恒" || { note "B) FAIL $b"; fail=1; }
grep -q "FreeLibrary16" docs/84_wine_boundary.md && grep -q "ptrace_scope" docs/84_wine_boundary.md && grep -q "native -m32" docs/84_wine_boundary.md && note "C) docs/84 锚齐" || { note "C) 缺锚"; fail=1; }
grep -q "krnl386" docs/84_wine_boundary.md && grep -q "3b3e110e" docs/84_wine_boundary.md && note "D) docs/84 取证锚(补丁 sha/krnl386)在" || { note "D) 缺"; fail=1; }
dd=$(ls docs/*.md | wc -l); if grep -q "84_wine_boundary" README.md && [ "$dd" -eq 85 ]; then note "E) README 与 docs=$dd 一致"; else note "E) FAIL docs=$dd"; fail=1; fi
s=$(sha256sum $E | cut -c1-8); [ "$s" = "b61ee13e" ] && note "F) 原 EXE 只读恒" || { note "F) FAIL"; fail=1; }
[ $fail -eq 0 ] && echo "HOST92 CHECK PASS" || echo "HOST92 CHECK FAIL"
[ $fail -eq 0 ]
