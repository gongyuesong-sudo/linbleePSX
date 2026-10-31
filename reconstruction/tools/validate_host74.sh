#!/usr/bin/env bash
# validate_host74.sh — A3 楔2 宿主桥+主运行亿块(task-084, 2026-10-31)
# A) SVCLOW 注入锚: 32 thunk
# B) M0' 桥 20M 跑穿: rc=20000000 kind=0 (楔1 同预算=120块死, 对照在 C)
# C) 无桥对照恒死: EMITFAIL pc=0000010c
# D) M4 桥 100M 跑穿 EMITFAIL=0
# E) docs/68 锚; F) README docs=69
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST74: $*"; }
python3 tools/gen_inc.py >/dev/null
gcc -m32 -O0 -g -no-pie -I host -o host/build/fwd_test tools/fwd_test.c host/fwd.c 2>&1 | grep -q error && { note "构建失败(必须零 error)"; exit 1; }
b=$(timeout 300 env H_SVCLOW=1 FWDMAIN=20000000 FWDRCLOG=/tmp/h74_m.rclog ./host/build/fwd_test 2>&1)
echo "$b" | grep -q "SVCLOW win=100..200 jr-ra thunks=32" && note "A) 桥注入 32 thunk" || { note "A) FAIL $b"; fail=1; }
echo "$b" | grep -q "MAINRUN rc=20000000 kind=0" && note "B) 桥 20M 跑穿" || { note "B) FAIL"; fail=1; }
c=$(timeout 300 env FWDMAIN=200000000 FWDRCLOG=/tmp/h74_n.rclog ./host/build/fwd_test 2>&1)
echo "$c" | grep -q "endpc=000000b0" && grep -q "EMITFAIL pc=0000010c op=00000004 ra=8005a314" /tmp/h74_n.rclog && note "C) 无桥对照恒死锚(楔1 形态)" || { note "C) FAIL $c"; fail=1; }
d=$(timeout 400 env H_SVCLOW=1 FWDMAIN=100000000 FWDRCLOG=/tmp/h74_100.rclog ./host/build/fwd_test 2>&1)
echo "$d" | grep -q "MAINRUN rc=100000000 kind=0" && [ "$(grep -c EMITFAIL /tmp/h74_100.rclog)" -eq 0 ] && note "D) 100M 跑穿零死跳(里程碑)" || { note "D) FAIL $d"; fail=1; }
grep -q "八个数量级" docs/68_mainrun_bridge.md && grep -q "H_SVCLOW" docs/68_mainrun_bridge.md && grep -q "异常向量页" docs/68_mainrun_bridge.md && note "E) docs/68 锚齐" || { note "E) 缺锚"; fail=1; }
dd=$(ls docs/*.md | wc -l); if grep -q "68_mainrun_bridge" README.md && [ "$dd" -eq 69 ]; then note "F) README 与 docs=$dd 一致"; else note "F) FAIL docs=$dd"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST74 CHECK PASS" || echo "HOST74 CHECK FAIL"
[ $fail -eq 0 ]
