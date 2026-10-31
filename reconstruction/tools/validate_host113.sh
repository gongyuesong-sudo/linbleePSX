#!/usr/bin/env bash
# validate_host113.sh — BIOS战役楔8b-① 实现楔(task-121, 2026-11-01): CDROM 面 + 等待点三判
set -u
cd /home/gill/bleem/port
fail=0; note(){ echo "HOST113: $*"; }
rm -f build/bleem_port
if cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/tmp/h113_b.log 2>&1 && [ -x build/bleem_port ]; then
  note "A) 重建 PASS"; else note "A) FAIL"; grep -i error /tmp/h113_b.log | head -2; fail=1; fi
export BLEEM_BIOS=/home/gill/bleem/psxbios/SCPH1001.BIN
ok=1; fsum=0; tot=""
for t in psx cpu dev eng; do ./build/bleem_port --$t-selftest >/tmp/h113_$t.log 2>&1; r=$?; p=$(grep -c ": PASS" /tmp/h113_$t.log); f=$(grep -c ": FAIL" /tmp/h113_$t.log); tot="$tot ${t}=${p}P/${f}F"; eval "p_$t=$p"; fsum=$((fsum+f)); [ $r -eq 0 ] || ok=0; done
grep -q "cdrom_poweron_ack_bit2: PASS" /tmp/h113_dev.log && grep -q "cdrom_getstat_nodisc_F1: PASS" /tmp/h113_dev.log && rb=1 || rb=0
[ $ok -eq 1 ] && [ "$fsum" = "0" ] && [ "$p_psx" -ge 31 ] && [ "$p_cpu" -ge 16 ] && [ "$p_dev" -ge 10 ] && [ "$p_eng" -ge 9 ] && [ "$rb" = 1 ] && note "B) 四面回归$tot + CDROM 双断言" || { note "B) FAIL$tot rb=$rb"; fail=1; }
# C) 三判证据复现①: 主序 I_MASK=0 关中断写(step 80959 现场)
timeout 120 env BIOSRUN_WATCH=1f801070,1f801078 ./build/bleem_port --bios-run=200000 >/tmp/h113_m.log 2>&1
grep -q "pc=bfc06894 -> gpa=1f801074 post=0" /tmp/h113_m.log && c1=1 || c1=0
# C) 三判证据复现②: 195 步周期 = 全字清零环 a0=0x80030000 现场(2696413 窗)
timeout 200 env BIOSRUN_TRACE=2696411,2696415 ./build/bleem_port --bios-run=2696416 >/tmp/h113_c.log 2>&1
grep -q "pc=bfc019b0 insn=ad400000 a0=80030000" /tmp/h113_c.log && c2=1 || c2=0
[ "$c1" = 1 ] && [ "$c2" = 1 ] && note "C) 三判双证据复现(I_MASK=0 写 + 清零环 0x80030000 现场)" || { note "C) FAIL c1=$c1 c2=$c2"; fail=1; }
# D) CDROM 面在位不改 idle 行为(rom 段健康保持)
timeout 200 env BIOSRUN_MMIOTRACE=1 ./build/bleem_port --bios-run=2000000 >/tmp/h113_h.log 2>&1
rom=$(grep -oE "rom=[0-9]+" /tmp/h113_h.log | head -1 | cut -d= -f2)
jz=$(grep -c JUMPZERO /tmp/h113_h.log || true)
[ "${rom:-0}" -gt 1500000 ] && [ "$jz" = 0 ] && note "D) idle 健康保持 rom=$rom(实现面无回归)" || { note "D) FAIL rom=$rom jz=$jz"; fail=1; }
# E) 门链 + final_check
if timeout 550 bash /home/gill/bleem/rewrite/tools/validate_host112.sh 2>&1 | grep -q "HOST112 CHECK PASS"; then note "E1) host112 回归 PASS"; else note "E1) FAIL"; fail=1; fi
export DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
if timeout 240 bash tools/final_check.sh 2>&1 | grep -q "FINAL CHECK PASS"; then note "E2) final_check PASS"; else note "E2) FAIL"; fail=1; fi
cd /home/gill/bleem/rewrite
dd=$(ls docs/*.md | wc -l); grep -q "102_bios_marathon_w8b1impl" README.md && [ "$dd" -ge 103 ] && note "F1) docs=$dd+README" || { note "F1) FAIL docs=$dd"; fail=1; }
e=$(sha256sum /home/gill/bleem/demo-v1_4.EXE | cut -c1-8)
g2=$(sha256sum decrypted/stream_10a4c_L71cbf.bin | cut -c1-8)
romgit=$(git ls-files | grep -icE "SCPH|psxbios|demo-v1_4|demo_patched|GOLDEN_RAM_2MB\.bin$" || true)
[ "$e" = "b61ee13e" ] && [ "$g2" = "98752b98" ] && [ "$romgit" = "0" ] && note "G) 金标准恒+ROM/golden 不入 git" || { note "G) FAIL $e/$g2/romgit=$romgit"; fail=1; }
[ $fail -eq 0 ] && echo "HOST113 CHECK PASS" || echo "HOST113 CHECK FAIL"
[ $fail -eq 0 ]
