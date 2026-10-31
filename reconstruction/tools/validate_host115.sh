#!/usr/bin/env bash
# validate_host115.sh — ③战役楔1 payload-Wine 侦察门(task-123, 2026-11-01)
set -u
cd /home/gill/bleem/port
fail=0; note(){ echo "HOST115: $*"; }
rm -f build/bleem_port
if cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/tmp/h115_b.log 2>&1 && [ -x build/bleem_port ]; then
  note "A) 重建 PASS"; else note "A) FAIL"; grep -i error /tmp/h115_b.log | head -2; fail=1; fi
export BLEEM_BIOS=/home/gill/bleem/psxbios/SCPH1001.BIN
ok=1; fsum=0; tot=""
for t in psx cpu dev eng; do ./build/bleem_port --$t-selftest >/tmp/h115_$t.log 2>&1; r=$?; p=$(grep -c ": PASS" /tmp/h115_$t.log); f=$(grep -c ": FAIL" /tmp/h115_$t.log); tot="$tot ${t}=${p}P/${f}F"; eval "p_$t=$p"; fsum=$((fsum+f)); [ $r -eq 0 ] || ok=0; done
[ $ok -eq 1 ] && [ "$fsum" = "0" ] && [ "$p_psx" -ge 31 ] && [ "$p_cpu" -ge 16 ] && [ "$p_dev" -ge 10 ] && [ "$p_eng" -ge 9 ] && note "B) 四面回归$tot" || { note "B) FAIL$tot"; fail=1; }
# C) PBP 加密判读复现: PSISOIMG0000@0x10000 明文 + 全文件零 CD001/零 PS-X EXE
c=$(python3 - <<'EOF'
P="/home/gill/bleem/psgame/D 打击者1945 II(日)-SLPS-01637_2DSTG.PBP"
f=open(P,"rb"); f.seek(0x10000); ok1 = f.read(12)==b"PSISOIMG0000"
f.seek(0); pos=0; cd=0; pe=0
import re
while pos<202763212:
    blk=f.read(16<<20)
    if not blk: break
    cd+=len(re.findall(b"CD001",blk)); pe+=len(re.findall(b"PS-X EXE",blk)); pos+=len(blk)
print("1" if (ok1 and cd==0 and pe==0) else "0")
EOF
)
[ "$c" = 1 ] && note "C) PBP 加密容器判读复现(明文描述符在位+全扫零文件系统明文)" || { note "C) FAIL"; fail=1; }
# D) Wine 假CD探测复现: .windows-label 命中 + SYSTEM.CNF 零访问(对话框墙行为)
export DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1) LD_LIBRARY_PATH=/home/gill/.local/lib
[ -e /home/gill/.wine/drive_c/fakecd/SYSTEM.CNF ] || { mkdir -p /home/gill/.wine/drive_c/fakecd && printf 'BOOT=cdrom:\\SLPS_01637.EXE;1\nTDELAY=16\n' > /home/gill/.wine/drive_c/fakecd/SYSTEM.CNF; }
[ -L /home/gill/.wine/dosdevices/e: ] || ln -s /home/gill/.wine/drive_c/fakecd /home/gill/.wine/dosdevices/e:
rm -f /dev/null 2>/dev/null; cd /home/gill/.wine/drive_c/demo && timeout 50 strace -f -qq -e trace=openat -o /tmp/h115_st.log wine demo_patched.EXE >/dev/null 2>&1
grep -qa "fakecd/.windows-label" /tmp/h115_st.log && ! grep -qa "fakecd/SYSTEM.CNF" /tmp/h115_st.log && d=1 || d=0
[ "$d" = 1 ] && note "D) Wine 探测面复现(e: 可见但 SYSTEM.CNF 零访问=墙在位)" || { note "D) FAIL d=$d"; fail=1; }
# E) 门链 + final_check
if timeout 550 bash /home/gill/bleem/rewrite/tools/validate_host114.sh 2>&1 | grep -q "HOST114 CHECK PASS"; then note "E1) host114 回归 PASS"; else note "E1) FAIL"; fail=1; fi
cd /home/gill/bleem/port
if timeout 240 bash tools/final_check.sh 2>&1 | grep -q "FINAL CHECK PASS"; then note "E2) final_check PASS"; else note "E2) FAIL"; fail=1; fi
cd /home/gill/bleem/rewrite
dd=$(ls docs/*.md | wc -l); grep -q "104_payload_wine_w1" README.md && [ "$dd" -ge 105 ] && note "F1) docs=$dd+README" || { note "F1) FAIL docs=$dd"; fail=1; }
e=$(sha256sum /home/gill/bleem/demo-v1_4.EXE | cut -c1-8)
g2=$(sha256sum decrypted/stream_10a4c_L71cbf.bin | cut -c1-8)
romgit=$(git ls-files | grep -icE "SCPH|psxbios|demo-v1_4|demo_patched|GOLDEN_RAM_2MB\.bin$|\.PBP$" || true)
[ "$e" = "b61ee13e" ] && [ "$g2" = "98752b98" ] && [ "$romgit" = "0" ] && note "G) 金标准恒+ROM/golden/PBP 不入 git" || { note "G) FAIL $e/$g2/romgit=$romgit"; fail=1; }
[ $fail -eq 0 ] && echo "HOST115 CHECK PASS" || echo "HOST115 CHECK FAIL"
[ $fail -eq 0 ]
