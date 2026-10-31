#!/usr/bin/env bash
# validate_host100.sh — PE楔2 fixed 原址三窗+BIOS 装载(task-108, 2026-11-01)
# A) port 重建双检 B) selftest 无ROM 31 PASS rc=0(HLE+硬失败三态)
# C) selftest 带真ROM: 31 PASS + sha 登记=71af94d1 + 装载面断言在
# D) 原址三窗 EV 实证(ram=0x80000000/bios=0xbfc00000/mmio=0x1f800000)
# E) 楔1 门 host98 回归 + final_check F) docs=90+README G) 三金标准恒+ROM 不入 git
set -u
cd /home/gill/bleem/port
fail=0; note(){ echo "HOST100: $*"; }
rm -f build/bleem_port
if cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/tmp/h100_b.log 2>&1 && [ -x build/bleem_port ]; then
  note "A) 重建 PASS"; else note "A) FAIL"; grep -i error /tmp/h100_b.log | head -2; fail=1; fi
unset BLEEM_BIOS
./build/bleem_port --psx-selftest >/tmp/h100_a.log 2>&1; ra=$?
na=$(grep -c ": PASS" /tmp/h100_a.log); fa=$(grep -c ": FAIL" /tmp/h100_a.log)
[ $ra -eq 0 ] && [ "$na" -ge 31 ] && [ "$fa" = 0 ] && note "B) 无ROM 态 $na/31 PASS rc=0(HLE+missing/-10+badsize/-11 三态)" || { note "B) FAIL rc=$ra p=$na f=$fa"; fail=1; }
BLEEM_BIOS=/home/gill/bleem/psxbios/SCPH1001.BIN ./build/bleem_port --psx-selftest >/tmp/h100_b2.log 2>&1; rb=$?
nb=$(grep -c ": PASS" /tmp/h100_b2.log)
grep -q "bios_sha=71af94d1e47a68c11e8fdb9f8368040601514a42a5a399cda48c7d3bff1e99d3" /tmp/h100_b2.log && sg=1 || sg=0
grep -q "bios_load_sha_register_window: PASS" /tmp/h100_b2.log && lw=1 || lw=0
[ $rb -eq 0 ] && [ "$nb" -ge 31 ] && [ "$sg" = 1 ] && [ "$lw" = 1 ] && note "C) 带ROM 态 $nb/31 + sha 登记逐字符等 + 装载面 PASS" || { note "C) FAIL rc=$rb p=$nb sg=$sg lw=$lw"; fail=1; }
grep -q "fixed_init rc=0 ram=0x80000000 bios=0xbfc00000 mmio=0x1f800000" /tmp/h100_a.log && note "D) 原址三窗实证(恒等)" || { note "D) FAIL"; fail=1; }
if timeout 300 bash /home/gill/bleem/rewrite/tools/validate_host98.sh 2>&1 | grep -q "HOST98 CHECK PASS"; then note "E1) host98 楔1 门回归 PASS"; else note "E1) FAIL"; fail=1; fi
export DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
if timeout 240 bash tools/final_check.sh 2>&1 | grep -q "FINAL CHECK PASS"; then note "E2) final_check 显示面回归 PASS"; else note "E2) FAIL"; fail=1; fi
cd /home/gill/bleem/rewrite
dd=$(ls docs/*.md | wc -l); grep -q "89_port_thin_engine_w2" README.md && [ "$dd" -eq 90 ] && note "F) docs=$dd+README 索引" || { note "F) FAIL docs=$dd"; fail=1; }
e=$(sha256sum /home/gill/bleem/demo-v1_4.EXE | cut -c1-8)
g=$(sha256sum decrypted/stream_10a4c_L71cbf.bin | cut -c1-8)
romgit=$(git ls-files | grep -icE "SCPH|psxbios|demo-v1_4|demo_patched" || true)
[ "$e" = "b61ee13e" ] && [ "$g" = "98752b98" ] && [ "$romgit" = "0" ] && note "G) 金标准恒+ROM 字节不入 git" || { note "G) FAIL $e/$g/romgit=$romgit"; fail=1; }
[ $fail -eq 0 ] && echo "HOST100 CHECK PASS" || echo "HOST100 CHECK FAIL"
[ $fail -eq 0 ]
