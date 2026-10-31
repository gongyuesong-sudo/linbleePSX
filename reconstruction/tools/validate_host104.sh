#!/usr/bin/env bash
# validate_host104.sh — BIOS长跑战役楔1(task-112, 2026-11-01)
# A) 重建双检 B) 四面 selftest 31/14/8/7 回归(无ROM)
# C) --bios-run=10000000 真跑: rc=0 + steps=10M + exc_total=0
# D) 三修真值面源码锚(RAM 折叠/rfe/GTE 命令判定) E) host103 门回归+final_check
# F) docs=94+README G) 金标准恒+ROM 不入 git
set -u
cd /home/gill/bleem/port
fail=0; note(){ echo "HOST104: $*"; }
rm -f build/bleem_port
if cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/tmp/h104_b.log 2>&1 && [ -x build/bleem_port ]; then
  note "A) 重建 PASS"; else note "A) FAIL"; grep -i error /tmp/h104_b.log | head -2; fail=1; fi
unset BLEEM_BIOS; ok=1; tot=""
fsum=0
fsum=0
for t in psx cpu dev eng; do ./build/bleem_port --$t-selftest >/tmp/h104_$t.log 2>&1; r=$?; p=$(grep -c ": PASS" /tmp/h104_$t.log); f=$(grep -c ": FAIL" /tmp/h104_$t.log); tot="$tot ${t}=${p}P/${f}F"; eval "p_$t=$p"; fsum=$((fsum+f)); [ $r -eq 0 ] || ok=0; done
[ $ok -eq 1 ] && [ "$fsum" = "0" ] && [ "$p_psx" -ge 31 ] && [ "$p_cpu" -ge 14 ] && [ "$p_dev" -ge 8 ] && [ "$p_eng" -ge 7 ] && note "B) 四面回归$tot" || { note "B) FAIL$tot"; fail=1; }
export BLEEM_BIOS=/home/gill/bleem/psxbios/SCPH1001.BIN
timeout 240 env BIOSRUN_NOCM=1 ./build/bleem_port --bios-run=10000000 >/tmp/h104_run.log 2>&1; rr=$?
grep -q "steps=10000000 cycles=10000000 exc_total=0" /tmp/h104_run.log && ex=1 || ex=0
[ $rr -eq 0 ] && [ "$ex" = 1 ] && note "C) BIOS 10M 步真跑 rc=0 零异常" || { note "C) FAIL rc=$rr ex=$ex"; tail -2 /tmp/h104_run.log; fail=1; }
grep -q "gpa & 0x1FFFFFu" src/psx_core.c && grep -q "f == 0x10" src/psx_cpu.c && grep -q "rt == 0 && rd == 0" src/psx_cpu.c && note "D) 三修真值面源码锚在" || { note "D) FAIL"; fail=1; }
if timeout 300 bash /home/gill/bleem/rewrite/tools/validate_host103.sh 2>&1 | grep -q "HOST103 CHECK PASS"; then note "E1) host103 楔5 门回归 PASS"; else note "E1) FAIL"; fail=1; fi
export DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
if timeout 240 bash tools/final_check.sh 2>&1 | grep -q "FINAL CHECK PASS"; then note "E2) final_check 显示面 PASS"; else note "E2) FAIL"; fail=1; fi
cd /home/gill/bleem/rewrite
dd=$(ls docs/*.md | wc -l); grep -q "93_bios_marathon_w1" README.md && [ "$dd" -eq 100 ] && note "F) docs=$dd+README" || { note "F) FAIL docs=$dd"; fail=1; }
e=$(sha256sum /home/gill/bleem/demo-v1_4.EXE | cut -c1-8)
g=$(sha256sum decrypted/stream_10a4c_L71cbf.bin | cut -c1-8)
romgit=$(git ls-files | grep -icE "SCPH|psxbios|demo-v1_4|demo_patched" || true)
[ "$e" = "b61ee13e" ] && [ "$g" = "98752b98" ] && [ "$romgit" = "0" ] && note "G) 金标准恒+ROM 不入 git" || { note "G) FAIL $e/$g/romgit=$romgit"; fail=1; }
[ $fail -eq 0 ] && echo "HOST104 CHECK PASS" || echo "HOST104 CHECK FAIL"
[ $fail -eq 0 ]
