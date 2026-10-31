#!/usr/bin/env bash
# validate_host60.sh — M-13 填表式活线程三层阻力终证(task-070, 2026-10-31)
# A) 毒值跟随: FWDLiveThread+FWDPoison 下切换 sp=dead0000(队列表=真消费点)
# B) ra 非内存来源钉死: 切换时刻 m85d4=bfc4c1b0(SENT 未被覆盖) 而 ra=0xE10; 且 golden 0x85D4=0xE10 唯一槽
# C) tcb94= 运行期回写(静态 TCB 注入被 suspend 覆写)
# D) 全序基线恒定 ok=2 ctx=22; E) docs/56 锚; F) README docs=57
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST60: $*"; }
python3 tools/gen_inc.py >/dev/null
gcc -m32 -O0 -g -no-pie -I host -o host/build/fwd_test tools/fwd_test.c host/fwd.c 2>&1 | grep -q error && { note "构建失败(必须零 error)"; exit 1; }
echo "b0 19" > /tmp/h60_one.txt; rm -f /tmp/h60_qd.txt
FWDLIST=/tmp/h60_one.txt FWDSERVE=1 H_IRQTICK=1 FWDLiveThread=1 FWDPoison=1 FWDSBW=/tmp/h60_qd.txt timeout 120 ./host/build/fwd_test >/dev/null 2>&1
q=$(grep "^QDUMP" /tmp/h60_qd.txt | head -1); s=$(grep "^SPB" /tmp/h60_qd.txt | head -1)
echo "$s" | grep -q "801ffe00->dead0000" && echo "$q" | grep -q "sp=dead0000 q0=dead0000" && note "A) 毒值跟随(队列表=sp 消费点): $s" || { note "A) FAIL s=$s q=$q"; fail=1; }
echo "$q" | grep -q "m85d4=bfc4c1b0" && echo "$s" | grep -q "ra=00000e10" && note "B) ra 非内存来源钉死(SENT 槽完好而 ra=0xE10)" || { note "B) FAIL q=$q s=$s"; fail=1; }
python3 - <<'PYEOF' || fail=1
import struct
g=open('/home/gill/bleem/.analysis/golden/GOLDEN_RAM_2MB.bin','rb').read()
W=lambda a: struct.unpack_from('<I',g,a)[0]
u=[a for a in range(0,0x200000,4) if W(a)==0xE10]
assert u==[0x85D4],[hex(x) for x in u]
print("HOST60: B2) golden 值=0xE10 唯一位点=0x85D4 复算一致")
PYEOF
echo "$q" | grep -q "tcb94=880085d8" && note "C) TCB(0x9400) 运行期回写(suspend 覆写注入槽)" || { note "C) FAIL q=$q"; fail=1; }
FWDLIST=/tmp/h59_all.txt timeout 300 ./host/build/fwd_test 2>&1 | grep -q "FWDSUM ok=2 ctx=22 miss=0" && note "D) 全序基线恒定" || { note "D) FAIL"; fail=1; }
grep -q "事件驱动运行期实现" docs/56_livethread_resistance.md && grep -q "实施顺序建议 2→1→3" docs/56_livethread_resistance.md && note "E) docs/56 锚齐" || { note "E) 缺锚"; fail=1; }
d=$(ls docs/*.md | wc -l); if grep -q "56_livethread_resistance" README.md && [ "$d" -eq 65 ]; then note "F) README 与 docs=$d 一致"; else note "F) FAIL docs=$d"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST60 CHECK PASS" || echo "HOST60 CHECK FAIL"
[ $fail -eq 0 ]
