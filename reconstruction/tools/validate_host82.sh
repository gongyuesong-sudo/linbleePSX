#!/usr/bin/env bash
# validate_host82.sh — A3 楔9 段间注入全否(task-091, 2026-10-31)
# A) 基线 10 拍恒 4/3×9
# B) 三候选注入组全部同基线(零差异)
# C) 单段假阳性门恒(楔7 rc=4)
# D) docs/75 锚; E) README docs=76
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST82: $*"; }
G=/home/gill/bleem/.analysis/golden/gstate.hdr
python3 tools/gen_inc.py >/dev/null
rm -f host/build/fwd_test
gcc -m32 -O0 -g -no-pie -I host -o host/build/fwd_test tools/fwd_test.c host/fwd.c 2>/tmp/h82_b.err || { note "构建失败"; exit 1; }
test -x host/build/fwd_test || { note "产物缺失"; exit 1; }
E="H_CPUGOLD=$G H_POKE=83c58:0 H_HOSTRET=1 FWDSEGS=10 FWDMAIN=20000000"
pat(){ env $E "$@" FWDRCLOG=/tmp/h82_x.rclog timeout 200 ./host/build/fwd_test 2>/dev/null | grep -E "^SEG" | awk '{printf "%s ",$3}'; echo; }
p0=$(pat); p1=$(pat H_SEGP=89d48:1); p2=$(pat H_SEGP=89d44:80000000); p3=$(pat H_SEGP=6ce0:85d8,83c58:1)
[ "$p0" = "rc=4 rc=3 rc=3 rc=3 rc=3 rc=3 rc=3 rc=3 rc=3 rc=3 " ] && note "A) 基线 10 拍恒(4/3x9)" || { note "A) FAIL $p0"; fail=1; }
if [ "$p1" = "$p0" ] && [ "$p2" = "$p0" ] && [ "$p3" = "$p0" ]; then note "B) 三注入组零差异(定谳锚)"; else note "B) FAIL p1=$p1 p2=$p2 p3=$p3"; fail=1; fi
c=$(env H_CPUGOLD=$G H_POKE=83c58:0 H_HOSTRET=1 FWDSEGS=1 FWDMAIN=20000000 FWDRCLOG=/tmp/h82_1.rclog timeout 200 ./host/build/fwd_test 2>/dev/null | grep "SEG 0")
echo "$c" | grep -q "rc=4 kind=0 v0=00000000 endpc=044f4c02" && note "C) 单段假阳性门恒" || { note "C) FAIL $c"; fail=1; }
grep -q "分发器在宿主侧\|唤醒协议" docs/75_seginject_falsified.md && grep -q "H_SEGP" docs/75_seginject_falsified.md && grep -q "实测穷尽" docs/75_seginject_falsified.md && note "D) docs/75 锚齐" || { note "D) 缺锚"; fail=1; }
dd=$(ls docs/*.md | wc -l); if grep -q "75_seginject_falsified" README.md && [ "$dd" -eq 83 ]; then note "E) README 与 docs=$dd 一致"; else note "E) FAIL docs=$dd"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST82 CHECK PASS" || echo "HOST82 CHECK FAIL"
[ $fail -eq 0 ]
