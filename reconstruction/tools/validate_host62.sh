#!/usr/bin/env bash
# validate_host62.sh — M-15 切换出口钩单点突破终证(task-072, 2026-10-31)
# A) 0x19 孤跑 H_RAFIX 转化 rc=0 blocks=91
# B) 0x3d 孤跑 H_RAFIX 转化 rc=0 blocks=736
# C) 假阳性门: 无钩基线 0x19/0x3d ctx 逐字(118051/119220)
# D) 全序批次态恒 ok=2 ctx=22(无钩与+钩两态)
# E) docs/58 锚; F) README docs=59
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST62: $*"; }
python3 tools/gen_inc.py >/dev/null
gcc -m32 -O0 -g -no-pie -I host -o host/build/fwd_test tools/fwd_test.c host/fwd.c 2>&1 | grep -q error && { note "构建失败(必须零 error)"; exit 1; }
echo "b0 19" > /tmp/h62_19.txt; echo "b0 3d" > /tmp/h62_3d.txt
a=$(H_RAFIX=1 FWDLIST=/tmp/h62_19.txt timeout 120 ./host/build/fwd_test 2>&1 | grep "^FWD vec")
if echo "$a" | grep -q "rc=0 .*blocks=91 haltpc=bfc4c1b4"; then note "A) 0x19 钩下转化: $a"; else note "A) FAIL $a"; fail=1; fi
b=$(H_RAFIX=1 FWDLIST=/tmp/h62_3d.txt timeout 120 ./host/build/fwd_test 2>&1 | grep "^FWD vec")
if echo "$b" | grep -q "rc=0 .*blocks=736 haltpc=bfc4c1b4"; then note "B) 0x3d 钩下转化: $b"; else note "B) FAIL $b"; fail=1; fi
c=$(FWDLIST=/tmp/h62_19.txt timeout 120 ./host/build/fwd_test 2>&1 | grep "^FWD vec")
d=$(FWDLIST=/tmp/h62_3d.txt timeout 120 ./host/build/fwd_test 2>&1 | grep "^FWD vec")
if echo "$c" | grep -q "blocks=118051" && echo "$d" | grep -q "blocks=119220"; then note "C) 假阳性门: 无钩基线逐字不变"; else note "C) FAIL c=$c d=$d"; fail=1; fi
python3 - <<'PYEOF' || fail=1
import json,os,subprocess
rows=json.load(open('/tmp/h76_fam2.json'))
open('/tmp/h62_all.txt','w').writelines(f"{v:x} {c:x}\n" for v,c,_ in rows)
for extra in ({},{'H_RAFIX':'1'}):
    env=dict(os.environ,FWDLIST='/tmp/h62_all.txt',**extra)
    o=subprocess.run(['bash','-c','cd /home/gill/bleem/rewrite && timeout 300 ./host/build/fwd_test 2>&1 | grep FWDSUM'],env=env,capture_output=True,text=True,timeout=340).stdout
    assert 'ok=2 ctx=22 miss=0' in o,(extra,o)
print("HOST61: D) 全序批次态两态恒 ok=2 ctx=22(批次态阻因在钩前, docs/58 §3)")
PYEOF
grep -q "H_RAFIX" docs/58_rafix_hook.md && grep -q "H-25 到此\*\*工程交付完成\*\*" docs/58_rafix_hook.md && note "E) docs/58 锚齐" || { note "E) 缺锚"; fail=1; }
d2=$(ls docs/*.md | wc -l); if grep -q "58_rafix_hook" README.md && [ "$d2" -eq 66 ]; then note "F) README 与 docs=$d2 一致"; else note "F) FAIL docs=$d2"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST62 CHECK PASS" || echo "HOST62 CHECK FAIL"
[ $fail -eq 0 ]
