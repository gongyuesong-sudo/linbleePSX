#!/usr/bin/env bash
# validate_host73.sh — A3 楔1 主运行 harness + 0x10C 宿主槽墙(task-083, 2026-10-31)
# A) M0 形态锚: MAINRUN endpc=000000b0 + EMITFAIL pc=10c op=4 ra=8005a314
# B) M1 六件套同停滞(主运行早于设备面, 零增益符合机制)
# C) rclog 恒 10 行(停滞点一致)
# D) docs/67 锚; E) README docs=68
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST73: $*"; }
python3 tools/gen_inc.py >/dev/null
gcc -m32 -O0 -g -no-pie -I host -o host/build/fwd_test tools/fwd_test.c host/fwd.c 2>&1 | grep -q error && { note "构建失败(必须零 error)"; exit 1; }
m0=$(timeout 300 env FWDMAIN=20000000 FWDRCLOG=/tmp/h73_m0.rclog ./host/build/fwd_test 2>&1)
echo "$m0" | grep -q "MAINRUN rc=114 kind=2 v0=00000001 endpc=000000b0" && note "A) M0 主运行停滞锚($?)" || { note "A) FAIL $m0"; fail=1; }
grep -q "EMITFAIL pc=0000010c op=00000004 ra=8005a314" /tmp/h73_m0.rclog && note "A2) 0x10C jr t9 宿主槽尸检锚(rclog)" || { note "A2) FAIL"; fail=1; }
m1=$(timeout 300 env H_IRQMASK=0xFFFFFFFF H_ISTATCLR=1 H_EVTGEN=0x1 H_I044OK=1 H_I044SM=1 H_TIMEROK=1 FWDMAIN=20000000 FWDRCLOG=/tmp/h73_m1.rclog ./host/build/fwd_test 2>&1)
echo "$m1" | grep -q "endpc=000000b0" && grep -q "EMITFAIL pc=0000010c op=00000004 ra=8005a314" /tmp/h73_m1.rclog && note "B) 六件套同停滞(机制自洽)" || { note "B) FAIL $m1"; fail=1; }
[ "$(wc -l < /tmp/h73_m0.rclog)" -eq 10 ] && [ "$(wc -l < /tmp/h73_m1.rclog)" -eq 10 ] && note "C) rclog 10 行恒定" || { note "C) FAIL"; fail=1; }
grep -q "host_service_cb" docs/67_mainrun_hostbridge.md && grep -q "宿主服务指针槽" docs/67_mainrun_hostbridge.md && grep -q "FWDMAIN" docs/67_mainrun_hostbridge.md && note "D) docs/67 锚齐" || { note "D) 缺锚"; fail=1; }
dd=$(ls docs/*.md | wc -l); if grep -q "67_mainrun_hostbridge" README.md && [ "$dd" -eq 72 ]; then note "E) README 与 docs=$dd 一致"; else note "E) FAIL docs=$dd"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST73 CHECK PASS" || echo "HOST73 CHECK FAIL"
[ $fail -eq 0 ]
