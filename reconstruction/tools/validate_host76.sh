#!/usr/bin/env bash
# validate_host76.sh — A3 楔3 僵尸三角环真相(task-085, 2026-10-31)
# A) SEQTOP 三角三柱(0000/0080/0100 各 >=30M)
# B) SVCW 恒定现场 ra=00000000 t9=80079d00
# C) COP0 全零(异常假说否证锚)
# D) 楔2 里程碑回归: 100M 跑穿 kind=0
# E) docs/69 锚; F) README docs=70
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST76: $*"; }
python3 tools/gen_inc.py >/dev/null
gcc -m32 -O0 -g -no-pie -I host -o host/build/fwd_test tools/fwd_test.c host/fwd.c 2>&1 | grep -q error && { note "构建失败(必须零 error)"; exit 1; }
o=$(timeout 400 env H_SVCLOW=1 H_SEQTOP=1 H_SVCW=1 FWDMAIN=100000000 FWDRCLOG=/tmp/h76.rclog ./host/build/fwd_test 2>&1)
echo "$o" | grep -q "MAINRUN rc=100000000 kind=0" && note "D) 100M 跑穿回归恒" || { note "D) FAIL $o"; fail=1; }
echo "$o" | grep -q "COP0 status=00000000 cause=00000000 epc=00000000" && note "C) cop0 全零(异常假说否证)" || { note "C) FAIL"; fail=1; }
t1=$(grep "SEQTOP 0000 cnt=" /tmp/h76.rclog | sed 's/.*cnt=//'); t2=$(grep "SEQTOP 0080 cnt=" /tmp/h76.rclog | sed 's/.*cnt=//'); t3=$(grep "SEQTOP 0100 cnt=" /tmp/h76.rclog | sed 's/.*cnt=//')
if [ "${t1:-0}" -ge 30000000 ] && [ "${t2:-0}" -ge 30000000 ] && [ "${t3:-0}" -ge 30000000 ]; then note "A) 三角三柱 $t1/$t2/$t3"; else note "A) FAIL $t1/$t2/$t3"; fail=1; fi
grep -q "SVCW pc=00000080 ra=00000000 sp=800dfee0 t9=80079d00" /tmp/h76.rclog && note "B) 僵尸环恒定现场锚" || { note "B) FAIL"; fail=1; }
grep -q "反指标防腐蚀" docs/69_zombie_ring.md && grep -q "H_SEQTOP" docs/69_zombie_ring.md && grep -q "不成立" docs/69_zombie_ring.md && note "E) docs/69 锚齐" || { note "E) 缺锚"; fail=1; }
dd=$(ls docs/*.md | wc -l); if grep -q "69_zombie_ring" README.md && [ "$dd" -eq 75 ]; then note "F) README 与 docs=$dd 一致"; else note "F) FAIL docs=$dd"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST76 CHECK PASS" || echo "HOST76 CHECK FAIL"
[ $fail -eq 0 ]
