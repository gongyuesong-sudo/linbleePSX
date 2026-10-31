#!/usr/bin/env bash
# validate_host67.sh — H-27 楔1 设备真装四段链终证(task-077, 2026-10-31)
# A) MASK 关键: 0x3d +H_IRQMASK 从 119220 → <20000 过轮询
# B) 全链真装形态: 四段组合 2M 预算 halt=80059ed8(活循环 C2 深码)且 blocks>=2M (无 RAFIX/ZEROFIX)
# C) I044 单段无组合不劣化(119220 基线)
# D) 假阳性: 全序无 H-27 env 恒 ok=2 ctx=22
# E) docs/62 锚; F) README docs=63
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST67: $*"; }
python3 tools/gen_inc.py >/dev/null
gcc -m32 -O0 -g -no-pie -I host -o host/build/fwd_test tools/fwd_test.c host/fwd.c 2>&1 | grep -q error && { note "构建失败(必须零 error)"; exit 1; }
echo "b0 3d" > /tmp/h67_3d.txt
a=$(H_IRQMASK=0xFFFFFFFF FWDLIST=/tmp/h67_3d.txt timeout 200 ./host/build/fwd_test 2>&1 | grep "^FWD vec")
blk=$(echo "$a" | sed 's/.*blocks=\([0-9]*\).*/\1/')
echo "$a" | grep -q "haltpc=000000a0" && [ "$blk" -lt 20000 ] && note "A) MASK=交始假总根因: $a" || { note "A) FAIL $a"; fail=1; }
b=$(H_I044OK=1 H_IRQMASK=0xFFFFFFFF H_ISTATCLR=1 H_EVTGEN=0x1 FWDLIST=/tmp/h67_3d.txt timeout 400 ./host/build/fwd_test 2000000 2>&1 | grep "^FWD vec")
blk2=$(echo "$b" | sed 's/.*blocks=\([0-9]*\).*/\1/')
echo "$b" | grep -q "haltpc=80059ed8" && [ "$blk2" -ge 2000000 ] && note "B) 四段全链真装=活循环至 C2 深码(非死跳非接管): $b" || { note "B) FAIL $b"; fail=1; }
c=$(H_I044OK=1 FWDLIST=/tmp/h67_3d.txt timeout 200 ./host/build/fwd_test 2>&1 | grep "^FWD vec")
echo "$c" | grep -q "blocks=119220" && note "C) I044 单段不劣化基线" || { note "C) FAIL $c"; fail=1; }
FWDLIST=/tmp/h59_all.txt timeout 300 ./host/build/fwd_test 2>&1 | grep -q "FWDSUM ok=2 ctx=22 miss=0" && note "D) 假阳性门恒" || { note "D) FAIL"; fail=1; }
grep -q "交始终假" docs/62_devreal_chain.md && grep -q "0x1F801040-4F" docs/62_devreal_chain.md && grep -q "真实设备实装" docs/62_devreal_chain.md && note "E) docs/62 锚齐" || { note "E) 缺锚"; fail=1; }
d=$(ls docs/*.md | wc -l); if grep -q "62_devreal_chain" README.md && [ "$d" -eq 67 ]; then note "F) README 与 docs=$d 一致"; else note "F) FAIL docs=$d"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST67 CHECK PASS" || echo "HOST67 CHECK FAIL"
[ $fail -eq 0 ]
