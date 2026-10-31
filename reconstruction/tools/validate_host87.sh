#!/usr/bin/env bash
# validate_host87.sh — x86 楔4 解包链会师(task-096, 2026-10-31)
# A) 块表 4 条目复算(fab7/1014f/10a4e/55338 + chk 4D24AF3D)
# B) 区域链=三流尺寸账目闭合
# C) DX 动态面串直证(stream_10a4c)
# D) 位引擎同源锚(docs/06 引用 0x55 系地址 n>=20)
# E) docs/79 锚; F) README docs=80; G) EXE 只读恒
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST87: $*"; }
E=/home/gill/bleem/demo-v1_4.EXE
python3 - <<'PY' || fail=1
import struct
d=open('/home/gill/bleem/demo-v1_4.EXE','rb').read()
u=lambda o: struct.unpack_from('<I',d,o)[0]
ok = (u(0xFA1c),u(0xFA2c),u(0xFA3c),u(0xFA4c))==(0xfab7,0x1014f,0x10a4e,0x55338) and u(0xFA58)==0x4D24AF3D and u(0xFA5c)==0x55df4
# 区域差 = 压缩尺寸
c1,c2,c3=0x1014f-0xfab7,0x10a4e-0x1014f,0x55338-0x10a4e
ok = ok and (c1,c2,c3)==(1688,2303,280810)
print("HOST87: A+B) 块表4条目+校验和+区域账目", "OK" if ok else "FAIL")
exit(0 if ok else 1)
PY
s=$(strings -a decrypted/stream_10a4c_L71cbf.bin 2>/dev/null | grep -cE "DDRAW\.DLL|DSOUND\.DLL|DINPUT\.DLL|DirectDrawCreate|DirectSoundCreate")
[ "$s" -ge 4 ] && note "C) DX 动态面串直证($s/5 命中)" || { note "C) FAIL n=$s"; fail=1; }
n=$(grep -rhoE "0x55[0-9a-f]{3}" docs/06_decryption.md tools/native_unpack/unpack_lib.c | sort -u | wc -l)
[ "$n" -ge 20 ] && note "D) 位引擎同源锚 $n 个(>=20)" || { note "D) FAIL n=$n"; fail=1; }
grep -q "会师" docs/79_unpack_chain_confluence.md && grep -q "4D24AF3D" docs/79_unpack_chain_confluence.md && grep -q "诚实勘正" docs/79_unpack_chain_confluence.md && note "E) docs/79 锚齐" || { note "E) 缺锚"; fail=1; }
dd=$(ls docs/*.md | wc -l); if grep -q "79_unpack_chain_confluence" README.md && [ "$dd" -eq 84 ]; then note "F) README 与 docs=$dd 一致"; else note "F) FAIL docs=$dd"; fail=1; fi
h=$(sha256sum $E | cut -c1-8); [ "$h" = "b61ee13e" ] && note "G) EXE 只读恒" || { note "G) FAIL"; fail=1; }
[ $fail -eq 0 ] && echo "HOST87 CHECK PASS" || echo "HOST87 CHECK FAIL"
[ $fail -eq 0 ]
