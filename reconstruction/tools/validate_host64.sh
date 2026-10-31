#!/usr/bin/env bash
# validate_host64.sh — H-26 楔1 零出口钩终证(task-074, 2026-10-31)
# A) b0/00 +H_ZEROFIX 转化 rc=0 (无钩恒 115871/0x40E4)
# B) 孤跑群 ZEROFIX 转化=6/24 且集合锚定 {19,43,45,4a,c0/2,c0/3}
# C) 全序假阳性门 ok=2 ctx=22 恒
# D) TCB 迁移后 host59 关键门行为(0x19 Tcb 轨迹+QDUMP 动态读)
# E) docs/59 锚+勘误声明; F) README docs=60
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST64: $*"; }
python3 tools/gen_inc.py >/dev/null
gcc -m32 -O0 -g -no-pie -I host -o host/build/fwd_test tools/fwd_test.c host/fwd.c 2>&1 | grep -q error && { note "构建失败(必须零 error)"; exit 1; }
echo "b0 00" > /tmp/h64_00.txt
a=$(H_ZEROFIX=1 FWDLIST=/tmp/h64_00.txt timeout 200 ./host/build/fwd_test 2>&1 | grep "^FWD vec")
b=$(FWDLIST=/tmp/h64_00.txt timeout 200 ./host/build/fwd_test 2>&1 | grep "^FWD vec")
echo "$a" | grep -q "rc=0" && echo "$b" | grep -q "blocks=115871 haltpc=000040e4" && note "A) 0x00 钩下转化且无钩基线恒定: $a" || { note "A) FAIL a=$a b=$b"; fail=1; }
python3 - <<'PYEOF' || fail=1
import json,os,subprocess
rows=json.load(open('/tmp/h76_fam2.json'))
ok=[]
for v,c,_ in rows:
    lf=f"/tmp/h64_s_{v:x}_{c:x}.txt"; open(lf,'w').write(f"{v:x} {c:x}\n")
    env=dict(os.environ,H_ZEROFIX='1',FWDLIST=lf)
    o=subprocess.run(['bash','-c','cd /home/gill/bleem/rewrite && timeout 180 ./host/build/fwd_test 2>&1 | grep "^FWD vec"'],env=env,capture_output=True,text=True,timeout=210).stdout
    if 'rc=0' in o: ok.append(f"{v:x}/{c:x}")
assert sorted(ok)==sorted(['b0/19','b0/43','b0/45','b0/4a','c0/2','c0/3']), ok
print(f"HOST64: B) ZEROFIX 孤跑转化集锚定 {ok}")
PYEOF
FWDLIST=/tmp/h59_all.txt H_ZEROFIX=1 timeout 300 ./host/build/fwd_test 2>&1 | grep -q "FWDSUM ok=2 ctx=22 miss=0" && note "C) 全序假阳性门恒 ok=2 ctx=22" || { note "C) FAIL"; fail=1; }
echo "b0 19" > /tmp/h64_19.txt
rm -f /tmp/h64_qd.txt; FWDLIST=/tmp/h64_19.txt FWDSERVE=1 H_IRQTICK=1 FWDLiveThread=1 FWDPoison=1 FWDSBW=/tmp/h64_qd.txt timeout 150 ./host/build/fwd_test >/dev/null 2>&1
grep -q "801ffe00->dead0000" /tmp/h64_qd.txt && grep -q "m85d4=bfc4c1b0" /tmp/h64_qd.txt && note "D) TCB 迁移(0x9400)后切换语境门+QDUMP 动态读正常" || { note "D) FAIL"; head -2 /tmp/h64_qd.txt; fail=1; }
grep -q "广义零出口钩" docs/59_create_devsw.md && grep -q "先写结论后跑实验" docs/59_create_devsw.md && note "E) docs/59 锚+勘误声明在" || { note "E) 缺锚"; fail=1; }
d=$(ls docs/*.md | wc -l); if grep -q "59_create_devsw" README.md && [ "$d" -eq 63 ]; then note "F) README 与 docs=$d 一致"; else note "F) FAIL docs=$d"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST64 CHECK PASS" || echo "HOST64 CHECK FAIL"
[ $fail -eq 0 ]
