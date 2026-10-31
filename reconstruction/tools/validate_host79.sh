#!/usr/bin/env bash
# validate_host79.sh — A3 楔6 真起点+首道设备墙(task-088, 2026-10-31)
# A) CPUGOLD 装载锚 pc=8003d704 sp=801fff00
# B) 真起点收敛 0x8003d6fc LOOP-DETECT rc=20002(有桥无桥同形)
# C) POKE=0 过首墙→第二墙 04500eca + golden 寄存器语境(ra=044f4c02)
# D) docs/72 锚; E) README docs=73
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST79: $*"; }
G=/home/gill/bleem/.analysis/golden/gstate.hdr
python3 tools/gen_inc.py >/dev/null
rm -f host/build/fwd_test
gcc -m32 -O0 -g -no-pie -I host -o host/build/fwd_test tools/fwd_test.c host/fwd.c 2>/tmp/h79_b.err; [ $? -ne 0 ] && { note "构建失败(必须零 error)"; head -3 /tmp/h79_b.err; exit 1; }
test -x host/build/fwd_test || { note "构建产物缺失"; exit 1; }
a=$(timeout 200 env H_CPUGOLD=$G FWDMAIN=20000 ./host/build/fwd_test 2>&1)
echo "$a" | grep -q "CPUGOLD pc=8003d704 sp=801fff00 ra=044f4c02" && note "A) 金标 CPU 装载锚" || { note "A) FAIL $a"; fail=1; }
b=$(timeout 200 env H_CPUGOLD=$G FWDMAIN=20000000 FWDRCLOG=/tmp/h79_1.rclog ./host/build/fwd_test 2>/dev/null)
echo "$b" | grep -q "MAINRUN rc=20002 kind=0 v0=800ea868 endpc=8003d6fc" && grep -q "LOOP-DETECT pc=8003d6fc" /tmp/h79_1.rclog && note "B) 真起点首墙收敛锚" || { note "B) FAIL $b"; fail=1; }
c=$(timeout 200 env H_CPUGOLD=$G H_POKE=83c58:0 FWDMAIN=20000000 FWDRCLOG=/tmp/h79_2.rclog ./host/build/fwd_test 2>/dev/null)
echo "$c" | grep -q "MAINRUN rc=393 kind=2 v0=00000000 endpc=04500e82" && grep -q "EMITFAIL pc=04500eca op=0ee40010 ra=044f4c02 sp=801fff00" /tmp/h79_2.rclog && note "C) 过首墙+第二墙+golden 语境锚" || { note "C) FAIL $c"; fail=1; }
grep -q "H_CPUGOLD" docs/72_cpugold_firstwall.md && grep -q "0x8008_3C58" docs/72_cpugold_firstwall.md && grep -q "假起点产物" docs/72_cpugold_firstwall.md && note "D) docs/72 锚齐" || { note "D) 缺锚"; fail=1; }
dd=$(ls docs/*.md | wc -l); if grep -q "72_cpugold_firstwall" README.md && [ "$dd" -eq 75 ]; then note "E) README 与 docs=$dd 一致"; else note "E) FAIL docs=$dd"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST79 CHECK PASS" || echo "HOST79 CHECK FAIL"
[ $fail -eq 0 ]
