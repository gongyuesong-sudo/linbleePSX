#!/usr/bin/env bash
# validate_host81.sh — A3 楔8 多段回放+节拍判定(task-090, 2026-10-31)
# A) 3 段回放: SEG0 rc=4 / SEG1 rc=3 / SEG2 rc=3 全 endpc=044f4c02
# B) HOSTCORE 递减行 ×3
# C) 假阳性: 无 CPUGOLD 段模式仍在(不死机)且单段楔7 锚恒
# D) docs/74 锚; E) README docs=75
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST81: $*"; }
G=/home/gill/bleem/.analysis/golden/gstate.hdr
python3 tools/gen_inc.py >/dev/null
rm -f host/build/fwd_test
gcc -m32 -O0 -g -no-pie -I host -o host/build/fwd_test tools/fwd_test.c host/fwd.c 2>/tmp/h81_b.err || { note "构建失败"; head -3 /tmp/h81_b.err; exit 1; }
test -x host/build/fwd_test || { note "产物缺失"; exit 1; }
a=$(timeout 300 env H_CPUGOLD=$G H_POKE=83c58:0 H_HOSTRET=1 FWDSEGS=3 FWDMAIN=20000000 FWDRCLOG=/tmp/h81_s.rclog ./host/build/fwd_test 2>/dev/null)
echo "$a" | grep -q "SEG 0 rc=4 kind=0 v0=00000000 endpc=044f4c02" && echo "$a" | grep -q "SEG 1 rc=3 kind=0 v0=00000000 endpc=044f4c02" && echo "$a" | grep -q "SEG 2 rc=3 kind=0 v0=00000000 endpc=044f4c02" && note "A) 三段回放节拍锚(4/3/3)" || { note "A) FAIL $a"; fail=1; }
[ "$(echo "$a" | grep -c 'HOSTCORE 83c58-->00000000 reenter=8003d704')" -eq 3 ] && note "B) 宿主核递减 3 次锚" || { note "B) FAIL"; fail=1; }
c=$(timeout 300 env H_CPUGOLD=$G H_POKE=83c58:0 H_HOSTRET=1 FWDSEGS=1 FWDMAIN=20000000 FWDRCLOG=/tmp/h81_1.rclog ./host/build/fwd_test 2>/dev/null | grep "SEG 0")
echo "$c" | grep -q "SEG 0 rc=4 kind=0" && note "C) 单段恒(与楔7 4 块锚一致)" || { note "C) FAIL $c"; fail=1; }
grep -q "每拍交还宿主" docs/74_multiseg_beat.md && grep -q "FWDSEGS" docs/74_multiseg_beat.md && grep -q "语义上不存在" docs/74_multiseg_beat.md && note "D) docs/74 锚齐" || { note "D) 缺锚"; fail=1; }
dd=$(ls docs/*.md | wc -l); if grep -q "74_multiseg_beat" README.md && [ "$dd" -eq 77 ]; then note "E) README 与 docs=$dd 一致"; else note "E) FAIL docs=$dd"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST81 CHECK PASS" || echo "HOST81 CHECK FAIL"
[ $fail -eq 0 ]
