#!/usr/bin/env bash
# validate_host71.sh — A2 否证: 五栈=MIPS JIT 自生码(task-081, 2026-10-31)
# A) 0x66C4 帧区 40 词反汇编: sltiu/bne/beq/addiu 族 >= 28(分派码判定)
# B) 块头 4 份(66CC/6878/42D2C/42ED8) + 三流 0 命中
# C) 0x93A20020 = sb v0,0x20(sp) 勘正锚
# D) 0x3d 五段形态恒(207248@0x26d0)
# E) docs/66 锚; F) README docs=67
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST71: $*"; }
G=/home/gill/bleem/.analysis/golden/GOLDEN_RAM_2MB.bin
a=$(python3 tools/mips_dis.py $G 66c4 40 2>/dev/null)
n=$(echo "$a" | grep -Ec "sltiu|bne|beq|addiu")
[ "$n" -ge 28 ] && note "A) 帧区 MIPS 分派码词数=$n/40(合法 MIPS 判定)" || { note "A) FAIL n=$n"; fail=1; }
r=$(python3 - <<'EOF'
import struct
g=open('/home/gill/bleem/.analysis/golden/GOLDEN_RAM_2MB.bin','rb').read()
pat=g[0x66CC:0x66CC+16]; locs=[]; off=0
while True:
    i=g.find(pat,off)
    if i<0: break
    locs.append(hex(i)); off=i+1
st=sum(1 for f in ('stream_0facc_Lba3','stream_1015c_L10e7','stream_10a4c_L71cbf','stream_10a4c_L71cbf_relocated')
       if open(f'/home/gill/bleem/rewrite/decrypted/{f}.bin','rb').read().find(pat)>=0)
print('LOCS',','.join(sorted(locs))); print('STREAMHITS',st)
EOF
)
echo "$r" | grep -q "LOCS 0x42d2c,0x42ed8,0x66cc,0x6878" && echo "$r" | grep -q "STREAMHITS 0" && note "B) 4 份码块+三流 0 命中(运行时生成实证)" || { note "B) FAIL $r"; fail=1; }
echo "$a" | head -1 | grep -q "sb v0,sp,0x20" && note "C) 0x93A20020=sb v0,0x20(sp) 勘正锚" || { note "C) FAIL"; fail=1; }
python3 tools/gen_inc.py >/dev/null
gcc -m32 -O0 -g -no-pie -I host -o host/build/fwd_test tools/fwd_test.c host/fwd.c 2>&1 | grep -q error && { note "构建失败(必须零 error)"; exit 1; }
printf "b0 3d\n" > /tmp/h71_3d.txt
d=$(env H_TIMEROK=1 H_I044OK=1 H_IRQMASK=0xFFFFFFFF H_ISTATCLR=1 H_EVTGEN=0x1 FWDLIST=/tmp/h71_3d.txt timeout 300 ./host/build/fwd_test 4000000 2>&1 | grep "^FWD vec")
echo "$d" | grep -q "blocks=207248 haltpc=000026d0" && note "D) 五段形态逐字恒" || { note "D) FAIL $d"; fail=1; }
grep -q "运行时生成" docs/66_mips_jit_falsified_vm.md && grep -q "工程量问题而非认知问题" docs/66_mips_jit_falsified_vm.md && grep -q "sb v0,0x20(sp)" docs/66_mips_jit_falsified_vm.md && note "E) docs/66 锚齐" || { note "E) 缺锚"; fail=1; }
dd=$(ls docs/*.md | wc -l); if grep -q "66_mips_jit_falsified_vm" README.md && [ "$dd" -eq 74 ]; then note "F) README 与 docs=$dd 一致"; else note "F) FAIL docs=$dd"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST71 CHECK PASS" || echo "HOST71 CHECK FAIL"
[ $fail -eq 0 ]
