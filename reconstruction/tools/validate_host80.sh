#!/usr/bin/env bash
# validate_host80.sh — A3 楔7 混合模型+首个 rc=0 完成段(task-089, 2026-10-31)
# A) RDUMP 20M 无自然递减锚(0x83C58=2 不变)
# B) HOSTRET 首段 rc=4 kind=0 endpc=044f4c02 + rclog HOSTRET 行
# C) 假阳性: 无 HOSTRET 恒 rc=393 EMITFAIL 04500eca(楔6 形态逐字)
# D) docs/73 锚; E) README docs=74
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST80: $*"; }
G=/home/gill/bleem/.analysis/golden/gstate.hdr
python3 tools/gen_inc.py >/dev/null
rm -f host/build/fwd_test
gcc -m32 -O0 -g -no-pie -I host -o host/build/fwd_test tools/fwd_test.c host/fwd.c 2>/tmp/h80_b.err || { note "构建失败"; head -3 /tmp/h80_b.err; exit 1; }
test -x host/build/fwd_test || { note "产物缺失"; exit 1; }
a=$(timeout 300 env H_CPUGOLD=$G H_RAMDUMP=83c58 FWDMAIN=20000000 FWDRCLOG=/tmp/h80_a.rclog ./host/build/fwd_test 2>/dev/null)
echo "$a" | grep -q "RDUMP 00083c58=00000002" && echo "$a" | grep -q "MAINRUN rc=20002 kind=0" && note "A) 无自然递减观测锚(20M)" || { note "A) FAIL $a"; fail=1; }
b=$(timeout 300 env H_CPUGOLD=$G H_POKE=83c58:0 H_HOSTRET=1 FWDMAIN=20000000 FWDRCLOG=/tmp/h80_b.rclog ./host/build/fwd_test 2>/dev/null)
echo "$b" | grep -q "MAINRUN rc=4 kind=0 v0=00000000 endpc=044f4c02" && grep -q "HOSTRET pc=044f4c02 ra=044f4c02" /tmp/h80_b.rclog && note "B) 首个 rc=0 完成段锚" || { note "B) FAIL $b"; fail=1; }
c=$(timeout 300 env H_CPUGOLD=$G H_POKE=83c58:0 FWDMAIN=20000000 FWDRCLOG=/tmp/h80_c.rclog ./host/build/fwd_test 2>/dev/null)
echo "$c" | grep -q "MAINRUN rc=393 kind=2 v0=00000000 endpc=04500e82" && grep -q "EMITFAIL pc=04500eca op=0ee40010 ra=044f4c02" /tmp/h80_c.rclog && note "C) 无 HOSTRET 假阳性门逐字恒" || { note "C) FAIL $c"; fail=1; }
grep -q "混合执行模型" docs/73_hostret_segment.md && grep -q "H_HOSTRET" docs/73_hostret_segment.md && grep -q "边界声明" docs/73_hostret_segment.md && note "D) docs/73 锚齐" || { note "D) 缺锚"; fail=1; }
dd=$(ls docs/*.md | wc -l); if grep -q "73_hostret_segment" README.md && [ "$dd" -eq 84 ]; then note "E) README 与 docs=$dd 一致"; else note "E) FAIL docs=$dd"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST80 CHECK PASS" || echo "HOST80 CHECK FAIL"
[ $fail -eq 0 ]
