#!/usr/bin/env bash
# validate_host105.sh — BIOS战役楔2 勘正+侦察升级(task-113, 2026-11-01)
# A) 重建双检 B) 四面 selftest 回归 C) --bios-run 真跑: BISO_SEG 分段实数+LOST 侦测在
# D) 勘正文档锚(楔3 钉 CDROM 0x1F801C00 系+反指标方法论) E) host104 门回归+final_check
# F) docs=95+README G) 金标准恒+ROM 不入 git
set -u
cd /home/gill/bleem/port
fail=0; note(){ echo "HOST105: $*"; }
rm -f build/bleem_port
if cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/tmp/h105_b.log 2>&1 && [ -x build/bleem_port ]; then
  note "A) 重建 PASS"; else note "A) FAIL"; grep -i error /tmp/h105_b.log | head -2; fail=1; fi
unset BLEEM_BIOS; ok=1; tot=""
fsum=0
fsum=0
for t in psx cpu dev eng; do ./build/bleem_port --$t-selftest >/tmp/h105_$t.log 2>&1; r=$?; p=$(grep -c ": PASS" /tmp/h105_$t.log); f=$(grep -c ": FAIL" /tmp/h105_$t.log); tot="$tot ${t}=${p}P/${f}F"; eval "p_$t=$p"; fsum=$((fsum+f)); [ $r -eq 0 ] || ok=0; done
[ $ok -eq 1 ] && [ "$fsum" = "0" ] && [ "$p_psx" -ge 31 ] && [ "$p_cpu" -ge 14 ] && [ "$p_dev" -ge 8 ] && [ "$p_eng" -ge 7 ] && note "B) 四面回归$tot(阈值化)" || { note "B) FAIL$tot"; fail=1; }
export BLEEM_BIOS=/home/gill/bleem/psxbios/SCPH1001.BIN
timeout 200 env BIOSRUN_NOCM=1 ./build/bleem_port --bios-run=300000 >/tmp/h105_run.log 2>&1; rr=$?
rom=$(grep -oP 'BISO_SEG rom=\K[0-9]+' /tmp/h105_run.log || echo 0)
lo=$(grep -oP ' ramlo=\K[0-9]+' /tmp/h105_run.log || echo 0)
nlost=$(grep -c BISO_LOST /tmp/h105_run.log || true)
[ $rr -eq 0 ] && [ "$rom" -gt 50000 ] && [ "$lo" -gt 100000 ] && [ "$nlost" -ge 1 ] \
  && note "C) 真跑 300k: rom=$rom ramlo=$lo LOST=$nlost(无盘 nop 漂流实数面)" || { note "C) FAIL rc=$rr rom=$rom lo=$lo lost=$nlost"; fail=1; }
cd /home/gill/bleem/rewrite
grep -q "0x1F801C00" docs/94_bios_marathon_w2.md && grep -q "零异常.*≠.*活着" docs/94_bios_marathon_w2.md && note "D) 勘正锚在(CDROM 0x1C00 系+反指标方法论)" || { note "D) FAIL"; fail=1; }
if timeout 300 bash tools/validate_host104.sh 2>&1 | grep -q "HOST104 CHECK PASS"; then note "E1) host104 楔1 门回归 PASS"; else note "E1) FAIL"; fail=1; fi
export DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
if timeout 240 bash /home/gill/bleem/port/tools/final_check.sh 2>&1 | grep -q "FINAL CHECK PASS"; then note "E2) final_check PASS"; else note "E2) FAIL"; fail=1; fi
dd=$(ls docs/*.md | wc -l); grep -q "94_bios_marathon_w2" README.md && [ "$dd" -eq 108 ] && note "F) docs=$dd+README" || { note "F) FAIL docs=$dd"; fail=1; }
e=$(sha256sum /home/gill/bleem/demo-v1_4.EXE | cut -c1-8)
g=$(sha256sum decrypted/stream_10a4c_L71cbf.bin | cut -c1-8)
romgit=$(git ls-files | grep -icE "SCPH|psxbios|demo-v1_4|demo_patched" || true)
[ "$e" = "b61ee13e" ] && [ "$g" = "98752b98" ] && [ "$romgit" = "0" ] && note "G) 金标准恒+ROM 不入 git" || { note "G) FAIL $e/$g/romgit=$romgit"; fail=1; }
[ $fail -eq 0 ] && echo "HOST105 CHECK PASS" || echo "HOST105 CHECK FAIL"
[ $fail -eq 0 ]
