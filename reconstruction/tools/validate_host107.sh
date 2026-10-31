#!/usr/bin/env bash
# validate_host107.sh — BIOS战役楔4 侦察楔(task-115, 2026-11-01)
# 验收 = 三件侦察装备 EV 实证 + 漂移剧本关键点复现 + 零语义回归 + 文档在账
set -u
cd /home/gill/bleem/port
fail=0; note(){ echo "HOST107: $*"; }
rm -f build/bleem_port
if cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/tmp/h107_b.log 2>&1 && [ -x build/bleem_port ]; then
  note "A) 重建 PASS"; else note "A) FAIL"; grep -i error /tmp/h107_b.log | head -2; fail=1; fi
export BLEEM_BIOS=/home/gill/bleem/psxbios/SCPH1001.BIN
unset BLEEM_BIOS; ok=1; fsum=0; tot=""
for t in psx cpu dev eng; do ./build/bleem_port --$t-selftest >/tmp/h107_$t.log 2>&1; r=$?; p=$(grep -c ": PASS" /tmp/h107_$t.log); f=$(grep -c ": FAIL" /tmp/h107_$t.log); tot="$tot ${t}=${p}P/${f}F"; eval "p_$t=$p"; fsum=$((fsum+f)); [ $r -eq 0 ] || ok=0; done
[ $ok -eq 1 ] && [ "$fsum" = "0" ] && [ "$p_psx" -ge 31 ] && [ "$p_cpu" -ge 14 ] && [ "$p_dev" -ge 8 ] && [ "$p_eng" -ge 8 ] && note "B) 四面回归$tot(阈值化)" || { note "B) FAIL$tot"; fail=1; }
# C) JUMPZERO 现场复现
timeout 200 env BLEEM_BIOS=/home/gill/bleem/psxbios/SCPH1001.BIN ./build/bleem_port --bios-run=200000 >/tmp/h107_jz.log 2>&1
grep -q "BISO_JUMPZERO step=80910 from=bfc0d9a4 insn=01400008 -> tgt=b0 ra=bfc06868" /tmp/h107_jz.log && c1=1 || c1=0
grep -q "BISO_JUMPZERO step=81896" /tmp/h107_jz.log && c2=1 || c2=0
[ "$c1" = 1 ] && [ "$c2" = 1 ] && note "C) A(24) 漂移现场两位复现(step 80910/81896)" || { note "C) FAIL c1=$c1 c2=$c2"; fail=1; }
# D) WATCH 生命周期三点 + TR 逐词
timeout 200 env BLEEM_BIOS=/home/gill/bleem/psxbios/SCPH1001.BIN BIOSRUN_WATCH=b0,b4 ./build/bleem_port --bios-run=90000 >/tmp/h107_w.log 2>&1
grep -q "pc=bfc042c4 -> gpa=b0 post=3c080000 step=79238" /tmp/h107_w.log && d1=1 || d1=0
grep -q "pc=bfc0196c -> gpa=b0 post=0 step=79491" /tmp/h107_w.log && d2=1 || d2=0
timeout 200 env BLEEM_BIOS=/home/gill/bleem/psxbios/SCPH1001.BIN BIOSRUN_TRACE=79238,79243 ./build/bleem_port --bios-run=79300 >/tmp/h107_t.log 2>&1
grep -q "TR 79243 pc=bfc042c4 insn=acc7fffc a0=a0000528 a2=b8 a3=250805e0" /tmp/h107_t.log && d3=1 || d3=0
[ "$d1" = 1 ] && [ "$d2" = 1 ] && [ "$d3" = 1 ] && note "D) stub 生命周期(装载 79238/抹除 79491)+TR 逐词正确 实证" || { note "D) FAIL d1=$d1 d2=$d2 d3=$d3"; fail=1; }
# E) 楔3 门回归 + final_check
if timeout 300 bash /home/gill/bleem/rewrite/tools/validate_host106.sh 2>&1 | grep -q "HOST106 CHECK PASS"; then note "E1) host106 回归 PASS"; else note "E1) FAIL"; fail=1; fi
export DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
if timeout 240 bash tools/final_check.sh 2>&1 | grep -q "FINAL CHECK PASS"; then note "E2) final_check PASS"; else note "E2) FAIL"; fail=1; fi
# F) docs/96+README+判读器在账
cd /home/gill/bleem/rewrite
dd=$(ls docs/*.md | wc -l); grep -q "96_bios_marathon_w4" README.md && [ "$dd" -ge 97 ] && note "F1) docs=$dd+README" || { note "F1) FAIL docs=$dd"; fail=1; }
[ -x tools/psx_rom_dis.py ] || chmod +x tools/psx_rom_dis.py
python3 tools/psx_rom_dis.py 0xBFC042A0 3 | grep -q "jal\|lui" && note "F2) 判读器在账可用" || { note "F2) FAIL 判读器"; fail=1; }
# G) 金标准恒 + ROM/golden/golden 派生不入 git
e=$(sha256sum /home/gill/bleem/demo-v1_4.EXE | cut -c1-8)
g=$(sha256sum decrypted/stream_10a4c_L71cbf.bin | cut -c1-8)
romgit=$(git ls-files | grep -icE "SCPH|psxbios|demo-v1_4|demo_patched|GOLDEN_RAM_2MB\.bin$" || true)
[ "$e" = "b61ee13e" ] && [ "$g" = "98752b98" ] && [ "$romgit" = "0" ] && note "G) 金标准恒+ROM/golden 不入 git" || { note "G) FAIL $e/$g/romgit=$romgit"; fail=1; }
[ $fail -eq 0 ] && echo "HOST107 CHECK PASS" || echo "HOST107 CHECK FAIL"
[ $fail -eq 0 ]
