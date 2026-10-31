#!/usr/bin/env bash
# validate_host54.sh — M-7 H-25 批1 转发器 API（task-064，2026-10-31）
# A) 构建 fwd_test
# B) 核清单(docs/49 BREAK 集现场重建): FWDSUM ok=26 ctx=0 miss=0
# C) 全量 50: ok=26 ctx=24 miss=0, 且 ok 集 == docs/49 BREAK 集逐行一致
# D) docs/50 锚(两发现+验收) ; E) README 含 50_fwd_api 且 docs 计数=51
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST54: $*"; }
python3 tools/gen_inc.py >/dev/null
gcc -m32 -O0 -g -no-pie -I host -o host/build/fwd_test tools/fwd_test.c host/fwd.c || { note "A) 构建失败"; exit 1; }
note "A) fwd_test 构建 ok"
python3 - <<'PYEOF' || fail=1
import re,subprocess,sys
doc=open('/home/gill/bleem/rewrite/docs/49_forward_subset.md').read()
ok=[];alll=[]
for l in doc.splitlines():
    m=re.match(r'\| 0x([0-9a-f]{2}) \| 0x([0-9a-f]{2}) \| (\S+) \| (\w+) \| (\w+) \|',l)
    if m:
        k=(int(m.group(1),16),int(m.group(2),16)); alll.append(k)
        if m.group(4)=='BREAK' or m.group(5)=='BREAK': ok.append(k)
assert len(ok)==26 and len(alll)==50
open('/tmp/h54_ok.txt','w').write("".join(f"{v:x} {c:x}\n" for v,c in ok))
open('/tmp/h54_all.txt','w').write("".join(f"{v:x} {c:x}\n" for v,c in alll))
def run(lst):
    r=subprocess.run(['bash','-c',f'cd /home/gill/bleem/rewrite && FWDLIST={lst} timeout 300 ./host/build/fwd_test'],capture_output=True,text=True,timeout=330).stdout
    res={}
    for l in r.splitlines():
        if l.startswith('FWD vec'):
            m=dict(re.findall(r'(\w+)=(\S+)',l)); res[(int(m['vec'],16),int(m['cmd'],16))]=int(m['rc'])
    s={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',[l for l in r.splitlines() if l.startswith('FWDSUM')][0])}
    return res,s
rk,sk=run('/tmp/h54_ok.txt'); ra,sa=run('/tmp/h54_all.txt')
assert sk=={'ok':26,'ctx':0,'miss':0}, sk
assert sa=={'ok':26,'ctx':24,'miss':0}, sa
assert set(k for k,v in ra.items() if v==0)==set(ok)
print(f"HOST54: B/C) 核 26/26 全通; 全量 ok=26 ctx=24 miss=0; ok 集与 docs/49 逐点一致")
PYEOF
grep -q "真 handler 会清活表槽" docs/50_fwd_api.md && grep -q "安装态快照" docs/50_fwd_api.md && grep -q "ok=26 ctx=0 miss=0" docs/50_fwd_api.md \
  && note "D) docs/50 锚齐" || { note "D) 缺锚"; fail=1; }
d=$(ls docs/*.md | wc -l); grep -q "50_fwd_api" README.md && [ "$d" -eq 55 ] \
  && note "E) README 索引与 docs=$d 一致" || { note "E) FAIL docs=$d"; fail=1; }
[ $fail -eq 0 ] && echo "HOST54 CHECK PASS" || echo "HOST54 CHECK FAIL"
[ $fail -eq 0 ]
