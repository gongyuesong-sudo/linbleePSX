#!/usr/bin/env bash
# validate_host78.sh — A3 楔5 ra 归零全链(task-087, 2026-10-31)
# A) SVCT 序列锚: 前 2 跳 pc=b0 ra=8005a314/8005a338 合法; 第 3 跳 ra=0 t9=80079d00 sp=FEE0
# B) H_POKE 状态词双实验零影响(排除锚)
# C) 主运行 500K 形态恒(kind=0 进环)
# D) docs/71 锚; E) README docs=72
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST78: $*"; }
python3 tools/gen_inc.py >/dev/null
gcc -m32 -O0 -g -no-pie -I host -o host/build/fwd_test tools/fwd_test.c host/fwd.c 2>&1 | grep -q error && { note "构建失败(必须零 error)"; exit 1; }
timeout 200 env H_SVCLOW=1 H_SVCT=24 FWDMAIN=500000 FWDRCLOG=/tmp/h78_t.rclog ./host/build/fwd_test >/dev/null 2>&1
grep -q "SVCT 1 pc=000000b0 ra=8005a314" /tmp/h78_t.rclog && grep -q "SVCT 2 pc=000000b0 ra=8005a338" /tmp/h78_t.rclog && note "A1) 前两跳合法服务锚" || { note "A1) FAIL"; fail=1; }
grep -q "SVCT 3 pc=00000000 ra=00000000 t9=80079d00 v0=8005a844 sp=800dfee0" /tmp/h78_t.rclog && note "A2) 第 3 跳死帧转折锚(ra=0 sp=FEE0)" || { note "A2) FAIL"; fail=1; }
n1=$(timeout 120 env H_POKE=89d44:5a5a5a5a H_SVCLOW=1 H_SVCT=24 FWDMAIN=500000 FWDRCLOG=/tmp/h78_p.rclog ./host/build/fwd_test 2>/dev/null | grep -c "MAINRUN rc=500000 kind=0")
grep -q "SVCT 3 pc=00000000 ra=00000000" /tmp/h78_p.rclog && [ "$n1" -eq 1 ] && note "B) 状态词毒值零影响(假说排除)" || { note "B) FAIL"; fail=1; }
grep -q "disc.state" docs/71_ra_zero_trace.md && grep -q "H_CPUGOLD" docs/71_ra_zero_trace.md && grep -q "死栈帧\|死帧" docs/71_ra_zero_trace.md && note "D) docs/71 锚齐" || { note "D) 缺锚"; fail=1; }
dd=$(ls docs/*.md | wc -l); if grep -q "71_ra_zero_trace" README.md && [ "$dd" -eq 74 ]; then note "E) README 与 docs=$dd 一致"; else note "E) FAIL docs=$dd"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST78 CHECK PASS" || echo "HOST78 CHECK FAIL"
[ $fail -eq 0 ]
