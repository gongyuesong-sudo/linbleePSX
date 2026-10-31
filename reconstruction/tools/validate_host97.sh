#!/usr/bin/env bash
# validate_host97.sh — 总收官报告门(task-105, 2026-10-31)
# A) 报告结构锚(EN summary/五战线表/定案8/边界/失败账/方法论)  B) 命令清单抽检真跑
# C) 账目数字与 JSON 实况一致  D) docs=88+README 索引  E) EXE 只读恒
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST97: $*"; }
E=/home/gill/bleem/demo-v1_4.EXE
D=docs/87_grandfinale_report.md
a=0
[ -f $D ] || a=99
for pat in "Executive Summary" "五条战线" "定案清单" "边界与否定" "可复现命令清单" "失败任务账" "方法论沉淀" "混合执行模型" "Win16 API"; do grep -q "$pat" $D || a=$((a+1)); done
[ "$a" = "0" ] && note "A) 报告在+结构九锚齐(双语节在)" || { note "A) FAIL 缺锚累计=$a"; fail=1; }
# B) 报告§5 命令抽检(轻量的三条现场真跑)
b=0
timeout 300 bash tools/native_unpack/build_selftest.sh >/tmp/h97_b1.log 2>&1 && b=$((b+1))
timeout 200 bash tools/analysis_harness/build_check.sh >/tmp/h97_b2.log 2>&1 && b=$((b+1))
timeout 60 bash tools/analysis_harness/build_check.sh --verify >/tmp/h97_b3.log 2>&1 && b=$((b+1))
[ "$b" = "3" ] && note "B) 命令清单抽检 3/3 现场复跑 PASS" || { note "B) FAIL b=$b/3"; fail=1; }
# C) 报告声称账目与 JSON 实况
c=$(python3 - <<'PY'
import json
d=json.load(open('harness-tasks.json'))
c=sum(1 for x in d['tasks'] if x['status']=='completed'); f=sum(1 for x in d['tasks'] if x['status']=='failed')
p=sum(1 for x in d['tasks'] if x['status']=='pending')
print(len(d['tasks']),c,f,p)
PY
)
set -- $c
IP=$(( $1 - $2 - $3 - $4 ))
if grep -q "$1 = $2✓/$3✗/$4 排队" $D || { [ "$IP" -ge 1 ] && grep -q "$1 = $(( $2 + IP ))✓/$3✗/$4 排队" $D; }; then note "C) 账目口径一致(实况 $1/$2/$3/$4+在途$IP, 报告按关账点表述)"; else note "C) FAIL 账目 $c 在途$IP 未在报告"; fail=1; fi
dd=$(ls docs/*.md | wc -l); if grep -q "87_grandfinale_report" README.md && [ "$dd" -eq 88 ]; then note "D) docs=$dd + README 索引在"; else note "D) FAIL docs=$dd"; fail=1; fi
s=$(sha256sum $E | cut -c1-8); [ "$s" = "b61ee13e" ] && note "E) EXE 只读恒" || { note "E) FAIL"; fail=1; }
[ $fail -eq 0 ] && echo "HOST97 CHECK PASS" || echo "HOST97 CHECK FAIL"
[ $fail -eq 0 ]
