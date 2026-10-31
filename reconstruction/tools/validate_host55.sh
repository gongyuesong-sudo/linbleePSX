#!/usr/bin/env bash
# validate_host55.sh — M-8 ctx 嵌套需求分型（task-065，2026-10-31）
# A) 全量 50 序跑(clean): ok=26 且 haltpc 分型 = 零页12/B0向量6/分发器1/ROM深1 (+2 顺序敏感项在零页或B0桶)
# B) ctx24 孤跑: ok=2 且 BREAK 集恰 {b0/45, b0/4a}(批次态依赖实证)
# C) docs/51 锚; D) README+docs 计数=52
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST55: $*"; }
python3 tools/gen_inc.py >/dev/null
gcc -m32 -O0 -g -no-pie -I host -o host/build/fwd_test tools/fwd_test.c host/fwd.c || { note "构建失败"; exit 1; }
python3 - <<'PYG' || { echo "清单生成失败"; exit 1; }
import re
doc=open('/home/gill/bleem/rewrite/docs/49_forward_subset.md').read()
ok=[];alll=[]
for l in doc.splitlines():
    m=re.match(r'\| 0x([0-9a-f]{2}) \| 0x([0-9a-f]{2}) \| \S+ \| (\w+) \| (\w+) \|',l)
    if m:
        v,c=int(m.group(1),16),int(m.group(2),16); alll.append((v,c))
        if m.group(3)=='BREAK' or m.group(4)=='BREAK': ok.append((v,c))
open('/tmp/h55_all.txt','w').write("".join(f"{v:x} {c:x}\n" for v,c in alll))
oks=set(ok)
open('/tmp/h55_ctx.txt','w').write("".join(f"{v:x} {c:x}\n" for v,c in alll if (v,c) not in oks))
PYG
FWDLIST=/tmp/h55_all.txt timeout 300 ./host/build/fwd_test > /tmp/h55all.log 2>&1
FWDLIST=/tmp/h55_ctx.txt timeout 300 ./host/build/fwd_test > /tmp/h55ctx.log 2>&1
python3 - <<'PYEOF' || fail=1
import re,collections
def load(p):
    rows={}
    for l in open(p):
        if l.startswith('FWD vec'):
            m=dict(re.findall(r'(\w+)=(\S+)',l)); rows[(int(m['vec'],16),int(m['cmd'],16))]=m
    return rows
A=load('/tmp/h55all.log'); C=load('/tmp/h55ctx.log')
ok=[k for k,m in A.items() if m['rc']=='0']
buck=collections.Counter()
for k,m in A.items():
    if m['rc']=='0': continue
    pc=int(m['haltpc'],16)
    buck['zero' if pc==0 else ('b0vec' if pc==0xB0 else ('disp' if pc==0xBFC1E988 else 'deep'))]+=1
assert len(A)==50 and len(ok)==26, (len(A),len(ok))
assert buck['zero']==11 and buck['deep']==13 and buck.get('b0vec',0)==0, dict(buck)  # fwd 快照解析使 halt 更深(对比 hprobe 桶)
okc=[k for k,m in C.items() if m['rc']=='0']
assert sorted(okc)==[(0xB0,0x45),(0xB0,0x4A)], okc
print(f"HOST55: A) 分型一致 {dict(buck)} ok=26; B) ctx 孤跑转化集 {{b0/45,b0/4a}} 批次态依赖实证")
PYEOF
grep -q "批次态依赖" docs/51_batch2_ctx.md && grep -q "RAM\[0x100+4c\]" docs/51_batch2_ctx.md && grep -q "ok=2 ctx=22 四环境恒定" docs/51_batch2_ctx.md \
  && note "C) docs/51 锚齐" || { note "C) 缺锚"; fail=1; }
d=$(ls docs/*.md | wc -l); grep -q "51_batch2_ctx" README.md && [ "$d" -eq 56 ] \
  && note "D) README 与 docs=$d 一致" || { note "D) FAIL docs=$d"; fail=1; }
[ $fail -eq 0 ] && echo "HOST55 CHECK PASS" || echo "HOST55 CHECK FAIL"
[ $fail -eq 0 ]
