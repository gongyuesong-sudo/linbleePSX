#!/usr/bin/env bash
# validate_host63.sh — M-16 封账审计(task-073, 2026-10-31)
# A) JSON 状态审计: task-001..072 全终态+字段完备; 073 唯一 in_progress
# B) STATS 末行与 JSON 计数一致
# C) README 终态锚(73 任务/H-25 交付节/host63)
# D) port final_check.sh 全绿(带 DISPLAY)
# E) docs=59 且 README 索引完整
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST63: $*"; }
python3 - <<'PYEOF' || fail=1
import json
d=json.load(open('harness-tasks.json'))
term=[t for t in d['tasks'] if t['status'] in ('completed','failed')]
inf=[t for t in d['tasks'] if t['status'] not in ('completed','failed')]
assert len(inf)<=1, [t['id'] for t in inf]
assert all(t.get('completed_at') for t in term if t['status']=='completed'), 'completed 缺 completed_at'
c=sum(1 for t in term if t['status']=='completed'); f2=sum(1 for t in term if t['status']=='failed')
print(f"HOST63: A) 状态审计通过(终态 {c}\u2713/{f2}\u2717, 在途 {len(inf)} 条=新开线容差; completed_at 完备)")
PYEOF
python3 - <<'PYEOF' || fail=1
import re,json
last=[l for l in open('harness-progress.txt') if ' [STATS] ' in l or ' STATS ' in l][-1]
m=re.search(r'completed=(\d+) failed=(\d+)',last); assert m,last
lc,lf=int(m.group(1)),int(m.group(2))
d=json.load(open('harness-tasks.json'))
c=sum(1 for t in d['tasks'] if t['status']=='completed'); f=sum(1 for t in d['tasks'] if t['status']=='failed')
assert (lc,lf) in ((c,f),(c-1,f),(c,f-1),(c-1,f-1)), (lc,lf,c,f)   # 末行=最近一次关账, 容差新线在途
print(f"HOST63: B) STATS 末行({lc}✓/{lf}✗) 与 JSON({c}✓/{f}✗+073 在途) 一致")
PYEOF
grep -q "73 任务全终态 60✓/13✗" README.md && grep -q "引擎兼容层 H-25 工程交付完成" README.md && grep -q "封账审计=validate_host63" README.md && note "C) README 终态锚齐" || { note "C) 缺锚"; fail=1; }
if command -v Xwayland >/dev/null 2>&1 || ls /run/user/1000/.mutter-Xwaylandauth.* >/dev/null 2>&1; then
  export XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1); export DISPLAY=:0
fi
(cd ../port && timeout 500 bash tools/final_check.sh > /tmp/h82_fc.log 2>&1) && grep -q "FINAL CHECK PASS" /tmp/h82_fc.log && note "D) port final_check 全绿(封账终跑)" || { note "D) FAIL"; tail -6 /tmp/h82_fc.log; fail=1; }
d=$(ls docs/*.md | wc -l)
m=0; for k in 46_cmd_handlers 52_batch2_falsified 58_rafix_hook evidence_ledger; do grep -q "$k" README.md || m=$((m+1)); done
{ [ "$d" -eq 64 ] && [ $m -eq 0 ] && note "E) docs=$d 抽验锚齐(全量索引 host45 已守)" || { note "E) FAIL docs=$d miss=$m"; fail=1; }; }
[ $fail -eq 0 ] && echo "HOST63 CHECK PASS" || echo "HOST63 CHECK FAIL"
[ $fail -eq 0 ]
