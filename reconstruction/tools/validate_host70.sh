#!/usr/bin/env bash
# validate_host70.sh — C2 绿色线程发现+magic 否证(task-080, 2026-10-31)
# A) magic 0x27D54445 全源 0 命中(4 文件)
# B) 双份工作区 96B 逐词全等 + 不在 relocated 流内 + 哨兵 10 处
# C) 队列 0x6CE0 布局复Dump
# D) 0x3d 五段形态恒(207248@0x26d0)
# E) docs/65 锚; F) README docs=66
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST70: $*"; }
G=/home/gill/bleem/.analysis/golden/GOLDEN_RAM_2MB.bin
r=$(python3 - <<'EOF'
import struct,glob,os
g=open('/home/gill/bleem/.analysis/golden/GOLDEN_RAM_2MB.bin','rb').read()
n=0
for p in ['/home/gill/bleem/.analysis/golden/GOLDEN_RAM_2MB.bin','/home/gill/bleem/psxbios/SCPH1001.BIN']+glob.glob('/home/gill/bleem/rewrite/decrypted/stream_*.bin'):
    n+=open(p,'rb').read().find(struct.pack('<I',0x27D54445))>=1 and 1 or 0
print('MAGICHITS',n)
print('EQUAL',g[0x66C4:0x66C4+96]==g[0x42D24:0x42D24+96])
print('INSTRE',open('/home/gill/bleem/rewrite/decrypted/stream_10a4c_L71cbf_relocated.bin','rb').read().find(g[0x66C4:0x66C4+96]))
print('SENT',open('/home/gill/bleem/.analysis/golden/GOLDEN_RAM_2MB.bin','rb').read().count(struct.pack('<I',0x93A20020)))
q=[struct.unpack_from('<I',g,0x6CE0+4*i)[0] for i in range(7)]
print('QUEUE',[hex(x) for x in q])
EOF
)
echo "$r" | grep -q "MAGICHITS 0" && note "A) magic 全源 0 命中(记忆参数非客体资产)" || { note "A) FAIL"; fail=1; }
echo "$r" | grep -q "EQUAL True" && echo "$r" | grep -q "INSTRE -1" && [ "$(echo "$r" | grep SENT | awk '{print $2}')" -eq 10 ] && note "B) 双镜像全等+非静态+哨兵10 锚齐" || { note "B) FAIL $r"; fail=1; }
echo "$r" | grep -q "QUEUE \['0x66c4', '0x67b8', '0x6870', '0x6928', '0x85d8', '0xf40', '0x85d4'\]" && note "C) 绿色线程队列表逐词恒" || { note "C) FAIL"; fail=1; }
python3 tools/gen_inc.py >/dev/null
gcc -m32 -O0 -g -no-pie -I host -o host/build/fwd_test tools/fwd_test.c host/fwd.c 2>&1 | grep -q error && { note "构建失败(必须零 error)"; exit 1; }
printf "b0 3d\n" > /tmp/h70_3d.txt
d=$(env H_TIMEROK=1 H_I044OK=1 H_IRQMASK=0xFFFFFFFF H_ISTATCLR=1 H_EVTGEN=0x1 FWDLIST=/tmp/h70_3d.txt timeout 300 ./host/build/fwd_test 4000000 2>&1 | grep "^FWD vec")
echo "$d" | grep -q "blocks=207248 haltpc=000026d0" && note "D) 五段形态逐字恒" || { note "D) FAIL $d"; fail=1; }
grep -q "0x93A20020" docs/65_c2_green_thread.md && grep -q "绿色线程" docs/65_c2_green_thread.md && grep -q "结构性无法到达" docs/65_c2_green_thread.md && note "E) docs/65 锚齐" || { note "E) 缺锚"; fail=1; }
dd=$(ls docs/*.md | wc -l); if grep -q "65_c2_green_thread" README.md && [ "$dd" -eq 71 ]; then note "F) README 与 docs=$dd 一致"; else note "F) FAIL docs=$dd"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST70 CHECK PASS" || echo "HOST70 CHECK FAIL"
[ $fail -eq 0 ]
