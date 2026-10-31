#!/usr/bin/env bash
# validate_host111.sh — BIOS战役楔8a GPU 前置侦察(task-119, 2026-11-01)
# 验收 = 三实证可复现(GPU 端口零读写/CDROM 零读/vblank 静默面存在) + 设计书在账 + 零回归
set -u
cd /home/gill/bleem/port
fail=0; note(){ echo "HOST111: $*"; }
rm -f build/bleem_port
if cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/tmp/h111_b.log 2>&1 && [ -x build/bleem_port ]; then
  note "A) 重建 PASS"; else note "A) FAIL"; grep -i error /tmp/h111_b.log | head -2; fail=1; fi
export BLEEM_BIOS=/home/gill/bleem/psxbios/SCPH1001.BIN
timeout 220 env BIOSRUN_MMIOTRACE=1 ./build/bleem_port --bios-run=2000000 >/tmp/h111_tr.log 2>&1
g=$(grep -cE "DEV_TR (W|R) off=0x18[01]" /tmp/h111_tr.log || true)
c=$(grep -cE "DEV_TR (W|R) off=0x1(e0|c0)" /tmp/h111_tr.log || true)
n=$(grep -c DEV_TR /tmp/h111_tr.log)
[ "$g" = 0 ] && [ "$c" = 0 ] && [ "$n" -le 20 ] && note "B) 三实证复现: GPU 端口写读=0 CDROM=0 (DEV_TR 全量 $n 项)" || { note "B) FAIL g=$g c=$c n=$n"; fail=1; }
# C) 2M 健康面保持(转正默认态断言)
jz=$(grep -c JUMPZERO /tmp/h111_tr.log || true); rom=$(grep -oE "rom=[0-9]+" /tmp/h111_tr.log | head -1 | cut -d= -f2)
[ "$jz" = 0 ] && [ "${rom:-0}" -gt 1500000 ] && note "C) 默认态健康保持 rom=$rom JUMPZERO=0" || { note "C) FAIL rom=$rom jz=$jz"; fail=1; }
# D) 四面回归(阈值化)
unset BLEEM_BIOS; ok=1; fsum=0; tot=""
for t in psx cpu dev eng; do ./build/bleem_port --$t-selftest >/tmp/h111_$t.log 2>&1; r=$?; p=$(grep -c ": PASS" /tmp/h111_$t.log); f=$(grep -c ": FAIL" /tmp/h111_$t.log); tot="$tot ${t}=${p}P/${f}F"; eval "p_$t=$p"; fsum=$((fsum+f)); [ $r -eq 0 ] || ok=0; done
[ $ok -eq 1 ] && [ "$fsum" = "0" ] && [ "$p_psx" -ge 31 ] && [ "$p_cpu" -ge 16 ] && [ "$p_dev" -ge 8 ] && [ "$p_eng" -ge 9 ] && note "D) 四面回归$tot" || { note "D) FAIL$tot"; fail=1; }
# E) 门链 + final_check
if timeout 550 bash /home/gill/bleem/rewrite/tools/validate_host110.sh 2>&1 | grep -q "HOST110 CHECK PASS"; then note "E1) host110 回归 PASS"; else note "E1) FAIL"; fail=1; fi
export DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
if timeout 240 bash tools/final_check.sh 2>&1 | grep -q "FINAL CHECK PASS"; then note "E2) final_check PASS"; else note "E2) FAIL"; fail=1; fi
# F) docs=101 + README + 设计书关键内容在账
cd /home/gill/bleem/rewrite
dd=$(ls docs/*.md | wc -l); grep -q "100_bios_marathon_w8a" README.md && [ "$dd" -ge 101 ] && note "F1) docs=$dd+README" || { note "F1) FAIL docs=$dd"; fail=1; }
grep -q "CDROM 驱动器应答模型" docs/100_bios_marathon_w8a.md && grep -q "楔8b 实施清单" docs/100_bios_marathon_w8a.md && note "F2) 设计书关键章节在账" || { note "F2) FAIL 设计书"; fail=1; }
# G) 金标准恒+ROM/golden 不入 git
e=$(sha256sum /home/gill/bleem/demo-v1_4.EXE | cut -c1-8)
g2=$(sha256sum decrypted/stream_10a4c_L71cbf.bin | cut -c1-8)
romgit=$(git ls-files | grep -icE "SCPH|psxbios|demo-v1_4|demo_patched|GOLDEN_RAM_2MB\.bin$" || true)
[ "$e" = "b61ee13e" ] && [ "$g2" = "98752b98" ] && [ "$romgit" = "0" ] && note "G) 金标准恒+ROM/golden 不入 git" || { note "G) FAIL $e/$g2/romgit=$romgit"; fail=1; }
[ $fail -eq 0 ] && echo "HOST111 CHECK PASS" || echo "HOST111 CHECK FAIL"
[ $fail -eq 0 ]
