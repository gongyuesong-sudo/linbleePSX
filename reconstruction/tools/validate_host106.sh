#!/usr/bin/env bash
# validate_host106.sh — BIOS战役楔3 诊断楔(task-114, 2026-11-01)
# A) 重建双检 B) 四面 selftest 31/14/8/8 回归(含 e4b RAM_SIZE 回读)
# C) bios-run DEV_TR 真数(SPU 0x2040>0 + RAM_SIZE 0x1060>0) + CDROM 0x1C00 访问=0 负结果断言
# D) 三工具 EV 面(golden_vectors/cold_seed/dump) E) host105 门回归+final_check
# F) docs=96+README(alias 回滚无残留) G) 金标准恒+ROM/golden 不入 git
set -u
cd /home/gill/bleem/port
fail=0; note(){ echo "HOST106: $*"; }
rm -f build/bleem_port
if cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/tmp/h106_b.log 2>&1 && [ -x build/bleem_port ]; then
  note "A) 重建 PASS"; else note "A) FAIL"; grep -i error /tmp/h106_b.log | head -2; fail=1; fi
unset BLEEM_BIOS; ok=1; tot=""; fsum=0
for t in psx cpu dev eng; do ./build/bleem_port --$t-selftest >/tmp/h106_$t.log 2>&1; r=$?; p=$(grep -c ": PASS" /tmp/h106_$t.log); f=$(grep -c ": FAIL" /tmp/h106_$t.log); tot="$tot ${t}=${p}P/${f}F"; eval "p_$t=$p"; fsum=$((fsum+f)); [ $r -eq 0 ] || ok=0; done
grep -q "e4b_ram_size_writeback: PASS" /tmp/h106_eng.log && rb=1 || rb=0
[ $ok -eq 1 ] && [ "$fsum" = "0" ] && [ "$p_psx" -ge 31 ] && [ "$p_cpu" -ge 14 ] && [ "$p_dev" -ge 8 ] && [ "$p_eng" -ge 8 ] && [ "$rb" = 1 ] && note "B) 四面回归$tot + e4b RAM_SIZE" || { note "B) FAIL$tot rb=$rb"; fail=1; }
export BLEEM_BIOS=/home/gill/bleem/psxbios/SCPH1001.BIN
timeout 200 env BIOSRUN_NOCM=1 BIOSRUN_MMIOTRACE=1 ./build/bleem_port --bios-run=200000 >/tmp/h106_tr.log 2>&1
grep -q "DEV_TR W off=0x2040" /tmp/h106_tr.log && spu=1 || spu=0
grep -q "DEV_TR W off=0x1060" /tmp/h106_tr.log && rs=1 || rs=0
cdrom=$(grep -c "off=0x1c0" /tmp/h106_tr.log || true)
[ "$spu" = 1 ] && [ "$rs" = 1 ] && [ "$cdrom" = 0 ] && note "C) DEV_TR 真数(SPU+RAM_SIZE 在账) + CDROM 访问=0 负结果成立" || { note "C) FAIL spu=$spu rs=$rs cdrom=$cdrom"; fail=1; }
env BIOSRUN_GOLDEN=/home/gill/bleem/.analysis/golden/GOLDEN_RAM_2MB.bin ./build/bleem_port --bios-run=1000 2>&1 | grep -q "golden_vectors=53" && g1=1 || g1=0
env BIOSRUN_COLD=/home/gill/bleem/.analysis/golden/GOLDEN_RAM_2MB.bin ./build/bleem_port --bios-run=1000 2>&1 | grep -q "cold_seed_words=112" && g2=1 || g2=0
[ "$g1" = 1 ] && [ "$g2" = 1 ] && note "D) 金标表 53+冷种子 112 工具面 EV 实证" || { note "D) FAIL g1=$g1 g2=$g2"; fail=1; }
if timeout 300 bash /home/gill/bleem/rewrite/tools/validate_host105.sh 2>&1 | grep -q "HOST105 CHECK PASS"; then note "E1) host105 门回归 PASS"; else note "E1) FAIL"; fail=1; fi
export DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
if timeout 240 bash tools/final_check.sh 2>&1 | grep -q "FINAL CHECK PASS"; then note "E2) final_check PASS"; else note "E2) FAIL"; fail=1; fi
cd /home/gill/bleem/rewrite
grep -q "楔6d 真值" /home/gill/bleem/port/src/psx_core.c && { note "F1) FAIL alias 回滚残留"; fail=1; } || note "F1) alias 回滚干净"
dd=$(ls docs/*.md | wc -l); grep -q "95_bios_marathon_w3" README.md && [ "$dd" -eq 108 ] && note "F2) docs=$dd+README" || { note "F2) FAIL docs=$dd"; fail=1; }
e=$(sha256sum /home/gill/bleem/demo-v1_4.EXE | cut -c1-8)
g=$(sha256sum decrypted/stream_10a4c_L71cbf.bin | cut -c1-8)
romgit=$(git ls-files | grep -icE "SCPH|psxbios|demo-v1_4|demo_patched|GOLDEN_RAM_2MB\.bin$" || true)
[ "$e" = "b61ee13e" ] && [ "$g" = "98752b98" ] && [ "$romgit" = "0" ] && note "G) 金标准恒+ROM/golden 不入 git" || { note "G) FAIL $e/$g/romgit=$romgit"; fail=1; }
[ $fail -eq 0 ] && echo "HOST106 CHECK PASS" || echo "HOST106 CHECK FAIL"
[ $fail -eq 0 ]
