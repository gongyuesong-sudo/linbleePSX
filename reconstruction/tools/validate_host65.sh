#!/usr/bin/env bash
# validate_host65.sh — H-26 楔2 ZEROFIX 定版+分型终证(task-075, 2026-10-31)
# A) 孤跑转化集锚 = {19,43,45,4a,c0/2,c0/3} (6/24)
# B) 深度门防腐: 序列 [00,12,1c,19] 中 12/1c/19 浅层死跳必须仍 ctx (blocks=0 不得吞)
# C) 全序 ZEROFIX 恒 ok=2 ctx=22 (批次态两个世界)
# D) a0/72 立返型仍 ctx (未装服务语义正确)
# E) docs/60 锚; F) README docs=61; G) host64 关键门不回归(0x00 转化+假阳性)
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST65: $*"; }
python3 tools/gen_inc.py >/dev/null
gcc -m32 -O0 -g -no-pie -I host -o host/build/fwd_test tools/fwd_test.c host/fwd.c 2>&1 | grep -q error && { note "构建失败(必须零 error)"; exit 1; }
python3 - <<'PYEOF' || fail=1
import json,os,subprocess
rows=json.load(open('/tmp/h76_fam2.json'))
ok=[]
for v,c,_ in rows:
    lf=f"/tmp/h65_s_{v:x}_{c:x}.txt"; open(lf,'w').write(f"{v:x} {c:x}\n")
    env=dict(os.environ,H_ZEROFIX='1',FWDLIST=lf)
    o=subprocess.run(['bash','-c','cd /home/gill/bleem/rewrite && timeout 180 ./host/build/fwd_test 2>&1 | grep "^FWD vec"'],env=env,capture_output=True,text=True,timeout=210).stdout
    if 'rc=0' in o: ok.append(f"{v:x}/{c:x}")
assert sorted(ok)==sorted(['b0/19','b0/43','b0/45','b0/4a','c0/2','c0/3']), ok
print(f"HOST65: A) 孤跑转化集锚定 {ok}")
PYEOF
printf 'b0 00\nb0 12\nb0 1c\nb0 19\n' > /tmp/h65_seq.txt
o=$(H_ZEROFIX=1 FWDLIST=/tmp/h65_seq.txt timeout 300 ./host/build/fwd_test 2>&1 | grep -E "^FWD vec")
n_ok=$(echo "$o" | grep -c "rc=0"); n_ctx=$(echo "$o" | grep -c "rc=1")
if [ "$n_ok" -eq 1 ] && [ "$n_ctx" -eq 3 ] && echo "$o" | grep "cmd=12" | grep -q "rc=1"; then note "B) 深度门防腐: 序列转化=1(仅 0x00), 12/1c/19 浅死保留 ctx"; else note "B) FAIL $o"; fail=1; fi
H_ZEROFIX=1 FWDLIST=/tmp/h59_all.txt timeout 300 ./host/build/fwd_test 2>&1 | grep -q "FWDSUM ok=2 ctx=22 miss=0" && note "C) 全序 ZEROFIX 批次态恒 ok=2 ctx=22" || { note "C) FAIL"; fail=1; }
echo "a0 72" > /tmp/h65_72.txt
H_ZEROFIX=1 FWDLIST=/tmp/h65_72.txt timeout 60 ./host/build/fwd_test 2>&1 | grep -q "cmd=72 rc=1" && note "D) 未装服务立返型语义正确(仍 ctx)" || { note "D) FAIL"; fail=1; }
grep -q "深度门" docs/60_zerofix_depth.md && grep -q "语义腐蚀" docs/60_zerofix_depth.md && grep -q "表槽 stub 注册" docs/60_zerofix_depth.md && note "E) docs/60 锚齐" || { note "E) 缺锚"; fail=1; }
d=$(ls docs/*.md | wc -l); if grep -q "60_zerofix_depth" README.md && [ "$d" -eq 62 ]; then note "F) README 与 docs=$d 一致"; else note "F) FAIL docs=$d"; fail=1; fi
echo "b0 00" > /tmp/h65_00.txt
H_ZEROFIX=1 FWDLIST=/tmp/h65_00.txt timeout 200 ./host/build/fwd_test 2>&1 | grep -q "cmd=0 rc=0" && FWDLIST=/tmp/h65_00.txt timeout 200 ./host/build/fwd_test 2>&1 | grep -q "blocks=115871 haltpc=000040e4" && note "G) host64 门不回归(0x00 转化+无钩基线)" || { note "G) FAIL"; fail=1; }
[ $fail -eq 0 ] && echo "HOST65 CHECK PASS" || echo "HOST65 CHECK FAIL"
[ $fail -eq 0 ]
