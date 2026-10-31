#!/usr/bin/env bash
# validate_host101.sh — PE楔3 MIPS 解释核(task-109, 2026-11-01)
# A) port 重建双检 B) --cpu-selftest 无ROM 14 PASS rc=0
# C) 带ROM: 14 PASS + p8 真 BIOS 首指令断言 + EV 实证
# D) --psx-selftest 31 断言楔1/2 回归 E) host100+host98 门回归+final_check
# F) docs=91+README G) 金标准恒+ROM 不入 git
set -u
cd /home/gill/bleem/port
fail=0; note(){ echo "HOST101: $*"; }
rm -f build/bleem_port
if cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/tmp/h101_b.log 2>&1 && [ -x build/bleem_port ]; then
  note "A) 重建 PASS(清产物+rc 双检)"; else note "A) FAIL"; grep -i error /tmp/h101_b.log | head -2; fail=1; fi
unset BLEEM_BIOS
./build/bleem_port --cpu-selftest >/tmp/h101_a.log 2>&1; ra=$?
na=$(grep -c ": PASS" /tmp/h101_a.log); fa=$(grep -c ": FAIL" /tmp/h101_a.log)
[ $ra -eq 0 ] && [ "$na" -ge 14 ] && [ "$fa" = 0 ] && note "B) 无ROM cpu-selftest $na/14 PASS rc=0" || { note "B) FAIL rc=$ra p=$na f=$fa"; fail=1; }
BLEEM_BIOS=/home/gill/bleem/psxbios/SCPH1001.BIN ./build/bleem_port --cpu-selftest >/tmp/h101_b2.log 2>&1; rb=$?
nb=$(grep -c ": PASS" /tmp/h101_b2.log)
grep -q "p8_bios_first_insn_exec: PASS" /tmp/h101_b2.log && p8=1 || p8=0
grep -q "bios_insn0=3c080013 pc=bfc00000 npc=bfc00004" /tmp/h101_b2.log && ev=1 || ev=0
[ $rb -eq 0 ] && [ "$nb" -ge 14 ] && [ "$p8" = 1 ] && [ "$ev" = 1 ] && note "C) 带ROM $nb/14 + 真 BIOS 首指令 lui t0,0x13 执行实证" || { note "C) FAIL rc=$rb p=$nb p8=$p8 ev=$ev"; fail=1; }
./build/bleem_port --psx-selftest >/tmp/h101_d.log 2>&1; rd=$?
nd=$(grep -c ": PASS" /tmp/h101_d.log); fd=$(grep -c ": FAIL" /tmp/h101_d.log)
[ $rd -eq 0 ] && [ "$nd" -ge 31 ] && [ "$fd" = 0 ] && note "D) 楔1/2 selftest $nd/31 回归 PASS" || { note "D) FAIL rc=$rd p=$nd f=$fd"; fail=1; }
if timeout 300 bash /home/gill/bleem/rewrite/tools/validate_host100.sh 2>&1 | grep -q "HOST100 CHECK PASS"; then note "E1) host100 楔2 门回归 PASS"; else note "E1) FAIL"; fail=1; fi
export DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
if timeout 240 bash tools/final_check.sh 2>&1 | grep -q "FINAL CHECK PASS"; then note "E2) final_check 显示面回归 PASS"; else note "E2) FAIL"; fail=1; fi
cd /home/gill/bleem/rewrite
dd=$(ls docs/*.md | wc -l); grep -q "90_port_thin_engine_w3" README.md && [ "$dd" -eq 91 ] && note "F) docs=$dd+README 索引" || { note "F) FAIL docs=$dd"; fail=1; }
e=$(sha256sum /home/gill/bleem/demo-v1_4.EXE | cut -c1-8)
g=$(sha256sum decrypted/stream_10a4c_L71cbf.bin | cut -c1-8)
romgit=$(git ls-files | grep -icE "SCPH|psxbios|demo-v1_4|demo_patched" || true)
[ "$e" = "b61ee13e" ] && [ "$g" = "98752b98" ] && [ "$romgit" = "0" ] && note "G) 金标准恒+ROM 不入 git" || { note "G) FAIL $e/$g/romgit=$romgit"; fail=1; }
[ $fail -eq 0 ] && echo "HOST101 CHECK PASS" || echo "HOST101 CHECK FAIL"
[ $fail -eq 0 ]
