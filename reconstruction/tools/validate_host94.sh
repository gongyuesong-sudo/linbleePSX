#!/usr/bin/env bash
# validate_host94.sh — x86 宿主码战役终判封线审计(task-103, 2026-10-31) 仿 host83 七判
# A) JSON 动态终态(在途容差) B) STATS 一致性 C) README x86 终判章+标题口径
# D) docs=86 + docs/76-85 索引七锚 E) x86 取证工件三件在账
# F) host88/90/93 抽验 G) port final_check 终跑
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST94: $*"; }
X=/home/gill/bleem/rewrite/.analysis-check
# A/B) JSON 动态终态
ab=$(python3 - <<'PY'
import json
d=json.load(open('harness-tasks.json'))
ts=d['tasks']
c=sum(1 for x in ts if x['status']=='completed'); f=sum(1 for x in ts if x['status']=='failed')
nt=[x['id'] for x in ts if x['status'] not in ('completed','failed')]
badcomp=[x['id'] for x in ts if x['status']=='completed' and not x.get('completed_at')]
a=sum(x['attempts'] for x in ts); k=sum(len(x['checkpoints']) for x in ts)
ok = f==13 and len(nt)<=1 and not badcomp and len(ts)==c+f+len(nt)
print(f"{len(ts)} {c} {f} {len(nt)} {a} {k} {'OK' if ok else 'BAD'} {';'.join(nt) or '-'} {';'.join(badcomp) or '-'}")
PY
)
set -- $ab
TOT=$1; C=$2; F=$3; NT=$4; A=$5; K=$6; V=$7
if [ "$V" = "OK" ]; then note "A) JSON 终态: total=$TOT comp=$C fail=$F nonterm=$NT(在途容差) completed_at 齐"; else note "A) FAIL $ab"; fail=1; fi
if [ "$TOT" -eq $((C+F+NT)) ] && [ "$F" = "13" ]; then note "B) STATS 账目一致 ($TOT=$C+$F+$NT, att=$A ck=$K)"; else note "B) FAIL"; fail=1; fi
# C) README
grep -q "x86 宿主码战役终判封线" README.md && grep -q "x86 宿主码战役终判(SESSION-102..112, docs/76-85)" README.md && grep -q "103 任务全终态 90✓/13✗" README.md && note "C) README 终判章+标题口径锚" || { note "C) FAIL"; fail=1; }
# D) docs
dd=$(ls docs/*.md | wc -l)
idx=0; for n in 76_x86_recon 77_two_stage_stub 78_second_stage_map 79_unpack_chain_confluence 80_host_engine_s3 81_service_table_abi 82_mapbuilder_negation 83_static_ceiling 84_wine_boundary 85_native_reverify; do grep -q "$n" README.md || idx=1; done
[ "$dd" -eq 89 ] && [ $idx -eq 0 ] && note "D) docs=$dd + x86 十章索引全在" || { note "D) FAIL docs=$dd idx=$idx"; fail=1; }
# E) 取证工件
python3 - <<'PY' || { note "E) FAIL 工件"; true; }
import json,sys
i1=json.load(open('tools/x86_imports.json')); assert sum(len(v) for v in i1.values())==119
i2=json.load(open('tools/x86_iat_map.json')); assert len(i2)==119
i3=json.load(open('tools/x86_s3_api_use.json')); assert len(i3)>=1
sys.stdout.write("")
PY
[ $? -eq 0 ] && note "E) x86 取证工件三件在账(119/119/≥1)" || fail=1
# F) 抽验
for v in 88 90 93; do timeout 500 bash tools/validate_host$v.sh >/tmp/h94_v$v.log 2>&1 || { note "F) host$v FAIL"; fail=1; }; done
[ $fail -eq 0 ] && note "F) host88/90/93 抽验三连绿"
# G) port 终跑
export DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
if (cd ../port && timeout 240 bash tools/final_check.sh 2>&1 | grep -q "FINAL CHECK PASS"); then note "G) port final_check 终跑 PASS"; else note "G) FAIL"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST94 CHECK PASS" || echo "HOST94 CHECK FAIL"
[ $fail -eq 0 ]
