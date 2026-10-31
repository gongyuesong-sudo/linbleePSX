#!/usr/bin/env bash
# validate_host69.sh — H-27 楔3第一砖双否证(task-079, 2026-10-31)
# A) E1 SM 零增益: 六段 0x3d 逐字 = 五段基线(207248 haltpc=000026d0)
# B) E2 create 污染: CREQ 1C@bfc0d950 + 0x3d 劣化 <1000 块
# C) 假阳性: 无 SM/CREATE 五段基线逐字 + 全序恒定
# D) docs/64 锚; E) README docs=65
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST69: $*"; }
python3 tools/gen_inc.py >/dev/null
gcc -m32 -O0 -g -no-pie -I host -o host/build/fwd_test tools/fwd_test.c host/fwd.c 2>&1 | grep -q error && { note "构建失败(必须零 error)"; exit 1; }
printf "b0 3d\n" > /tmp/h69_3d.txt; printf "b0 1c\nb0 1c\nc0 00\n" > /tmp/h69_create.txt
H5="H_TIMEROK=1 H_I044OK=1 H_IRQMASK=0xFFFFFFFF H_ISTATCLR=1 H_EVTGEN=0x1"
a=$(env H_I044SM=1 $H5 FWDLIST=/tmp/h69_3d.txt timeout 300 ./host/build/fwd_test 4000000 2>&1 | grep "^FWD vec")
echo "$a" | grep -q "blocks=207248 haltpc=000026d0" && note "A) SM 零增益逐字恒: $a" || { note "A) FAIL $a"; fail=1; }
b=$(env H_I044SM=1 $H5 FWDCREATE=/tmp/h69_create.txt FWDLIST=/tmp/h69_3d.txt timeout 300 ./host/build/fwd_test 4000000 2>&1)
echo "$b" | grep -q "CREQ vec=b0 cmd=1c.*haltpc=bfc0d950" || { note "B) CREQ 1C 形态 FAIL"; fail=1; }
d3=$(echo "$b" | grep "^FWD vec" | sed 's/.*blocks=\([0-9]*\).*/\1/')
[ "${d3:-999999}" -lt 1000 ] && note "B) create 污染实证: 0x3d 劣化至 $d3 块" || { note "B) 未劣化 d3=$d3"; fail=1; }
c=$(env $H5 FWDLIST=/tmp/h69_3d.txt timeout 300 ./host/build/fwd_test 4000000 2>&1 | grep "^FWD vec")
echo "$c" | grep -q "blocks=207248 haltpc=000026d0" && note "C1) 五段基线逐字恒" || { note "C1) FAIL $c"; fail=1; }
FWDLIST=/tmp/h59_all.txt timeout 300 ./host/build/fwd_test 2>&1 | grep -q "FWDSUM ok=2 ctx=22" && note "C2) 全序基线恒" || { note "C2) FAIL"; fail=1; }
grep -q "H_I044SM" docs/64_wedge3_falsified.md && grep -q "负增益" docs/64_wedge3_falsified.md && grep -q "资产损失" docs/64_wedge3_falsified.md && note "D) docs/64 锚齐" || { note "D) 缺锚"; fail=1; }
d=$(ls docs/*.md | wc -l); if grep -q "64_wedge3_falsified" README.md && [ "$d" -eq 90 ]; then note "E) README 与 docs=$d 一致"; else note "E) FAIL docs=$d"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST69 CHECK PASS" || echo "HOST69 CHECK FAIL"
[ $fail -eq 0 ]
