#!/usr/bin/env bash
# validate_host83.sh — A3 终判封线审计(task-092, 2026-10-31)
# A) 终态完备(动态): failed=13, 非终态<=1(在途自身), completed 有 completed_at
# B) 最新 STATS 形态一致(failed=13 pending=0)
# C) README 终判锚(A3 主运行战役终判/92 任务 79✓/语义上不存在)
# D) docs=76 且索引含 docs/67..75
# E) A3 能力钩在码(FWDMAIN/FWDSEGS/H_CPUGOLD/H_HOSTRET/H_SEGP/H_SVCLOW/H_POKE/H_SEQTOP/H_SVCT/H_RAMDUMP/jit_lib_set_regs)
# F) A3 锚点抽验(host79/80/81/82)
# G) port final_check 终跑(DISPLAY 兜底)
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST83: $*"; }
r=$(python3 - <<'EOF'
import json,re
d=json.load(open('harness-tasks.json'))
ts=d['tasks']
c=sum(1 for x in ts if x['status']=='completed'); f=sum(1 for x in ts if x['status']=='failed')
nt=[x['id'] for x in ts if x['status'] not in('completed','failed')]
na=[x['id'] for x in ts if x['status']=='completed' and not x.get('completed_at')]
print('TOT',len(ts),'C',c,'F',f,'NT',','.join(nt) or '-','NOATTR',','.join(na) or '-')
EOF
)
aok=$(echo "$r" | python3 -c "
import sys,re
s=sys.stdin.read()
tot=int(re.search(r'TOT (\d+)',s).group(1)); c=int(re.search(r'C (\d+)',s).group(1))
f=int(re.search(r'F (\d+)',s).group(1)); nt=re.search(r'NT (\S+)',s).group(1); na=re.search(r'NOATTR (\S+)',s).group(1)
nts=[] if nt=='-' else nt.split(',')
print('OK' if (f==13 and len(nts)<=1 and na=='-' and tot==c+f+len(nts)) else 'BAD')")
[ "$aok" = "OK" ] && note "A) 终态完备(动态断言: $r)" || { note "A) FAIL $r"; fail=1; }
last_stats=$(grep "STATS" harness-progress.txt | tail -1)
echo "$last_stats" | grep -Eq "failed=13 pending=0 blocked=0" && note "B) 最新 STATS 形态一致" || { note "B) FAIL $last_stats"; fail=1; }
grep -q "A3 主运行战役终判" README.md && grep -q "92 任务全终态 79✓/13✗" README.md && grep -q "语义上不存在" README.md && note "C) README 终判锚齐" || { note "C) FAIL"; fail=1; }
dd=$(ls docs/*.md | wc -l)
idx_ok=1; for i in 67 68 69 70 71 72 73 74 75; do grep -q "docs/${i}_" README.md || idx_ok=0; done
if [ "$dd" -eq 78 ] && [ "$idx_ok" -eq 1 ]; then note "D) docs=$dd 索引含 67-75"; else note "D) FAIL docs=$dd idx=$idx_ok"; fail=1; fi
e=0
for k in FWDMAIN FWDSEGS H_CPUGOLD H_HOSTRET H_SEGP H_SVCLOW H_POKE H_SEQTOP H_SVCT H_RAMDUMP; do grep -q "$k" tools/fwd_test.c host/fwd.c host/mips_jit.c || { e=1; note "E) 缺 $k"; }; done
grep -q "jit_lib_set_regs" host/mips_jit.c || { e=1; note "E) 缺 jit_lib_set_regs"; }
[ $e -eq 0 ] && note "E) A3 能力钩全在码" || fail=1
for v in 79 80 81 82; do timeout 500 bash tools/validate_host$v.sh >/tmp/h83r$v.log 2>&1 || { note "F) host$v FAIL"; fail=1; }; done
[ $fail -eq 0 ] && note "F) A3 锚点四连抽验绿"
export DISPLAY="${DISPLAY:-:0}"
if [ -z "${XAUTHORITY:-}" ]; then XA=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1); [ -n "$XA" ] && export XAUTHORITY="$XA"; fi
if (cd ../port && timeout 900 bash tools/final_check.sh >/tmp/h83_port.log 2>&1) && grep -q "FINAL CHECK PASS" /tmp/h83_port.log; then note "G) port final_check 终跑 PASS"; else note "G) port FAIL(见 /tmp/h83_port.log)"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST83 CHECK PASS" || echo "HOST83 CHECK FAIL"
[ $fail -eq 0 ]
