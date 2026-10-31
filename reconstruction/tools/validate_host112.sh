#!/usr/bin/env bash
# validate_host112.sh — BIOS战役楔8b-①前段精判楔(task-120, 2026-11-01)
# 验收 = 精判实证可复现(CDROM 正字窗零事件/golden 对表全等/主循环画像) + 零回归
set -u
cd /home/gill/bleem/port
fail=0; note(){ echo "HOST112: $*"; }
rm -f build/bleem_port
if cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/tmp/h112_b.log 2>&1 && [ -x build/bleem_port ]; then
  note "A) 重建 PASS"; else note "A) FAIL"; grep -i error /tmp/h112_b.log | head -2; fail=1; fi
export BLEEM_BIOS=/home/gill/bleem/psxbios/SCPH1001.BIN
# B) CDROM/GPU 正字窗 10M 步零事件(等待点=中断源非命令面 的机判形态)
timeout 200 env BIOSRUN_WATCH=1f801800,1f801830 ./build/bleem_port --bios-run=10000000 >/tmp/h112_w.log 2>&1
w=$(grep -c WATCH /tmp/h112_w.log || true)
[ "$w" = 0 ] && note "B) 正字窗(GPU+CDROM 0x1800-0x182F) 10M 步零事件复现" || { note "B) FAIL w=$w"; fail=1; }
# C) golden 对表全等(dispatch/handler 表三组)
timeout 120 env BIOSRUN_DUMP=/tmp/h112_d.bin ./build/bleem_port --bios-run=100000 >/dev/null 2>&1
python3 - <<'PY'
import struct,sys
g=open('/home/gill/bleem/.analysis/golden/GOLDEN_RAM_2MB.bin','rb').read()
o=open('/tmp/h112_d.bin','rb').read()
ok = (g[0x220:0x224]==o[0x220:0x224] and g[0x870:0x8A4]==o[0x870:0x8A4] and g[0x4020:0x4044]==o[0x4020:0x4044])
sys.exit(0 if ok else 1)
PY
[ $? -eq 0 ] && note "C) RAM 装载面 golden 对表全等(0x220/0x870-0x8A0/0x4020)" || { note "C) FAIL 对表"; fail=1; }
# D) 主循环画像(10M 采样 ra=0x7010 事件循环锚) + 四面回归
grep -q "ra=bfc07010" /tmp/h112_w.log || timeout 60 env BIOSRUN_TRACE=1999960,1999964 ./build/bleem_port --bios-run=1999965 2>&1 | grep -q "ra=bfc07010" && note "D) 主循环 ra 锚 0x7010 在账" || { note "D) FAIL ra 锚"; fail=1; }
unset BLEEM_BIOS; ok=1; fsum=0; tot=""
for t in psx cpu dev eng; do ./build/bleem_port --$t-selftest >/tmp/h112_$t.log 2>&1; r=$?; p=$(grep -c ": PASS" /tmp/h112_$t.log); f=$(grep -c ": FAIL" /tmp/h112_$t.log); tot="$tot ${t}=${p}P/${f}F"; eval "p_$t=$p"; fsum=$((fsum+f)); [ $r -eq 0 ] || ok=0; done
[ $ok -eq 1 ] && [ "$fsum" = "0" ] && [ "$p_psx" -ge 31 ] && [ "$p_cpu" -ge 16 ] && [ "$p_dev" -ge 8 ] && [ "$p_eng" -ge 9 ] && note "E) 四面回归$tot" || { note "E) FAIL$tot"; fail=1; }
# F) 门链 + final_check + docs/README + 金标准
if timeout 550 bash /home/gill/bleem/rewrite/tools/validate_host111.sh 2>&1 | grep -q "HOST111 CHECK PASS"; then note "F1) host111 回归 PASS"; else note "F1) FAIL"; fail=1; fi
export DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
if timeout 240 bash tools/final_check.sh 2>&1 | grep -q "FINAL CHECK PASS"; then note "F2) final_check PASS"; else note "F2) FAIL"; fail=1; fi
cd /home/gill/bleem/rewrite
dd=$(ls docs/*.md | wc -l); grep -q "101_bios_marathon_w8b1" README.md && [ "$dd" -ge 102 ] && note "F3) docs=$dd+README" || { note "F3) FAIL docs=$dd"; fail=1; }
e=$(sha256sum /home/gill/bleem/demo-v1_4.EXE | cut -c1-8)
g2=$(sha256sum decrypted/stream_10a4c_L71cbf.bin | cut -c1-8)
romgit=$(git ls-files | grep -icE "SCPH|psxbios|demo-v1_4|demo_patched|GOLDEN_RAM_2MB\.bin$" || true)
[ "$e" = "b61ee13e" ] && [ "$g2" = "98752b98" ] && [ "$romgit" = "0" ] && note "G) 金标准恒+ROM/golden 不入 git" || { note "G) FAIL $e/$g2/romgit=$romgit"; fail=1; }
[ $fail -eq 0 ] && echo "HOST112 CHECK PASS" || echo "HOST112 CHECK FAIL"
[ $fail -eq 0 ]
