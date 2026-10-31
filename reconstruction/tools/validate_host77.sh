#!/usr/bin/env bash
# validate_host77.sh — A3 楔4 活现场解剖(task-086, 2026-10-31)
# A) 五点 RAM≠ROM 双源全 DIFF
# B) 0x5A314 RAM 侧 = lw s4,sp,0x2c(8FB4002C) 合法指令锚
# C) 0x79D00 函数表: 前6词指针族 + "intr" 串
# D) 主运行 100M 形态恒(楔2 回归 + 僵尸环 v0=8005a844)
# E) docs/70 锚; F) README docs=71
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST77: $*"; }
r=$(python3 - <<'EOF'
import struct
g=open('/home/gill/bleem/.analysis/golden/GOLDEN_RAM_2MB.bin','rb').read()
rom=open('/home/gill/bleem/psxbios/SCPH1001.BIN','rb').read()
def w(b,a): return struct.unpack_from('<I',b,a)[0]
diffs=sum(1 for a in (0x5a314,0x5a1d8,0x5aba0,0x79d00,0x79b54) if w(g,a)!=w(rom,a))
print('DIFFS',diffs)
print('W5A314',hex(w(g,0x5a314)))
tbl=[w(g,0x79d00+4*i) for i in range(6)]
print('TBL',' '.join(hex(x) for x in tbl))
print('INTR', g[0x79d00+24:0x79d00+28]==b'intr')
EOF
)
echo "$r" | grep -q "DIFFS 5" && note "A) 五点双源全 DIFF(BIOS 镜像=运行时改写)" || { note "A) FAIL $r"; fail=1; }
echo "$r" | grep -q "W5A314 0x8fb4002c" && note "B) 0x5A314=lw s4,sp,0x2c 合法锚" || { note "B) FAIL"; fail=1; }
echo "$r" | grep -q "0x8005a844 0x8005a380 0x8005a160 0x8005a424 0x8005a5fc" && echo "$r" | grep -q "INTR True" && note "C) 0x79D00 中断服务表锚(指针族+intr 串)" || { note "C) FAIL $r"; fail=1; }
python3 tools/gen_inc.py >/dev/null
gcc -m32 -O0 -g -no-pie -I host -o host/build/fwd_test tools/fwd_test.c host/fwd.c 2>&1 | grep -q error && { note "构建失败(必须零 error)"; exit 1; }
d=$(timeout 400 env H_SVCLOW=1 FWDMAIN=100000000 FWDRCLOG=/tmp/h77.rclog ./host/build/fwd_test 2>&1 | grep MAINRUN)
echo "$d" | grep -q "MAINRUN rc=100000000 kind=0 v0=8005a844 endpc=00000080" && note "D) 主运行形态恒(v0=表槽[1]互证)" || { note "D) FAIL $d"; fail=1; }
grep -q "运行时自适应补丁码区" docs/70_livescene_patchcode.md && grep -q "活现场" docs/70_livescene_patchcode.md && grep -q '"intr"' docs/70_livescene_patchcode.md && note "E) docs/70 锚齐" || { note "E) 缺锚"; fail=1; }
dd=$(ls docs/*.md | wc -l); if grep -q "70_livescene_patchcode" README.md && [ "$dd" -eq 72 ]; then note "F) README 与 docs=$dd 一致"; else note "F) FAIL docs=$dd"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST77 CHECK PASS" || echo "HOST77 CHECK FAIL"
[ $fail -eq 0 ]
