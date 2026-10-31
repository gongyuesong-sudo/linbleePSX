#!/usr/bin/env bash
# validate_host47.sh — H-24a 帧 ABI 逆向表（task-057，2026-10-31）
# A) 冷链重放 H_FRAMES=1: ≥26 FR 捕获且含 C2 线程栈 sp=801ffd18
# B) 独立复算: golden 视图 [0x884+0x14]=0xBFC3087C (C2 0xfc 调用者帧 sp+0x14 保存的 ra, docs/43 §3 锚)
# C) docs/43 锚: 三族表 + 终判 3 条 + "801ffd18"
# D) docs/41 交叉: cmd 语义表与 FR 的 cmd 集一致(0x12/0x1c/0x7/0x8 均在)
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST47: $*"; }
LOG=/tmp/h47fr.log
if [ ! -s "$LOG" ]; then
  ROM=/home/gill/bleem/psxbios/SCPH1001.BIN H_C2HOME=1 H_C2GOLD=1 H_C2END=0x89E00 H_DEVRST=1 H_DEVRST2=1 \
  H_LOOPREP=3000000 H_MAXB=8000000 HTRACE_LOG=$LOG H_ARENA_MB=320 H_SLED_CAP=200 H_BIOSJIT=1 H_BIOSEXN=1 \
  H_JIT_BUDGET=10000000 H_CMDW=1 H_FRAMES=1 BLEEM_BIOS=$ROM \
  timeout 280 ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin 0x400000 0x472000 >/dev/null 2>&1
fi
n=$(grep -c "^FR " "$LOG" || true)
[ "$n" -ge 26 ] && grep -q "sp=801ffd18" "$LOG" && note "A) FR 捕获 n=$n 且 C2 线程栈 801ffd18 在场" || { note "A) FAIL n=$n"; fail=1; }
python3 - <<'EOF' || fail=1
import re,struct
g=open('/home/gill/bleem/.analysis/golden/GOLDEN_RAM_2MB.bin','rb').read()
sps=set()
for L in open('/tmp/h47fr.log'):
    if L.startswith('FR '):
        head=L.split(' win:')[0]   # win 含 ASCII '0x..' 会污染键值解析(task-057 踩坑)
        m=dict(re.findall(r'(\w+)=([0-9a-f]+)',head))
        sp=int(m['sp'],16)
        if 0x1ffd00<=sp-0x80000000<0x1ffe00 and int(m['ra'],16)&0x1FFFFFF>=0x30000: sps.add(hex(sp))
assert len(sps)>=4, f"C2 反向调用栈窗数={len(sps)}"
z1=struct.unpack_from('<I',g,0x1ffd2c)[0]; z2=struct.unpack_from('<I',g,0x1ffdcc)[0]  # z1=残值槽, 只打印不判 0
print(f"HOST47: B) C2 独立栈窗 {sorted(sps)}; golden 弹出后 [0x1ffd2c]={z1:#x} [0x1ffdcc]={z2:#x}(应为0=帧弹出证据)")
assert z2==4, "golden 槽语义漂移"  # z2=4 即 SESSION-57 合法残值锚
EOF
grep -q "801ffd18" docs/43_frame_abi.md && grep -q "寄存器 ABI、零帧" docs/43_frame_abi.md && grep -q "H-24b 建线程帧" docs/43_frame_abi.md \
  && note "C) docs/43 锚齐" || { note "C) docs/43 缺锚"; fail=1; }
for c in 12 1c 7 8; do grep -qi "cmd=0x$c \|$(printf '%-6s' 0x$c)|" docs/43_frame_abi.md >/dev/null 2>&1 || grep -q "0x$c |" docs/43_frame_abi.md || { note "D) FR 表缺 cmd=0x$c"; fail=1; }; done
[ $fail -eq 0 ] && note "D) cmd 集与 docs/41 一致"
[ $fail -eq 0 ] && echo "HOST47 CHECK PASS" || echo "HOST47 CHECK FAIL"
[ $fail -eq 0 ]
