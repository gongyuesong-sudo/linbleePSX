#!/usr/bin/env bash
# validate_host110.sh — BIOS战役楔7b CMEXP 转正(task-118, 2026-11-01)
set -u
cd /home/gill/bleem/port
fail=0; note(){ echo "HOST110: $*"; }
rm -f build/bleem_port
if cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/tmp/h110_b.log 2>&1 && [ -x build/bleem_port ]; then
  note "A) 重建 PASS"; else note "A) FAIL"; grep -i error /tmp/h110_b.log | head -2; fail=1; fi
export BLEEM_BIOS=/home/gill/bleem/psxbios/SCPH1001.BIN
ok=1; fsum=0; tot=""
for t in psx cpu dev eng; do ./build/bleem_port --$t-selftest >/tmp/h110_$t.log 2>&1; r=$?; p=$(grep -c ": PASS" /tmp/h110_$t.log); f=$(grep -c ": FAIL" /tmp/h110_$t.log); tot="$tot ${t}=${p}P/${f}F"; eval "p_$t=$p"; fsum=$((fsum+f)); [ $r -eq 0 ] || ok=0; done
grep -q "e4c_cm_write_divert_dual_state: PASS" /tmp/h110_eng.log && rb=1 || rb=0
[ $ok -eq 1 ] && [ "$fsum" = "0" ] && [ "$p_psx" -ge 31 ] && [ "$p_cpu" -ge 16 ] && [ "$p_dev" -ge 8 ] && [ "$p_eng" -ge 9 ] && [ "$rb" = 1 ] && note "B) 四面回归$tot + E4c 双态断言" || { note "B) FAIL$tot rb=$rb"; fail=1; }
# C) 默认面 = patch ON(无 JUMPZERO + cm_skip 在账 + rom 推进)
timeout 200 ./build/bleem_port --bios-run=1000000 >/tmp/h110_def.log 2>&1
jz=$(grep -c JUMPZERO /tmp/h110_def.log || true); cs=$(grep -oE "cm_skip=[0-9]+" /tmp/h110_def.log | head -1 | cut -d= -f2)
rom=$(grep -oE "rom=[0-9]+" /tmp/h110_def.log | head -1 | cut -d= -f2)
[ "$jz" = 0 ] && [ "${cs:-0}" -gt 100 ] && [ "${rom:-0}" -gt 500000 ] && note "C) 默认 ON 面: JUMPZERO=0 cm_skip=$cs rom=$rom" || { note "C) FAIL jz=$jz cs=$cs rom=$rom"; fail=1; }
# D) NOCM 显式面 = 历史取证面(80910/81896 原位)
timeout 120 env BIOSRUN_NOCM=1 ./build/bleem_port --bios-run=200000 >/tmp/h110_off.log 2>&1
grep -q "BISO_JUMPZERO step=80910" /tmp/h110_off.log && grep -q "BISO_JUMPZERO step=81896" /tmp/h110_off.log && ! grep -q cm_skip /tmp/h110_off.log && note "D) NOCM 面历史锚原位(80910+81896, 无 cm 计数)" || { note "D) FAIL NOCM 面"; fail=1; }
# E) 门链全回归(host104-109 六门)
for v in 104 105 106 107 108 109; do if timeout 550 bash /home/gill/bleem/rewrite/tools/validate_host$v.sh >/dev/null 2>&1; then note "E) host$v 回归 PASS"; else note "E) host$v FAIL"; fail=1; fi; done
export DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
if timeout 240 bash tools/final_check.sh 2>&1 | grep -q "FINAL CHECK PASS"; then note "F1) final_check PASS"; else note "F1) FAIL"; fail=1; fi
# F2) docs=100+README
cd /home/gill/bleem/rewrite
dd=$(ls docs/*.md | wc -l); grep -q "99_bios_marathon_w7b" README.md && [ "$dd" -ge 100 ] && note "F2) docs=$dd+README" || { note "F2) FAIL docs=$dd"; fail=1; }
# G) 金标准恒+ROM/golden 不入 git
e=$(sha256sum /home/gill/bleem/demo-v1_4.EXE | cut -c1-8)
g=$(sha256sum decrypted/stream_10a4c_L71cbf.bin | cut -c1-8)
romgit=$(git ls-files | grep -icE "SCPH|psxbios|demo-v1_4|demo_patched|GOLDEN_RAM_2MB\.bin$" || true)
[ "$e" = "b61ee13e" ] && [ "$g" = "98752b98" ] && [ "$romgit" = "0" ] && note "G) 金标准恒+ROM/golden 不入 git" || { note "G) FAIL $e/$g/romgit=$romgit"; fail=1; }
[ $fail -eq 0 ] && echo "HOST110 CHECK PASS" || echo "HOST110 CHECK FAIL"
[ $fail -eq 0 ]
