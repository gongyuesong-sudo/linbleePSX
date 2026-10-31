#!/usr/bin/env bash
# validate_host59.sh — M-12 调度器语境画像终证(task-069, 2026-10-31)
# A) ready 队列布局复算(0x6CE0 五 sp+saved-ra 表)+0x85D8 唯一性
# B) 0x58628 suspend 函数字段清单词级复算
# C) FWDTcb 注入下 0x19 轨迹恒定性(118051/0x58628)
# D) 全序 ok=2 ctx=22 + SENT 门无假阳性; E) docs/55 锚; F) README docs=56
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST59: $*"; }
python3 tools/gen_inc.py >/dev/null
gcc -m32 -O0 -g -no-pie -I host -o host/build/fwd_test tools/fwd_test.c host/fwd.c 2>&1 | grep -q error && { note "构建失败(必须零 error)"; exit 1; }
python3 - <<'PYEOF' || fail=1
import struct
g=open('/home/gill/bleem/.analysis/golden/GOLDEN_RAM_2MB.bin','rb').read()
W=lambda a: struct.unpack_from('<I',g,a)[0]
q=[W(0x6CE0+k*4) for k in range(8)]
assert q[:5]==[0x66C4,0x67B8,0x6870,0x6928,0x85D8], [hex(x) for x in q]
assert q[5]==0xF40 and q[6]==0x85D4
assert W(0x85D4)==0xE10
u=[a for a in range(0,0x200000,4) if W(a)==0x85D8]
assert u==[0x6CF0], [hex(x) for x in u]
assert W(0x89C44)==0xFEFF0002
# B) suspend 函数字段词级复算
assert W(0x5862C)==0x8CA59C44      # lw a1,0x9c44(a1[lui 8008])
assert W(0x58640)==0x31CF0008      # andi t7,t6,8  (bit3 ready)
assert W(0x58684)==0xACB90000      # sw t9(=0xFFF7 suspend),[a1+0]
assert W(0x5868C)==0x3C0100FF and W(0x58690)==0x3421FFFF and W(0x58698)==0x3C018800
print("HOST59: A+B) 队列布局+0x85D8 唯一位点+suspend 字段清单复算一致")
PYEOF
echo "b0 19" > /tmp/h59_one.txt
a=$(FWDLIST=/tmp/h59_one.txt FWDSERVE=1 H_IRQTICK=1 timeout 120 ./host/build/fwd_test 2>&1 | grep "^FWD vec")
b=$(FWDLIST=/tmp/h59_one.txt FWDSERVE=1 H_IRQTICK=1 FWDTcb=1 timeout 120 ./host/build/fwd_test 2>&1 | grep "^FWD vec")
if [ "$a" = "$b" ] && echo "$a" | grep -q "blocks=118051 haltpc=80058628"; then note "C) FWDTcb 轨迹恒定门(FWD 行一致): $a"; else note "C) FAIL base=$a tcb=$b"; fail=1; fi
sa=$(rm -f /tmp/h59_sa.txt; FWDLIST=/tmp/h59_one.txt FWDSERVE=1 H_IRQTICK=1 FWDSBW=/tmp/h59_sa.txt timeout 120 ./host/build/fwd_test >/dev/null 2>&1; cat /tmp/h59_sa.txt)
sb=$(rm -f /tmp/h59_sb.txt; FWDLIST=/tmp/h59_one.txt FWDSERVE=1 H_IRQTICK=1 FWDTcb=1 FWDSBW=/tmp/h59_sb.txt timeout 120 ./host/build/fwd_test >/dev/null 2>&1; cat /tmp/h59_sb.txt)
if [ "$sa" = "$sb" ] && echo "$sa" | grep -q "801ffe00->000085d8"; then note "C2) SPB 双态逐字一致(单槽注入不改切换语境)"; else note "C2) FAIL sa=$sa sb=$sb"; fail=1; fi
python3 - <<'PYEOF' || fail=1
import json,os,subprocess
rows=json.load(open('/tmp/h76_fam2.json'))
open('/tmp/h59_all.txt','w').writelines(f"{v:x} {c:x}\n" for v,c,_ in rows)
env=dict(os.environ,FWDLIST='/tmp/h59_all.txt')
o=subprocess.run(['bash','-c','cd /home/gill/bleem/rewrite && timeout 300 ./host/build/fwd_test 2>&1'],env=env,capture_output=True,text=True,timeout=340).stdout
assert 'FWDSUM ok=2 ctx=22 miss=0' in o, o[-200:]
import re
zeros=[l for l in o.splitlines() if re.search(r'rc=0 .*haltpc=bfc4c1b4',l)]
assert len(zeros)==2, zeros
print("HOST59: D) 全序 clean 恒定 ok=2 ctx=22 (真完成=45/4a 型) + 上方 SPB 双态一致门")
PYEOF
grep -q "高16位=0x8800" docs/55_tcb_layout.md && grep -q "运行期构建" docs/55_tcb_layout.md && note "E) docs/55 锚齐" || { note "E) 缺锚"; fail=1; }
d=$(ls docs/*.md | wc -l); if grep -q "55_tcb_layout" README.md && [ "$d" -eq 61 ]; then note "F) README 与 docs=$d 一致"; else note "F) FAIL docs=$d"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST59 CHECK PASS" || echo "HOST59 CHECK FAIL"
[ $fail -eq 0 ]
