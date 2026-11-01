#!/usr/bin/env bash
# validate_host103.sh — PE楔5 收官: 中断链+GTE 最小面+svc37+BIOS 烟测(task-111, 2026-11-01)
# A) 重建双检 B) 无ROM 四面 31/14/8/7 全 PASS C) 带ROM 四面全 PASS+e1 中断 EV+e4 烟测 EV
# D) status 真位锚(IE=1u/EXL=2u 源码面) E) host102 楔4 门回归+final_check F) docs=93+README G) 金标准恒
set -u
cd /home/gill/bleem/port
fail=0; note(){ echo "HOST103: $*"; }
rm -f build/bleem_port
if cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/tmp/h103_b.log 2>&1 && [ -x build/bleem_port ]; then
  note "A) 重建 PASS(清产物+rc 双检)"; else note "A) FAIL"; grep -i error /tmp/h103_b.log | head -2; fail=1; fi
unset BLEEM_BIOS; ok=1; tot=""
fsum=0
for t in psx cpu dev eng; do ./build/bleem_port --$t-selftest >/tmp/h103_$t.log 2>&1; r=$?; p=$(grep -c ": PASS" /tmp/h103_$t.log); f=$(grep -c ": FAIL" /tmp/h103_$t.log); tot="$tot ${t}=${p}P/${f}F"; eval "p_$t=$p"; fsum=$((fsum+f)); [ $r -eq 0 ] || ok=0; done
[ $ok -eq 1 ] && [ "$fsum" = "0" ] && [ "$p_psx" -ge 31 ] && [ "$p_cpu" -ge 14 ] && [ "$p_dev" -ge 8 ] && note "B) 无ROM 四面$tot" || { note "B) FAIL$tot"; fail=1; }
export BLEEM_BIOS=/home/gill/bleem/psxbios/SCPH1001.BIN; ok=1; tot2=""
for t in psx cpu dev eng; do ./build/bleem_port --$t-selftest >/tmp/h103_${t}_rom.log 2>&1; r=$?; p=$(grep -c ": PASS" /tmp/h103_${t}_rom.log); tot2="$tot2 ${t}=${p}"; [ $r -eq 0 ] || ok=0; done
grep -q "e1 cnt=1 cause=100" /tmp/h103_eng_rom.log && i1=1 || i1=0
grep -qE "e4 cycles=2000 pc=[0-9a-f]+ exc=[0-9]+" /tmp/h103_eng_rom.log && i4=1 || i4=0
[ $ok -eq 1 ] && [ "$i1" = 1 ] && [ "$i4" = 1 ] && note "C) 带ROM 四面$tot2 + e1 中断闭环 EV + e4 BIOS2000 步烟测 EV" || { note "C) FAIL$tot2 i1=$i1 i4=$i4"; fail=1; }
grep -q "EXL=bit1" src/psx_cpu.c && grep -q "cop0\[12\] |= 2u" src/psx_cpu.c && grep -q "irq_check" src/psx_cpu.c && note "D) status 真位修正+irq_check 源码锚在" || { note "D) FAIL"; fail=1; }
if timeout 300 bash /home/gill/bleem/rewrite/tools/validate_host102.sh 2>&1 | grep -q "HOST102 CHECK PASS"; then note "E1) host102 楔4 门回归 PASS"; else note "E1) FAIL"; fail=1; fi
export DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
if timeout 240 bash tools/final_check.sh 2>&1 | grep -q "FINAL CHECK PASS"; then note "E2) final_check 显示面回归 PASS"; else note "E2) FAIL"; fail=1; fi
cd /home/gill/bleem/rewrite
dd=$(ls docs/*.md | wc -l); grep -q "92_port_thin_engine_w5" README.md && [ "$dd" -eq 108 ] && note "F) docs=$dd+README 索引" || { note "F) FAIL docs=$dd"; fail=1; }
e=$(sha256sum /home/gill/bleem/demo-v1_4.EXE | cut -c1-8)
g=$(sha256sum decrypted/stream_10a4c_L71cbf.bin | cut -c1-8)
romgit=$(git ls-files | grep -icE "SCPH|psxbios|demo-v1_4|demo_patched" || true)
[ "$e" = "b61ee13e" ] && [ "$g" = "98752b98" ] && [ "$romgit" = "0" ] && note "G) 金标准恒+ROM 不入 git" || { note "G) FAIL $e/$g/romgit=$romgit"; fail=1; }
[ $fail -eq 0 ] && echo "HOST103 CHECK PASS" || echo "HOST103 CHECK FAIL"
[ $fail -eq 0 ]
