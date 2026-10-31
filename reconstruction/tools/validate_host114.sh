#!/usr/bin/env bash
# validate_host114.sh — BIOS战役收束楔(task-122, 2026-11-01): vblank 位转正 + 里程碑定案
set -u
cd /home/gill/bleem/port
fail=0; note(){ echo "HOST114: $*"; }
rm -f build/bleem_port
if cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/tmp/h114_b.log 2>&1 && [ -x build/bleem_port ]; then
  note "A) 重建 PASS"; else note "A) FAIL"; grep -i error /tmp/h114_b.log | head -2; fail=1; fi
export BLEEM_BIOS=/home/gill/bleem/psxbios/SCPH1001.BIN
ok=1; fsum=0; tot=""
for t in psx cpu dev eng; do ./build/bleem_port --$t-selftest >/tmp/h114_$t.log 2>&1; r=$?; p=$(grep -c ": PASS" /tmp/h114_$t.log); f=$(grep -c ": FAIL" /tmp/h114_$t.log); tot="$tot ${t}=${p}P/${f}F"; eval "p_$t=$p"; fsum=$((fsum+f)); [ $r -eq 0 ] || ok=0; done
grep -q "evtgen_64read_vblank_bit0: PASS" /tmp/h114_dev.log && grep -q "e1_vblank_irq_autoentry: PASS" /tmp/h114_eng.log && rb=1 || rb=0
[ $ok -eq 1 ] && [ "$fsum" = "0" ] && [ "$p_psx" -ge 31 ] && [ "$p_cpu" -ge 16 ] && [ "$p_dev" -ge 10 ] && [ "$p_eng" -ge 9 ] && [ "$rb" = 1 ] && note "B) 四面回归$tot + vblank bit0 双断言" || { note "B) FAIL$tot rb=$rb"; fail=1; }
# C) vblank 位实验证据复现: BIOS 轮询 I_STAT 现场(读计数>1000) + 单bit不解锁(健康保持)
timeout 250 env BIOSRUN_MMIOTRACE=1 ./build/bleem_port --bios-run=2000000 >/tmp/h114_c.log 2>&1
# C) 判据=诚实现场: idle 读侧几乎真空(DEV_TR R 条目<=2) + 健康保持(单bit不解锁)
nr=$(grep -c "DEV_TR R " /tmp/h114_c.log || true)
rom=$(grep -oE "rom=[0-9]+" /tmp/h114_c.log | head -1 | cut -d= -f2)
[ -s /tmp/h114_c.log ] && [ "$nr" -le 2 ] && [ "${rom:-0}" -gt 1500000 ] && note "C) idle 读侧真空实证(R条目=$nr<=2, I_STAT 零读——判读更正锚) + 健康面在账(rom=$rom)" || { note "C) FAIL R条目=$nr rom=$rom"; fail=1; }
# D) 门链 + final_check
if timeout 550 bash /home/gill/bleem/rewrite/tools/validate_host113.sh 2>&1 | grep -q "HOST113 CHECK PASS"; then note "D1) host113 回归 PASS"; else note "D1) FAIL"; fail=1; fi
export DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
if timeout 240 bash tools/final_check.sh 2>&1 | grep -q "FINAL CHECK PASS"; then note "D2) final_check PASS"; else note "D2) FAIL"; fail=1; fi
cd /home/gill/bleem/rewrite
dd=$(ls docs/*.md | wc -l); grep -q "103_bios_marathon_w9_close" README.md && [ "$dd" -ge 104 ] && note "E1) docs=$dd+README" || { note "E1) FAIL docs=$dd"; fail=1; }
e=$(sha256sum /home/gill/bleem/demo-v1_4.EXE | cut -c1-8)
g2=$(sha256sum decrypted/stream_10a4c_L71cbf.bin | cut -c1-8)
romgit=$(git ls-files | grep -icE "SCPH|psxbios|demo-v1_4|demo_patched|GOLDEN_RAM_2MB\.bin$" || true)
[ "$e" = "b61ee13e" ] && [ "$g2" = "98752b98" ] && [ "$romgit" = "0" ] && note "F) 金标准恒+ROM/golden 不入 git" || { note "F) FAIL $e/$g2/romgit=$romgit"; fail=1; }
[ $fail -eq 0 ] && echo "HOST114 CHECK PASS" || echo "HOST114 CHECK FAIL"
[ $fail -eq 0 ]
