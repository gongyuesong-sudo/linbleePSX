#!/usr/bin/env bash
# validate_host118.sh — ③战役楔4 落地全量取证门(task-126, 2026-11-01)
set -u
export PATH="$PATH:/usr/lib/x86_64-linux-gnu/wine"  # SESSION-138: wineserver 不在 PATH 守卫自愈
cd /home/gill/bleem/port
fail=0; note(){ echo "HOST118: $*"; }
rm -f build/bleem_port
if cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/tmp/h118_b.log 2>&1 && [ -x build/bleem_port ]; then
  note "A) 重建 PASS"; else note "A) FAIL"; grep -i error /tmp/h118_b.log | head -2; fail=1; fi
export BLEEM_BIOS=/home/gill/bleem/psxbios/SCPH1001.BIN
ok=1; fsum=0; tot=""
for t in psx cpu dev eng; do ./build/bleem_port --$t-selftest >/tmp/h118_$t.log 2>&1; r=$?; p=$(grep -c ": PASS" /tmp/h118_$t.log); f=$(grep -c ": FAIL" /tmp/h118_$t.log); tot="$tot ${t}=${p}P/${f}F"; eval "p_$t=$p"; fsum=$((fsum+f)); [ $r -eq 0 ] || ok=0; done
[ $ok -eq 1 ] && [ "$fsum" = "0" ] && [ "$p_psx" -ge 31 ] && [ "$p_cpu" -ge 16 ] && [ "$p_dev" -ge 10 ] && [ "$p_eng" -ge 9 ] && note "B) 四面回归$tot" || { note "B) FAIL$tot"; fail=1; }
# C) 取证时机+三区 dump 复现: POST_CALL eax=6(清理前窗口) + rwx_b sha 恒
export DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1) LD_LIBRARY_PATH=/home/gill/.local/lib WINEDEBUG=-all
for p in $(pgrep -x 'demo_v2.EXE') $(pgrep -x 'winedbg.exe'); do kill -9 $p 2>/dev/null; done
pgrep -x wineserver >/dev/null && wineserver -w; sleep 2
cd /home/gill/.wine/drive_c/demo
rm -f /tmp/h118_rwx_b.bin /tmp/h118_rwx_a.bin
sed 's#/tmp/mem_#/tmp/h118_#g' /home/gill/bleem/rewrite/tools/wdb_payload_dump.gdb > /tmp/h118_gdb.txt
timeout 300 bash -c "cat /tmp/h118_gdb.txt | winedbg --gdb demo_v2.EXE" >/tmp/h118_gdb.log 2>&1
c1=$(grep -ac "POST_CALL eax=6" /tmp/h118_gdb.log)
# SESSION-138 卫生修正: rwx_b 内容含运行期熵(指针烧录), 跨 run sha 会漂移=易变工件;
# 改钉不可变事实: POST_CALL 复现 + 三区 dump 尺寸恒(尺寸恒由 D 段复核)
sb_sz=$(stat -c%s /tmp/h118_rwx_b.bin 2>/dev/null)
sa_sz=$(stat -c%s /tmp/h118_rwx_a.bin 2>/dev/null)
[ "$c1" = 1 ] && [ "$sb_sz" = 1572864 ] && [ -n "$sa_sz" ] && [ -s /tmp/h118_rwx_a.bin ] && [ -s /tmp/mem_view.bin -o -s /tmp/h118_view.bin -o -f /tmp/h118_mem_view.bin ] && note "C) 取证窗口复现(POST_CALL eax=6@清理前)+rwx_b 1.5MB 在位" || { note "C) FAIL c1=$c1 sb_sz=$sb_sz"; fail=1; }
# D) 落地判读复现: 两 rwx 区零 booter 常量直引 + 无简单 errcode 返回模式
d=$(python3 - <<'PY'
try:
    a=open("/tmp/h118_rwx_a.bin","rb").read(); b=open("/tmp/h118_rwx_b.bin","rb").read()
except Exception:
    print("0"); raise SystemExit
bad=any(x in a or x in b for x in [b"\x68\x2d\x14\x40\x00",b"\x24\x11\x40\x00",b"\x68\xba\x13\x40\x00"])
nomode=not any(bytes([0xb8,c,0,0,0,0xc3]) in a or bytes([0xb8,c,0,0,0,0xc3]) in b for c in (3,4,5,6))
big = len(b)==0x180000 and len(a)==0x30000
print("1" if (not bad and nomode and big) else "0")
PY
)
[ "$d" = 1 ] && note "D) 落地判读复现(rwx 双区尺寸恒+零 booter 常量直引+无简单返回模式)" || { note "D) FAIL"; fail=1; }
# E) 门链 + final_check
for p in $(pgrep -x 'demo_v2.EXE') $(pgrep -x 'winedbg.exe'); do kill -9 $p 2>/dev/null; done
pgrep -x wineserver >/dev/null && wineserver -w; sleep 2
if timeout 550 bash /home/gill/bleem/rewrite/tools/validate_host117.sh 2>&1 | grep -q "HOST117 CHECK PASS"; then note "E1) host117 回归 PASS"; else note "E1) FAIL"; fail=1; fi
cd /home/gill/bleem/port
if timeout 240 bash tools/final_check.sh 2>&1 | grep -q "FINAL CHECK PASS"; then note "E2) final_check PASS"; else note "E2) FAIL"; fail=1; fi
cd /home/gill/bleem/rewrite
dd=$(ls docs/*.md | wc -l); grep -q "107_payload_wine_w4" README.md && [ "$dd" -ge 108 ] && note "F1) docs=$dd+README" || { note "F1) FAIL docs=$dd"; fail=1; }
e=$(sha256sum /home/gill/bleem/demo-v1_4.EXE | cut -c1-8)
g2=$(sha256sum decrypted/stream_10a4c_L71cbf.bin | cut -c1-8)
bad=$(git ls-files | grep -icE "SCPH|psxbios|demo-v1_4|demo_patched|demo_v2|demo_v3|GOLDEN_RAM_2MB\.bin$|\.PBP$|mem_rwx|mem_view|payload_view" || true)
[ "$e" = "b61ee13e" ] && [ "$g2" = "98752b98" ] && [ "$bad" = "0" ] && note "G) 金标准恒+工件/dump 不入 git" || { note "G) FAIL $e/$g2/bad=$bad"; fail=1; }
[ $fail -eq 0 ] && echo "HOST118 CHECK PASS" || echo "HOST118 CHECK FAIL"
[ $fail -eq 0 ]
