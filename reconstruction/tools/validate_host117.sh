#!/usr/bin/env bash
# validate_host117.sh — ③战役楔3 payload 动态 trace 门(task-125, 2026-11-01)
set -u
cd /home/gill/bleem/port
fail=0; note(){ echo "HOST117: $*"; }
rm -f build/bleem_port
if cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/tmp/h117_b.log 2>&1 && [ -x build/bleem_port ]; then
  note "A) 重建 PASS"; else note "A) FAIL"; grep -i error /tmp/h117_b.log | head -2; fail=1; fi
export BLEEM_BIOS=/home/gill/bleem/psxbios/SCPH1001.BIN
ok=1; fsum=0; tot=""
for t in psx cpu dev eng; do ./build/bleem_port --$t-selftest >/tmp/h117_$t.log 2>&1; r=$?; p=$(grep -c ": PASS" /tmp/h117_$t.log); f=$(grep -c ": FAIL" /tmp/h117_$t.log); tot="$tot ${t}=${p}P/${f}F"; eval "p_$t=$p"; fsum=$((fsum+f)); [ $r -eq 0 ] || ok=0; done
[ $ok -eq 1 ] && [ "$fsum" = "0" ] && [ "$p_psx" -ge 31 ] && [ "$p_cpu" -ge 16 ] && [ "$p_dev" -ge 10 ] && [ "$p_eng" -ge 9 ] && note "B) 四面回归$tot" || { note "B) FAIL$tot"; fail=1; }
# C) gdb 管道复现: view=12e0000 + errcode=6 + dump 在位(模板驱动一次实跑)
export DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1) LD_LIBRARY_PATH=/home/gill/.local/lib WINEDEBUG=-all
pgrep -x wineserver >/dev/null && wineserver -w; sleep 2
cd /home/gill/.wine/drive_c/demo
timeout 150 bash -c "cat /home/gill/bleem/rewrite/tools/wdb_payload.gdb | winedbg --gdb demo_v2.EXE" >/tmp/h117_gdb.log 2>&1
grep -aq "AT_CALL view=12e0000" /tmp/h117_gdb.log && grep -aq "MSGBOX_HIT errcode=6" /tmp/h117_gdb.log && [ -s /tmp/payload_view.bin ] && c=1 || c=0
[ "$c" = 1 ] && note "C) 触发链复现(view=12e0000@CALL + errcode=6@MSGBOX + dump 非空)" || { note "C) FAIL"; grep -aE "AT_CALL|MSGBOX" /tmp/h117_gdb.log|head -2; fail=1; }
# D) 解压落地判读复现: dump 前 4KB 全零 + 1000 起与 .text 部分同(非原只读映射)
d=$(python3 - <<'PY'
b=open("/tmp/payload_view.bin","rb").read()
f=open("/home/gill/.wine/drive_c/demo/demo_v2.EXE","rb").read()
ok1 = b[:0x1000]==b"\x00"*0x1000
t=sum(1 for i in range(0,0x400,4) if b[0x1000+i:0x1004+i]==f[0x600+i:0x604+i])
print("1" if (ok1 and 0 < t < 256) else "0")
PY
)
[ "$d" = 1 ] && note "D) 落地区判读复现(前4KB全零+部分.text=重映射痕迹)" || { note "D) FAIL"; fail=1; }
# E) 门链 + final_check
pkill -9 -f 'demo_v[23]' 2>/dev/null; sleep 1
pgrep -x wineserver >/dev/null && wineserver -w; sleep 2
if timeout 550 bash /home/gill/bleem/rewrite/tools/validate_host116.sh 2>&1 | grep -q "HOST116 CHECK PASS"; then note "E1) host116 回归 PASS"; else note "E1) FAIL"; fail=1; fi
cd /home/gill/bleem/port
if timeout 240 bash tools/final_check.sh 2>&1 | grep -q "FINAL CHECK PASS"; then note "E2) final_check PASS"; else note "E2) FAIL"; fail=1; fi
cd /home/gill/bleem/rewrite
dd=$(ls docs/*.md | wc -l); grep -q "106_payload_wine_w3" README.md && [ "$dd" -ge 107 ] && note "F1) docs=$dd+README" || { note "F1) FAIL docs=$dd"; fail=1; }
e=$(sha256sum /home/gill/bleem/demo-v1_4.EXE | cut -c1-8)
g2=$(sha256sum decrypted/stream_10a4c_L71cbf.bin | cut -c1-8)
bad=$(git ls-files | grep -icE "SCPH|psxbios|demo-v1_4|demo_patched|demo_v2|demo_v3|GOLDEN_RAM_2MB\.bin$|\.PBP$|payload_view" || true)
[ "$e" = "b61ee13e" ] && [ "$g2" = "98752b98" ] && [ "$bad" = "0" ] && note "G) 金标准恒+工件不入 git" || { note "G) FAIL $e/$g2/bad=$bad"; fail=1; }
[ $fail -eq 0 ] && echo "HOST117 CHECK PASS" || echo "HOST117 CHECK FAIL"
[ $fail -eq 0 ]
