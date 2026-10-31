#!/usr/bin/env bash
# validate_host72.sh — H-25/26/27 全线终封审计(task-082, 2026-10-31)
# A) JSON 全终态 81=68+13, 无 in_progress/pending; completed 均有 completed_at
# B) progress 最新 STATS 行与 JSON 计数一致
# C) README 终态口径三锚(81 任务/68✓/H-26/H-27 终判封线章)
# D) docs=67 且索引含 docs/59..66
# E) 能力钩在码(JIT: IRQMASK/ISTATCLR/EVTGEN/TIMEROK/GPUOK; fwd: RAFIX/ZEROFIX/FWDCREATE)
# F) 关键回归抽验(host67/68/69/70/71)
# G) port final_check 终跑(带 DISPLAY 兜底)
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST72: $*"; }
r=$(python3 - <<'EOF'
import json
d=json.load(open('harness-tasks.json'))
ts=d['tasks']
c=sum(1 for x in ts if x['status']=='completed'); f=sum(1 for x in ts if x['status']=='failed')
nt=[x['id'] for x in ts if x['status'] not in('completed','failed')]
noattr=[x['id'] for x in ts if x['status']=='completed' and not x.get('completed_at')]
print('TOT',len(ts),'C',c,'F',f,'NT',','.join(nt) or '-','NOATTR',','.join(noattr) or '-')
EOF
)
echo "$r" | grep -Eq "TOT 8[12] C 6[89] F 13 NT (task-082|-) NOATTR -" && note "A) JSON 终态完备(82=69+13, 在途容差仅限自身)" || { note "A) FAIL $r"; fail=1; }
last_stats=$(grep "STATS" harness-progress.txt | tail -1)
echo "$last_stats" | grep -Eq "tasks_total=8[12] completed=6[89] failed=13 pending=0 blocked=0" && note "B) 最新 STATS 一致" || { note "B) FAIL $last_stats"; fail=1; }
grep -q "82 任务全终态 69✓/13✗" README.md && grep -q "H-26/H-27 终判封线" README.md && grep -q "工程量而非认知问题" README.md && note "C) README 终态锚齐" || { note "C) FAIL"; fail=1; }
dd=$(ls docs/*.md | wc -l)
idx_ok=1; for i in 59 60 61 62 63 64 65 66; do grep -q "docs/${i}_" README.md || idx_ok=0; done
if [ "$dd" -eq 67 ] && [ "$idx_ok" -eq 1 ]; then note "D) docs=$dd 索引含 59-66"; else note "D) FAIL docs=$dd idx_ok=$idx_ok"; fail=1; fi
e=0
for k in H_IRQMASK H_ISTATCLR H_EVTGEN H_TIMEROK H_GPUOK; do grep -q "$k" host/mips_jit.c || { e=1; note "E) JIT 缺 $k"; }; done
for k in H_RAFIX FWDCREATE; do grep -q "$k" host/fwd.c tools/fwd_test.c || { e=1; note "E) fwd 缺 $k"; }; done
grep -q "ZEROFIX" host/fwd.c || { e=1; note "E) fwd 缺 ZEROFIX"; }
[ $e -eq 0 ] && note "E) 能力钩在码全齐" || fail=1
for v in 67 68 69 70 71; do timeout 500 bash tools/validate_host$v.sh >/tmp/h72r$v.log 2>&1 || { note "F) host$v FAIL"; fail=1; }; done
[ $fail -eq 0 ] && note "F) 近期锚点五连抽验绿"
export DISPLAY="${DISPLAY:-:0}"
if [ -z "${XAUTHORITY:-}" ]; then XA=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1); [ -n "$XA" ] && export XAUTHORITY="$XA"; fi
if (cd ../port && timeout 900 bash tools/final_check.sh >/tmp/h72_port.log 2>&1) && grep -q "FINAL CHECK PASS" /tmp/h72_port.log; then note "G) port final_check 终跑 PASS"; else note "G) port FAIL(见 /tmp/h72_port.log)"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST72 CHECK PASS" || echo "HOST72 CHECK FAIL"
[ $fail -eq 0 ]
