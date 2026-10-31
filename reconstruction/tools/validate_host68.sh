#!/usr/bin/env bash
# validate_host68.sh — H-27 楔2 Timer 真装+生命周期终证(task-078, 2026-10-31)
# A) Timer 热点复算: 四段(无 TIMEROK) RD 0x1120 >= 400000
# B) 五段真装形态锚: 207248 块 EMITFAIL ra=0xE10(haltpc=000026d0)
# C) 假阳性: 无 TIMEROK 基线 4M@80059ed8 逐字 + 全序恒
# D) docs/63 锚; E) README docs=64
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST68: $*"; }
python3 tools/gen_inc.py >/dev/null
gcc -m32 -O0 -g -no-pie -I host -o host/build/fwd_test tools/fwd_test.c host/fwd.c 2>&1 | grep -q error && { note "构建失败(必须零 error)"; exit 1; }
echo "b0 3d" > /tmp/h68_3d.txt; rm -f /tmp/h68_rd.log
H_I044OK=1 H_IRQMASK=0xFFFFFFFF H_ISTATCLR=1 H_EVTGEN=0x1 FWDLIST=/tmp/h68_3d.txt H_JITDEV=/tmp/h68_rd.log timeout 300 ./host/build/fwd_test 2000000 >/dev/null 2>&1
n=$(grep -c "RD 1f801120" /tmp/h68_rd.log)
[ "$n" -ge 400000 ] && note "A) Timer2 计数窗热点复算 RD=$n(真时基缺口实证)" || { note "A) FAIL n=$n"; fail=1; }
b=$(H_TIMEROK=1 H_I044OK=1 H_IRQMASK=0xFFFFFFFF H_ISTATCLR=1 H_EVTGEN=0x1 FWDLIST=/tmp/h68_3d.txt timeout 500 ./host/build/fwd_test 4000000 2>&1 | grep "^FWD vec")
echo "$b" | grep -q "haltpc=000026d0" && note "B) 五段真装收敛切换墙锚: $b" || { note "B) FAIL $b"; fail=1; }
c=$(H_I044OK=1 H_IRQMASK=0xFFFFFFFF H_ISTATCLR=1 H_EVTGEN=0x1 FWDLIST=/tmp/h68_3d.txt timeout 500 ./host/build/fwd_test 4000000 2>&1 | grep "^FWD vec")
echo "$c" | grep -q "blocks=4000000 haltpc=80059ed8" && note "C1) 无 TIMEROK 基线活循环逐字恒" || { note "C1) FAIL $c"; fail=1; }
FWDLIST=/tmp/h59_all.txt timeout 300 ./host/build/fwd_test 2>&1 | grep -q "FWDSUM ok=2 ctx=22" && note "C2) 全序基线恒" || { note "C2) FAIL"; fail=1; }
grep -q "0x1F801120" docs/63_timer_lifecycle.md && grep -q "同一边界会师" docs/63_timer_lifecycle.md && grep -q "H_TIMEROK" docs/63_timer_lifecycle.md && note "D) docs/63 锚齐" || { note "D) 缺锚"; fail=1; }
d=$(ls docs/*.md | wc -l); if grep -q "63_timer_lifecycle" README.md && [ "$d" -eq 64 ]; then note "E) README 与 docs=$d 一致"; else note "E) FAIL docs=$d"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST68 CHECK PASS" || echo "HOST68 CHECK FAIL"
[ $fail -eq 0 ]
