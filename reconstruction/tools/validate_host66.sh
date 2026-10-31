#!/usr/bin/env bash
# validate_host66.sh — H-26 楔3 表槽否证终证(task-076, 2026-10-31)
# A) 三域零槽数复算 = 296 且 FWDSLOT filled 一致
# B) 全序四组合(SLOT/SERVE+TICK/ZEROFIX/全叠加)恒 ok=2 ctx=22(否证锚)
# C) b0/0c SLOT 深推形态: EMITFAIL pc=0000010c ×≥200 且 haltpc=000000b0
# D) 假阳性: 无 SLOT 基线 b0/0c 逐字 4157 + 无 SLOT 全序恒
# E) docs/61 锚; F) README docs=62
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST66: $*"; }
python3 tools/gen_inc.py >/dev/null
gcc -m32 -O0 -g -no-pie -I host -o host/build/fwd_test tools/fwd_test.c host/fwd.c 2>&1 | grep -q error && { note "构建失败(必须零 error)"; exit 1; }
python3 - <<'PYEOF' || fail=1
import struct
g=open('/home/gill/bleem/.analysis/golden/GOLDEN_RAM_2MB.bin','rb').read()
per=[sum(1 for k in range(256) if struct.unpack_from('<I',g,b+k*4)[0]==0) for b in (0x200,0x874,0x674)]
uniq=len({b+k*4 for b in (0x200,0x874,0x674) for k in range(256) if struct.unpack_from('<I',g,b+k*4)[0]==0})
assert per==[23,175,145] and uniq==296,(per,uniq)   # 0x874/0x674 域交叠 47 槽, 运行时去重=296
print(f"HOST66: A) 三域零槽复算 per={per} 去重 uniq={uniq}(域交叠 47)")
PYEOF
o=$(FWDSLOT=1 FWDLIST=/tmp/h59_all.txt timeout 300 ./host/build/fwd_test 2>&1 | grep -E "FWDSLOT|FWDSUM")
echo "$o" | grep -q "filled=296" && echo "$o" | grep -q "ok=2 ctx=22" && note "B1) SLOT 填 296+批次态恒" || { note "B1) FAIL $o"; fail=1; }
for comb in "FWDSLOT=1 FWDSERVE=1 H_IRQTICK=1" "FWDSLOT=1 H_ZEROFIX=1" "FWDSLOT=1 FWDSERVE=1 H_IRQTICK=1 H_ZEROFIX=1"; do
  env $comb FWDLIST=/tmp/h59_all.txt timeout 300 ./host/build/fwd_test 2>&1 | grep -q "FWDSUM ok=2 ctx=22" || { note "B) FAIL $comb"; fail=1; }
done
[ $fail -eq 0 ] && note "B) 全组合批次态恒 ok=2 ctx=22(表槽否证锚)"
echo "b0 c" > /tmp/h66_c.txt; rm -f /tmp/h66_lc.txt
FWDSLOT=1 FWDSERVE=1 H_IRQTICK=1 H_ZEROFIX=1 FWDLOG=/tmp/h66_lc.txt FWDLIST=/tmp/h66_c.txt timeout 120 ./host/build/fwd_test >/dev/null 2>&1
n=$(grep -c "EMITFAIL pc=0000010c" /tmp/h66_lc.txt); [ "$n" -ge 200 ] && note "C) b0/0c 收敛形态锚定: 0x10C printf 劫持 EMITFAIL×$n" || { note "C) FAIL n=$n"; fail=1; }
b=$(FWDLIST=/tmp/h66_c.txt FWDSERVE=1 H_IRQTICK=1 timeout 120 ./host/build/fwd_test 2>&1 | grep "^FWD vec")
echo "$b" | grep -q "blocks=4157 haltpc=000000b0" && note "D) 无 SLOT 基线逐字不变(假阳性净)" || { note "D) FAIL $b"; fail=1; }
grep -q "被实验否证" docs/61_slot_falsified.md && grep -q "六类全试毕" docs/61_slot_falsified.md && grep -q "H-26 终判封线" docs/61_slot_falsified.md && note "E) docs/61 锚齐" || { note "E) 缺锚"; fail=1; }
d=$(ls docs/*.md | wc -l); if grep -q "61_slot_falsified" README.md && [ "$d" -eq 65 ]; then note "F) README 与 docs=$d 一致"; else note "F) FAIL docs=$d"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST66 CHECK PASS" || echo "HOST66 CHECK FAIL"
[ $fail -eq 0 ]
