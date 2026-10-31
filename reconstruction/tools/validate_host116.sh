#!/usr/bin/env bash
# validate_host116.sh — ③战役楔2 反调试二墙分辨门(task-124, 2026-11-01)
set -u
cd /home/gill/bleem/port
fail=0; note(){ echo "HOST116: $*"; }
rm -f build/bleem_port
if cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/tmp/h116_b.log 2>&1 && [ -x build/bleem_port ]; then
  note "A) 重建 PASS"; else note "A) FAIL"; grep -i error /tmp/h116_b.log | head -2; fail=1; fi
export BLEEM_BIOS=/home/gill/bleem/psxbios/SCPH1001.BIN
ok=1; fsum=0; tot=""
for t in psx cpu dev eng; do ./build/bleem_port --$t-selftest >/tmp/h116_$t.log 2>&1; r=$?; p=$(grep -c ": PASS" /tmp/h116_$t.log); f=$(grep -c ": FAIL" /tmp/h116_$t.log); tot="$tot ${t}=${p}P/${f}F"; eval "p_$t=$p"; fsum=$((fsum+f)); [ $r -eq 0 ] || ok=0; done
[ $ok -eq 1 ] && [ "$fsum" = "0" ] && [ "$p_psx" -ge 31 ] && [ "$p_cpu" -ge 16 ] && [ "$p_dev" -ge 10 ] && [ "$p_eng" -ge 9 ] && note "B) 四面回归$tot" || { note "B) FAIL$tot"; fail=1; }
# C) v2 补丁现场复现: file 0x63B 六字节=90*6 且锚前 8B 在位(mov+cmp 未被误伤)
c=$(python3 - <<'EOF'
d=open("/home/gill/.wine/drive_c/demo/demo_v2.EXE","rb").read()
import hashlib
ok1 = d[0x633:0x63b]==bytes.fromhex("a14411400083f801")
ok2 = d[0x63b:0x641]==b"\x90"*6
ok3 = hashlib.sha256(d).hexdigest().startswith("d266056d")
print("1" if ok1 and ok2 and ok3 else "0")
EOF
)
[ "$c" = 1 ] && note "C) v2 补丁现场复现(mov/cmp 锚在位+NOP jne 6B+sha 恒)" || { note "C) FAIL"; fail=1; }
# D) Wine 值面证据复现: pebprobe 编译+实跑 BeingDebugged=0 且 DebugPort=NOT_IMPLEMENTED
export PATH=/home/gill/.local/mingw/usr/bin:$PATH
if i686-w64-mingw32-gcc-posix -o /tmp/h116_pebprobe.exe /home/gill/bleem/rewrite/tools/pebprobe.c --sysroot=/home/gill/.local/mingw/usr/i686-w64-mingw32 2>/dev/null && [ -x /tmp/h116_pebprobe.exe ]; then
  export DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
  pgrep -x wineserver >/dev/null && wineserver -w; sleep 2   # 前缀锁竞态守卫
  out=$(timeout 60 wine /tmp/h116_pebprobe.exe 2>/dev/null)
  echo "$out" | grep -q "BeingDebugged=0" && echo "$out" | grep -q "DebugPort  st=c0000004" && d1=1 || d1=0
  [ "$d1" = 1 ] && note "D) Wine 值面复现(调试位全干净 + DebugPort 未实现=失配候选锚)" || { note "D) FAIL out=[$out]"; fail=1; }
else note "D) FAIL mingw 编译 pebprobe"; fail=1; fi
# E) 门链 + final_check
pgrep -x wineserver >/dev/null && wineserver -w; sleep 2
if timeout 550 bash /home/gill/bleem/rewrite/tools/validate_host115.sh 2>&1 | grep -q "HOST115 CHECK PASS"; then note "E1) host115 回归 PASS"; else note "E1) FAIL"; fail=1; fi
cd /home/gill/bleem/port
if timeout 240 bash tools/final_check.sh 2>&1 | grep -q "FINAL CHECK PASS"; then note "E2) final_check PASS"; else note "E2) FAIL"; fail=1; fi
cd /home/gill/bleem/rewrite
dd=$(ls docs/*.md | wc -l); grep -q "105_payload_wine_w2" README.md && [ "$dd" -ge 106 ] && note "F1) docs=$dd+README" || { note "F1) FAIL docs=$dd"; fail=1; }
e=$(sha256sum /home/gill/bleem/demo-v1_4.EXE | cut -c1-8)
g2=$(sha256sum decrypted/stream_10a4c_L71cbf.bin | cut -c1-8)
bad=$(git ls-files | grep -icE "SCPH|psxbios|demo-v1_4|demo_patched|demo_v2|demo_v3|GOLDEN_RAM_2MB\.bin$|\.PBP$|demo[-_]v[0-9]" || true)
[ "$e" = "b61ee13e" ] && [ "$g2" = "98752b98" ] && [ "$bad" = "0" ] && note "G) 金标准恒+EXE/PBP/golden 不入 git" || { note "G) FAIL $e/$g2/bad=$bad"; fail=1; }
[ $fail -eq 0 ] && echo "HOST116 CHECK PASS" || echo "HOST116 CHECK FAIL"
[ $fail -eq 0 ]
