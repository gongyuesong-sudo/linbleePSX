#!/usr/bin/env bash
# validate_host108.sh — BIOS战役楔5(task-116, 2026-11-01): REGIMM 修 + CMEXP 实验 + 主循环会聚
set -u
cd /home/gill/bleem/port
fail=0; note(){ echo "HOST108: $*"; }
rm -f build/bleem_port
if cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/tmp/h108_b.log 2>&1 && [ -x build/bleem_port ]; then
  note "A) 重建 PASS"; else note "A) FAIL"; grep -i error /tmp/h108_b.log | head -2; fail=1; fi
unset BLEEM_BIOS; ok=1; fsum=0; tot=""
for t in psx cpu dev eng; do ./build/bleem_port --$t-selftest >/tmp/h108_$t.log 2>&1; r=$?; p=$(grep -c ": PASS" /tmp/h108_$t.log); f=$(grep -c ": FAIL" /tmp/h108_$t.log); tot="$tot ${t}=${p}P/${f}F"; eval "p_$t=$p"; fsum=$((fsum+f)); [ $r -eq 0 ] || ok=0; done
grep -q "p7b_bltz_operand_in_rs: PASS" /tmp/h108_cpu.log && grep -q "p7b_bltzal_ra_link: PASS" /tmp/h108_cpu.log && rb=1 || rb=0
[ $ok -eq 1 ] && [ "$fsum" = "0" ] && [ "$p_psx" -ge 31 ] && [ "$p_cpu" -ge 16 ] && [ "$p_dev" -ge 8 ] && [ "$p_eng" -ge 8 ] && [ "$rb" = 1 ] && note "B) 四面回归$tot + P7b REGIMM 断言" || { note "B) FAIL$tot rb=$rb"; fail=1; }
# C) CMEXP 实验证据(1M 步): cm_skip>0 + 无 JUMPZERO + rom 段 >500k
export BLEEM_BIOS=/home/gill/bleem/psxbios/SCPH1001.BIN
timeout 200 env BIOSRUN_CMEXP=1 ./build/bleem_port --bios-run=1000000 >/tmp/h108_cm.log 2>&1
cs=$(grep -o "cm_skip=[0-9]*" /tmp/h108_cm.log | cut -d= -f2)
jz=$(grep -c JUMPZERO /tmp/h108_cm.log || true)
rom=$(grep -o "rom=[0-9]*" /tmp/h108_cm.log | cut -d= -f2)
[ "${cs:-0}" -gt 100 ] && [ "$jz" = 0 ] && [ "${rom:-0}" -gt 500000 ] && note "C) CMEXP 实证 cm_skip=$cs JUMPZERO=0 rom=$rom(旧态 74089)" || { note "C) FAIL cs=$cs jz=$jz rom=$rom"; fail=1; }
# D) 默认态零回归(不带开关, 旧漂移面保持)
timeout 120 ./build/bleem_port --bios-run=200000 >/tmp/h108_def.log 2>&1
grep -q "BISO_JUMPZERO step=80910" /tmp/h108_def.log && grep -q "rom=74089" /tmp/h108_def.log && note "D) 默认 OFF 面零回归(80910/74089 原位)" || { note "D) FAIL 默认面漂移"; fail=1; }
# E) 门链回归
if timeout 300 bash /home/gill/bleem/rewrite/tools/validate_host107.sh 2>&1 | grep -q "HOST107 CHECK PASS"; then note "E1) host107 回归 PASS"; else note "E1) FAIL"; fail=1; fi
export DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
if timeout 240 bash tools/final_check.sh 2>&1 | grep -q "FINAL CHECK PASS"; then note "E2) final_check PASS"; else note "E2) FAIL"; fail=1; fi
# F) docs=98 + README + 判读器/探针在账
cd /home/gill/bleem/rewrite
dd=$(ls docs/*.md | wc -l); grep -q "97_bios_marathon_w5" README.md && [ "$dd" -ge 98 ] && note "F) docs=$dd+README" || { note "F) FAIL docs=$dd"; fail=1; }
# G) 金标准恒 + ROM/golden 不入 git
e=$(sha256sum /home/gill/bleem/demo-v1_4.EXE | cut -c1-8)
g=$(sha256sum decrypted/stream_10a4c_L71cbf.bin | cut -c1-8)
romgit=$(git ls-files | grep -icE "SCPH|psxbios|demo-v1_4|demo_patched|GOLDEN_RAM_2MB\.bin$" || true)
[ "$e" = "b61ee13e" ] && [ "$g" = "98752b98" ] && [ "$romgit" = "0" ] && note "G) 金标准恒+ROM/golden 不入 git" || { note "G) FAIL $e/$g/romgit=$romgit"; fail=1; }
[ $fail -eq 0 ] && echo "HOST108 CHECK PASS" || echo "HOST108 CHECK FAIL"
[ $fail -eq 0 ]
