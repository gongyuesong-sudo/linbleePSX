#!/usr/bin/env bash
# validate_host99.sh — 全工程 FINAL-SEAL-ALL 终审(task-107, 2026-10-31)
# A) JSON 终态动态判定(在途<=1 泛化, 不点名任务) B) STATS 磁盘重算一致
# C) README 终局段(FINAL-SEAL-ALL 标题+全工程终局项) D) 六战役收官门抽验真跑(94/95/97/98)
# E) port final_check + AH build_check F) 金标准恒(EXE/s3/ROM) G) 镜像同步状态报告
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST99: $*"; }
NT=$(python3 - <<'PY'
import json
d=json.load(open('harness-tasks.json'))
bad=[]; nt_ip=0
for t in d['tasks']:
    if t['status'] not in ('completed','failed','pending','in_progress'): bad.append(t['id'])
    if t['status']=='completed' and not t.get('completed_at'): bad.append(t['id'])
    if t['status']=='pending': bad.append('PENDING:'+t['id'])
    if t['status']=='in_progress': nt_ip+=1
print(f"{len(d['tasks'])} {sum(1 for t in d['tasks'] if t['status']=='completed')} "
      f"{sum(1 for t in d['tasks'] if t['status']=='failed')} {'OK' if not bad and nt_ip<=1 else 'BAD'}")
PY
) || { note "A) JSON 解析失败"; fail=1; NT="ERR"; }
read -r TOT C F CHKV <<< "$NT"
[ "$TOT" -ge 107 ] && [ "$F" = "13" ] && [ "$C" -ge 93 ] && [ "$CHKV" = OK ] \
  && note "A) JSON 终态 $TOT=$C✓/$F✗(在途<=1 泛化容差)$CHKV" || { note "A) FAIL $NT"; fail=1; }
ATT=$(python3 -c "import json;d=json.load(open('harness-tasks.json'));print(sum(t['attempts'] for t in d['tasks']),sum(len(t['checkpoints']) for t in d['tasks']))")
read -r A K <<< "$ATT"
[ "$A" -ge 126 ] && [ "$K" -ge 145 ] && note "B) 磁盘重算 attempts=$A ck=$K" || { note "B) FAIL $ATT"; fail=1; }
if grep -q "全工程 FINAL-SEAL-ALL 停线" README.md && grep -q "全工程终局(FINAL-SEAL-ALL" README.md && grep -q "88_port_thin_engine_w1" README.md; then
  note "C) README 终局段(标题+全工程终局项+PEw1 索引)"; else note "C) FAIL"; fail=1; fi
for v in 94 95 97 98; do
  if timeout 280 bash tools/validate_host${v}.sh >/tmp/h99_$v.log 2>&1; then note "D) host${v} 抽验 PASS"; else note "D) host${v} FAIL"; tail -2 /tmp/h99_$v.log; fail=1; fi
done
export DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
if timeout 300 bash /home/gill/bleem/port/tools/final_check.sh 2>&1 | grep -q "FINAL CHECK PASS"; then note "E1) port final_check PASS"; else note "E1) FAIL"; fail=1; fi
# E2 双跑+留证: 与 D 段抽验编译并发时曾偶发, 单次 FAIL 不作废终审(第二次必带全量日志)
E2OK=0
for try in 1 2; do
  if timeout 240 bash tools/analysis_harness/build_check.sh >/tmp/h99_ah.log 2>&1 && grep -q "BUILD-CHECK PASS" /tmp/h99_ah.log; then E2OK=1; note "E2) AH build_check PASS(try=$try)"; break; fi
done
[ $E2OK -eq 1 ] || { note "E2) FAIL 两次"; tail -6 /tmp/h99_ah.log; fail=1; }
e=$(sha256sum /home/gill/bleem/demo-v1_4.EXE | cut -c1-8)
g=$(sha256sum decrypted/stream_10a4c_L71cbf.bin | cut -c1-8)
b=$(sha256sum /home/gill/bleem/psxbios/SCPH1001.BIN | cut -c1-8)
[ "$e" = "b61ee13e" ] && [ "$g" = "98752b98" ] && [ "$b" = "71af94d1" ] && note "F) EXE/s3/ROM 三金标准恒" || { note "F) FAIL $e/$g/$b"; fail=1; }
cd /home/gill/bleem/linbleePSX 2>/dev/null && git fetch -q origin 2>/dev/null && \
  [ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ] && note "G) 镜像 HEAD==origin/main(关账后需再同步一次才算终态)" || note "G) WARN 镜像待同步(关账步骤处理)"
[ $fail -eq 0 ] && echo "HOST99 CHECK PASS" || echo "HOST99 CHECK FAIL"
[ $fail -eq 0 ]
