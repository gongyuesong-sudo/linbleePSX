#!/usr/bin/env bash
# validate_host58.sh — M-11 0x19 线程调度器终证(task-068, 2026-10-31)
# A) SPB 单事件复算: b0/19 sp 迁移恰 1 次 801ffe00->000085d8 @pc=80058628
# B) 静态语境复算: [0x85D4]=0xE10 / [0xE08]与[0xE20]=thunk jalr / [0x89C44]=0xFEFF0002 / [0x26F0]=0
# C) 45/4a 双态恒 rc=0(修正 docs/51 定序定性)
# D) docs/54 锚; E) README docs=55
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST58: $*"; }
python3 tools/gen_inc.py >/dev/null
gcc -m32 -O0 -g -no-pie -I host -o host/build/fwd_test tools/fwd_test.c host/fwd.c 2>&1 | grep -q error && { note "构建失败(必须零 error)"; exit 1; }
rm -f /tmp/h58_spb.txt; echo "b0 19" > /tmp/h58_one.txt
FWDLIST=/tmp/h58_one.txt H_IRQTICK=1 FWDSERVE=1 FWDSBW=/tmp/h58_spb.txt timeout 120 ./host/build/fwd_test >/dev/null 2>&1
n=$(wc -l < /tmp/h58_spb.txt); l=$(cat /tmp/h58_spb.txt)
if [ "$n" -eq 1 ] && echo "$l" | grep -q "801ffe00->000085d8" && echo "$l" | grep -q "pc=80058628"; then note "A) SPB 单事件复算一致: $l"; else note "A) FAIL n=$n $l"; fail=1; fi
python3 - <<'PYEOF' || fail=1
import struct
g=open('/home/gill/bleem/.analysis/golden/GOLDEN_RAM_2MB.bin','rb').read()
W=lambda a: struct.unpack_from('<I',g,a)[0]
assert W(0x85D4)==0x00000E10, hex(W(0x85D4))
assert W(0xE08)==0x0220F809 and W(0xE20)==0x0200F809      # thunk jalr r31,r17 / r31,r16
assert W(0x89C44)==0xFEFF0002, hex(W(0x89C44))            # 回收状态词非指针
assert W(0x26F0)==0
import os
b=open('/home/gill/bleem/.analysis/golden/RAM_BOOT_5s_2MB.bin','rb').read()
assert struct.unpack_from('<I',b,0x89C44)[0]==0xFEFF0002  # 早期态同回收形
print("HOST58: B) 语境复算全中(TCB 槽回收+thunk 池+EMITFAIL 落点+t5s 同形)")
PYEOF
for mode in "FWDLIST=/tmp/h58_one45.txt" "FWDLIST=/tmp/h58_all.txt"; do :; done
echo "b0 45" > /tmp/h58_one45.txt; echo "b0 4a" > /tmp/h58_one4a.txt
python3 - <<'PYEOF' || fail=1
import json,os,subprocess
rows=json.load(open('/tmp/h76_fam2.json'))
open('/tmp/h58_all.txt','w').writelines(f"{v:x} {c:x}\n" for v,c,_ in rows)
def run(lf,extra):
    env=dict(os.environ,FWDLIST=lf,**extra)
    return subprocess.run(['bash','-c','cd /home/gill/bleem/rewrite && timeout 300 ./host/build/fwd_test 2>&1'],env=env,capture_output=True,text=True,timeout=340).stdout
solo_s=run('/tmp/h58_one45.txt',{'FWDSERVE':'1','H_IRQTICK':'1'})+run('/tmp/h58_one4a.txt',{'FWDSERVE':'1','H_IRQTICK':'1'})
full_s=run('/tmp/h58_all.txt',{})
ok45 = solo_s.count('cmd=45 rc=0')==1 and full_s.count('cmd=45 rc=0')==1
ok4a = solo_s.count('cmd=4a rc=0')==1 and full_s.count('cmd=4a rc=0')==1
full_ok = full_s.count('rc=0')
assert ok45 and ok4a, (solo_s[:200],full_s[:400])
assert 'FWDSUM ok=2 ctx=22 miss=0' in full_s, full_s[-200:]
print(f"HOST58: C) 45/4a 双态恒 rc=0 复算一致; 全序 ok={full_ok} 恒 {set(('45','4a'))}")
PYEOF
grep -q "线程调度/切换服务" docs/54_0x19_tcb.md && grep -q "予以作废" docs/54_0x19_tcb.md && grep -q "TCB 结构" docs/54_0x19_tcb.md && note "D) docs/54 锚齐" || { note "D) 缺锚"; fail=1; }
d=$(ls docs/*.md | wc -l); if grep -q "54_0x19_tcb" README.md && [ "$d" -eq 58 ]; then note "E) README 与 docs=$d 一致"; else note "E) FAIL docs=$d"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST58 CHECK PASS" || echo "HOST58 CHECK FAIL"
[ $fail -eq 0 ]
