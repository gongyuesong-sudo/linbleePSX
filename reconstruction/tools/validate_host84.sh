#!/usr/bin/env bash
# validate_host84.sh — x86 战线楔1 侦察基线(task-093, 2026-10-31)
# A) EXE sha 前缀恒 + PE 结构重放(.text VS=0x4A0, OVERLAY=287732)
# B) 导入 json 71 命名 API/6 DLL
# C) 只读性(本脚本零写 EXE)
# D) docs/76 锚; E) README docs=77
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST84: $*"; }
E=/home/gill/bleem/demo-v1_4.EXE
s=$(sha256sum $E | cut -c1-8); [ "$s" = "b61ee13e" ] && note "A1) EXE sha 前缀恒" || { note "A1) FAIL $s"; fail=1; }
r=$(python3 - <<'PY'
import struct
d=open('/home/gill/bleem/demo-v1_4.EXE','rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]; optsize=struct.unpack_from('<H',d,pe+20)[0]
so=pe+24+optsize
vs=struct.unpack_from('<I',d,so+8)[0]
end=0
for i in range(4):
    rs=struct.unpack_from('<I',d,so+i*40+16)[0]; ro=struct.unpack_from('<I',d,so+i*40+20)[0]
    end=max(end,ro+rs)
print('TEXTVS',hex(vs),'OVERLAY',len(d)-end,'SIZE',len(d))
PY
)
echo "$r" | grep -q "TEXTVS 0x4a0 OVERLAY 287732 SIZE 351732" && note "A2) PE 结构重放恒(1184B stub/overlay 287732)" || { note "A2) FAIL $r"; fail=1; }
b=$(python3 -c "
import json; imp=json.load(open('tools/x86_imports.json'))
print('DLLS',len(imp),'NAMED',sum(len(v) for v in imp.values()))")
echo "$b" | grep -q "DLLS 6 NAMED 71" && note "B) 导入面 6 DLL/71 命名 API 锚" || { note "B) FAIL $b"; fail=1; }
c=$(sha256sum $E | cut -c1-8); [ "$c" = "b61ee13e" ] && note "C) EXE 只读性恒" || { note "C) FAIL"; fail=1; }
grep -q "287,732" docs/76_x86_recon.md && grep -q "动态解析" docs/76_x86_recon.md && grep -q "楔2 定靶" docs/76_x86_recon.md && note "D) docs/76 锚齐" || { note "D) 缺锚"; fail=1; }
dd=$(ls docs/*.md | wc -l); if grep -q "76_x86_recon" README.md && [ "$dd" -eq 78 ]; then note "E) README 与 docs=$dd 一致"; else note "E) FAIL docs=$dd"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST84 CHECK PASS" || echo "HOST84 CHECK FAIL"
[ $fail -eq 0 ]
