#!/usr/bin/env bash
# validate_host98.sh — PE楔1 port 薄引擎骨架(task-106, 2026-10-31)
# A) port 全量重建(清产物+rc 双检) B) --psx-selftest 17 断言 PASS
# C) 施工图常数锚(0x328400/0x9400/0xBFC00000/0x1F800000/GPU+0x400) D) port final_check 回归
# E) docs=89+README F) 金标准三流 sha 恒+EXE 只读
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST98: $*"; }
cd /home/gill/bleem/port
rm -f build/bleem_port
if cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/tmp/h98_build.log 2>&1; then
  [ -x build/bleem_port ] && note "A) port 重建 PASS(清产物+rc 双检)" || { note "A) FAIL 无产物"; fail=1; }
else note "A) FAIL 编译"; grep -i error /tmp/h98_build.log | head -2; fail=1; fi
s=$(./build/bleem_port --psx-selftest 2>&1); rc=$?
n=$(echo "$s" | grep -c "^PSX_TEST.*PASS"); nf=$(echo "$s" | grep -c "^PSX_TEST.*FAIL")
[ $rc -eq 0 ] && [ "$n" -ge 17 ] && [ "$nf" = "0" ] && note "B) psx-selftest $n/17 PASS rc=0" || { note "B) FAIL rc=$rc pass=$n fail=$nf"; fail=1; }
c=0; for pat in "0x328400" "0x9400" "0xBFC00000" "0x1F800000" "0x400" "MAP_32BIT"; do grep -qE "$pat" src/psx_core.h src/psx_core.c && c=$((c+1)); done
[ "$c" -ge 6 ] && note "C) 施工图常数六锚全在(docs/82/81 值面)" || { note "C) FAIL c=$c"; fail=1; }
export DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
if timeout 240 bash tools/final_check.sh 2>&1 | grep -q "FINAL CHECK PASS"; then note "D) port final_check 回归 PASS(显示/解包面未破)"; else note "D) FAIL"; fail=1; fi
cd /home/gill/bleem/rewrite
dd=$(ls docs/*.md | wc -l); if grep -q "88_port_thin_engine_w1" README.md && [ "$dd" -eq 90 ]; then note "E) docs=$dd+README 索引"; else note "E) FAIL docs=$dd"; fail=1; fi
g=$(sha256sum decrypted/stream_10a4c_L71cbf.bin | cut -c1-8); e=$(sha256sum /home/gill/bleem/demo-v1_4.EXE | cut -c1-8)
[ "$g" = "98752b98" ] && [ "$e" = "b61ee13e" ] && note "F) s3 金标准+EXE 恒" || { note "F) FAIL $g/$e"; fail=1; }
[ $fail -eq 0 ] && echo "HOST98 CHECK PASS" || echo "HOST98 CHECK FAIL"
[ $fail -eq 0 ]
