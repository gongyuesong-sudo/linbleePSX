#!/usr/bin/env bash
# validate_host109.sh — BIOS战役楔6a 侦察楔(task-117, 2026-11-01): 主循环全判读+三勘正断言
set -u
cd /home/gill/bleem/port
fail=0; note(){ echo "HOST109: $*"; }
rm -f build/bleem_port
if cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/tmp/h109_b.log 2>&1 && [ -x build/bleem_port ]; then
  note "A) 重建 PASS"; else note "A) FAIL"; grep -i error /tmp/h109_b.log | head -2; fail=1; fi
export BLEEM_BIOS=/home/gill/bleem/psxbios/SCPH1001.BIN
unset BLEEM_BIOS; ok=1; fsum=0; tot=""
for t in psx cpu dev eng; do ./build/bleem_port --$t-selftest >/tmp/h109_$t.log 2>&1; r=$?; p=$(grep -c ": PASS" /tmp/h109_$t.log); f=$(grep -c ": FAIL" /tmp/h109_$t.log); tot="$tot ${t}=${p}P/${f}F"; eval "p_$t=$p"; fsum=$((fsum+f)); [ $r -eq 0 ] || ok=0; done
[ $ok -eq 1 ] && [ "$fsum" = "0" ] && [ "$p_psx" -ge 31 ] && [ "$p_cpu" -ge 16 ] && [ "$p_dev" -ge 8 ] && [ "$p_eng" -ge 8 ] && note "B) 四面回归$tot(阈值化)" || { note "B) FAIL$tot"; fail=1; }
# C) 勘正断言: 0x1D80 系写 12 次在账(主序 XA 组写已执行) + 无盘命令寄存器 0x1C00 零访问合法
timeout 200 env BLEEM_BIOS=/home/gill/bleem/psxbios/SCPH1001.BIN BIOSRUN_CMEXP=1 BIOSRUN_MMIOTRACE=1 ./build/bleem_port --bios-run=2000000 >/tmp/h109_tr.log 2>&1
n80=$(grep -oE "off=0x1d80\(gpa 0x1F801d80\) x[0-9]+" /tmp/h109_tr.log | grep -oE "x[0-9]+" | tr -d x | sort -n | tail -1); n84=$(grep -oE "off=0x1d84\(gpa 0x1F801d84\) x[0-9]+" /tmp/h109_tr.log | grep -oE "x[0-9]+" | tr -d x | sort -n | tail -1)
[ "${n80:-0}" -ge 12 ] && c1=1 || c1=0; [ "${n84:-0}" -ge 12 ] && c1b=1 || c1b=0
cdrom=$(grep -c "off=0x1c0" /tmp/h109_tr.log || true)
[ "$c1" = 1 ] && [ "$c1b" = 1 ] && [ "$cdrom" = 0 ] && note "C) XA 组写 x12 在账 + 无盘 0x1C00 零访问(合法终态)" || { note "C) FAIL c1=$c1/$c1b cdrom=$cdrom"; fail=1; }
# D) 堆清零环存在(0x2170 系 sb 环, 82950 窗现场)
timeout 200 env BLEEM_BIOS=/home/gill/bleem/psxbios/SCPH1001.BIN BIOSRUN_CMEXP=1 BIOSRUN_TRACE=82948,82952 ./build/bleem_port --bios-run=82953 2>&1 | grep -q "TR 82949 pc=2174 insn=a0800000" && note "D) 堆 memset 环现场在账(sb-zero 环 a0 递增)" || { note "D) FAIL memset 环现场"; fail=1; }
# E) 2M 步 rom 占比 >90%(健康主流判据) + 无 JUMPZERO
rom=$(grep -oE "rom=[0-9]+" /tmp/h109_tr.log | head -1 | cut -d= -f2); st=$(grep -oE "steps=[0-9]+" /tmp/h109_tr.log | head -1 | cut -d= -f2)
jz=$(grep -c JUMPZERO /tmp/h109_tr.log || true)
[ "${rom:-0}" -gt $(( ${st:-1} * 90 / 100 )) ] && [ "$jz" = 0 ] && note "E) rom=$rom/$st(>90%)+JUMPZERO=0 健康主循环" || { note "E) FAIL rom=$rom st=$st jz=$jz"; fail=1; }
# F) 门链回归
if timeout 300 bash /home/gill/bleem/rewrite/tools/validate_host108.sh 2>&1 | grep -q "HOST108 CHECK PASS"; then note "F1) host108 回归 PASS"; else note "F1) FAIL"; fail=1; fi
export DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
if timeout 240 bash tools/final_check.sh 2>&1 | grep -q "FINAL CHECK PASS"; then note "F2) final_check PASS"; else note "F2) FAIL"; fail=1; fi
# G) docs=99+README+dis 勘正在账
cd /home/gill/bleem/rewrite
dd=$(ls docs/*.md | wc -l); grep -q "98_bios_marathon_w6a" README.md && [ "$dd" -ge 99 ] && note "G1) docs=$dd+README" || { note "G1) FAIL docs=$dd"; fail=1; }
grep -q "op==36: return f\"lh" tools/psx_rom_dis.py && note "G2) dis op36/37 勘正在账" || { note "G2) FAIL dis 未勘正"; fail=1; }
# H) 金标准恒+ROM/golden 不入 git
e=$(sha256sum /home/gill/bleem/demo-v1_4.EXE | cut -c1-8)
g=$(sha256sum decrypted/stream_10a4c_L71cbf.bin | cut -c1-8)
romgit=$(git ls-files | grep -icE "SCPH|psxbios|demo-v1_4|demo_patched|GOLDEN_RAM_2MB\.bin$" || true)
[ "$e" = "b61ee13e" ] && [ "$g" = "98752b98" ] && [ "$romgit" = "0" ] && note "H) 金标准恒+ROM/golden 不入 git" || { note "H) FAIL $e/$g/romgit=$romgit"; fail=1; }
[ $fail -eq 0 ] && echo "HOST109 CHECK PASS" || echo "HOST109 CHECK FAIL"
[ $fail -eq 0 ]
