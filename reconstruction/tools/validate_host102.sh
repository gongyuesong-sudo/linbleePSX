#!/usr/bin/env bash
# validate_host102.sh — PE楔4 设备层+联动+rootcall契约(task-110, 2026-11-01)
# A) 重建双检 B) 三 selftest 无ROM 31/14/8 全 PASS
# C) 带ROM 三面全 PASS(联动回归在 ROM 态) D) rootcall 契约 EV 面(源内锚)
# E) host101 楔3 门回归 + final_check F) docs=92+README G) 金标准恒+ROM 不入 git
set -u
cd /home/gill/bleem/port
fail=0; note(){ echo "HOST102: $*"; }
rm -f build/bleem_port
if cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/tmp/h102_b.log 2>&1 && [ -x build/bleem_port ]; then
  note "A) 重建 PASS(清产物+rc 双检)"; else note "A) FAIL"; grep -i error /tmp/h102_b.log | head -2; fail=1; fi
unset BLEEM_BIOS; ok=1; tot=""
fsum=0
for t in psx cpu dev; do ./build/bleem_port --$t-selftest >/tmp/h102_$t.log 2>&1; r=$?; p=$(grep -c ": PASS" /tmp/h102_$t.log); f=$(grep -c ": FAIL" /tmp/h102_$t.log); tot="$tot ${t}=${p}P/${f}F"; eval "p_$t=$p"; fsum=$((fsum+f)); [ $r -eq 0 ] || ok=0; done
[ $ok -eq 1 ] && [ "$fsum" = "0" ] && [ "$p_psx" -ge 31 ] && [ "$p_cpu" -ge 14 ] && [ "$p_dev" -ge 8 ] && note "B) 无ROM 三面$tot" || { note "B) FAIL$tot"; fail=1; }
export BLEEM_BIOS=/home/gill/bleem/psxbios/SCPH1001.BIN; ok=1; tot2=""
for t in psx cpu dev; do ./build/bleem_port --$t-selftest >/tmp/h102_${t}_rom.log 2>&1; r=$?; p=$(grep -c ": PASS" /tmp/h102_${t}_rom.log); tot2="$tot2 ${t}=${p}"; [ $r -eq 0 ] || ok=0; done
[ $ok -eq 1 ] && note "C) 带ROM 三面$tot2 全 rc=0" || { note "C) FAIL$tot2"; fail=1; }
grep -q "rootcall_600_674_contract_chain" src/psx_dev.c && grep -q "0x80000674" src/psx_dev.c && grep -q "0x80000600" src/psx_dev.c && note "D) rootcall 契约三锚在源(0x600/0x674/dispatch)" || { note "D) FAIL"; fail=1; }
if timeout 300 bash /home/gill/bleem/rewrite/tools/validate_host101.sh 2>&1 | grep -q "HOST101 CHECK PASS"; then note "E1) host101 楔3 门回归 PASS"; else note "E1) FAIL"; fail=1; fi
export DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
if timeout 240 bash tools/final_check.sh 2>&1 | grep -q "FINAL CHECK PASS"; then note "E2) final_check 显示面回归 PASS"; else note "E2) FAIL"; fail=1; fi
cd /home/gill/bleem/rewrite
dd=$(ls docs/*.md | wc -l); grep -q "91_port_thin_engine_w4" README.md && [ "$dd" -eq 97 ] && note "F) docs=$dd+README 索引" || { note "F) FAIL docs=$dd"; fail=1; }
e=$(sha256sum /home/gill/bleem/demo-v1_4.EXE | cut -c1-8)
g=$(sha256sum decrypted/stream_10a4c_L71cbf.bin | cut -c1-8)
romgit=$(git ls-files | grep -icE "SCPH|psxbios|demo-v1_4|demo_patched" || true)
[ "$e" = "b61ee13e" ] && [ "$g" = "98752b98" ] && [ "$romgit" = "0" ] && note "G) 金标准恒+ROM 不入 git" || { note "G) FAIL $e/$g/romgit=$romgit"; fail=1; }
[ $fail -eq 0 ] && echo "HOST102 CHECK PASS" || echo "HOST102 CHECK FAIL"
fsum=0
for t in psx cpu dev; do ./build/bleem_port --$t-selftest >/tmp/h102_$t.log 2>&1; r=$?; p=$(grep -c ": PASS" /tmp/h102_$t.log); f=$(grep -c ": FAIL" /tmp/h102_$t.log); tot="$tot ${t}=${p}P/${f}F"; eval "p_$t=$p"; fsum=$((fsum+f)); [ $r -eq 0 ] || ok=0; done
# validate_host102.sh — PE楔4 设备层+联动+rootcall契约(task-110, 2026-11-01)
# A) 重建双检 B) 三 selftest 无ROM 31/14/8 全 PASS
# C) 带ROM 三面全 PASS(联动回归在 ROM 态) D) rootcall 契约 EV 面(源内锚)
# E) host101 楔3 门回归 + final_check F) docs=92+README G) 金标准恒+ROM 不入 git
set -u
cd /home/gill/bleem/port
fail=0; note(){ echo "HOST102: $*"; }
rm -f build/bleem_port
if cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/tmp/h102_b.log 2>&1 && [ -x build/bleem_port ]; then
  note "A) 重建 PASS(清产物+rc 双检)"; else note "A) FAIL"; grep -i error /tmp/h102_b.log | head -2; fail=1; fi
unset BLEEM_BIOS; ok=1; tot=""
fsum=0
for t in psx cpu dev; do ./build/bleem_port --$t-selftest >/tmp/h102_$t.log 2>&1; r=$?; p=$(grep -c ": PASS" /tmp/h102_$t.log); f=$(grep -c ": FAIL" /tmp/h102_$t.log); tot="$tot ${t}=${p}P/${f}F"; eval "p_$t=$p"; fsum=$((fsum+f)); [ $r -eq 0 ] || ok=0; done
[ $ok -eq 1 ] && [ "$fsum" = "0" ] && [ "$p_psx" -ge 31 ] && [ "$p_cpu" -ge 14 ] && [ "$p_dev" -ge 8 ] && note "B) 无ROM 三面$tot" || { note "B) FAIL$tot"; fail=1; }
export BLEEM_BIOS=/home/gill/bleem/psxbios/SCPH1001.BIN; ok=1; tot2=""
for t in psx cpu dev; do ./build/bleem_port --$t-selftest >/tmp/h102_${t}_rom.log 2>&1; r=$?; p=$(grep -c ": PASS" /tmp/h102_${t}_rom.log); tot2="$tot2 ${t}=${p}"; [ $r -eq 0 ] || ok=0; done
[ $ok -eq 1 ] && note "C) 带ROM 三面$tot2 全 rc=0" || { note "C) FAIL$tot2"; fail=1; }
grep -q "rootcall_600_674_contract_chain" src/psx_dev.c && grep -q "0x80000674" src/psx_dev.c && grep -q "0x80000600" src/psx_dev.c && note "D) rootcall 契约三锚在源(0x600/0x674/dispatch)" || { note "D) FAIL"; fail=1; }
if timeout 300 bash /home/gill/bleem/rewrite/tools/validate_host101.sh 2>&1 | grep -q "HOST101 CHECK PASS"; then note "E1) host101 楔3 门回归 PASS"; else note "E1) FAIL"; fail=1; fi
export DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
if timeout 240 bash tools/final_check.sh 2>&1 | grep -q "FINAL CHECK PASS"; then note "E2) final_check 显示面回归 PASS"; else note "E2) FAIL"; fail=1; fi
cd /home/gill/bleem/rewrite
dd=$(ls docs/*.md | wc -l); grep -q "91_port_thin_engine_w4" README.md && [ "$dd" -eq 97 ] && note "F) docs=$dd+README 索引" || { note "F) FAIL docs=$dd"; fail=1; }
e=$(sha256sum /home/gill/bleem/demo-v1_4.EXE | cut -c1-8)
g=$(sha256sum decrypted/stream_10a4c_L71cbf.bin | cut -c1-8)
romgit=$(git ls-files | grep -icE "SCPH|psxbios|demo-v1_4|demo_patched" || true)
[ "$e" = "b61ee13e" ] && [ "$g" = "98752b98" ] && [ "$romgit" = "0" ] && note "G) 金标准恒+ROM 不入 git" || { note "G) FAIL $e/$g/romgit=$romgit"; fail=1; }
[ $fail -eq 0 ] && echo "HOST102 CHECK PASS" || echo "HOST102 CHECK FAIL"
[ $fail -eq 0 ]
