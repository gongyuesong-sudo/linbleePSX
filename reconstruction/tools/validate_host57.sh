#!/usr/bin/env bash
# validate_host57.sh — M-10 批3楔子1 waiter 定性终证(task-067, 2026-10-31)
# A) IRQ 族代表复测(H_IRQTICK+FWDSERVE): b0/c blocks>3000 haltpc=000000b0
# B) 深探针复测: b0/19 blocks>100000; b0/15 haltpc=a000e028
# C) 0x3d 自递归静态复算: ROM+0x406c+0x24 = jal -> 0x406c
# D) 全序恒定 FWDSUM ok=2 ctx=22; E) docs/53 锚; F) README docs=54
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST57: $*"; }
python3 tools/gen_inc.py >/dev/null
gcc -m32 -O0 -g -no-pie -I host -o host/build/fwd_test tools/fwd_test.c host/fwd.c 2>&1 | grep -q error && { note "构建失败"; exit 1; }
run1(){ echo "$1 $2" > /tmp/h57_one.txt; H_IRQTICK=1 FWDSERVE=1 FWDLIST=/tmp/h57_one.txt timeout 120 ./host/build/fwd_test 2>&1 | grep "^FWD vec"; }
l=$(run1 b0 c); if echo "$l" | grep -q "haltpc=000000b0" && [ "$(echo "$l"|sed 's/.*blocks=\([0-9]*\).*/\1/')" -gt 3000 ]; then note "A) IRQ 族深推进复测: $l"; else note "A) FAIL $l"; fail=1; fi
l=$(run1 b0 19); if echo "$l" | grep -q "haltpc=80058628" && [ "$(echo "$l"|sed 's/.*blocks=\([0-9]*\).*/\1/')" -gt 100000 ]; then note "B1) 0x19 深探针复测一致"; else note "B1) FAIL $l"; fail=1; fi
l=$(run1 b0 15); echo "$l" | grep -q "haltpc=a000e028" && note "B2) 0x15 堆码 haltpc 一致" || { note "B2) FAIL $l"; fail=1; }
python3 - <<'PYEOF' || fail=1
import struct
g=open('/home/gill/bleem/.analysis/golden/GOLDEN_RAM_2MB.bin','rb').read()
raw=struct.unpack_from('<I',g,0x874+0x3d*4)[0]; base=raw&0xFFFFF
w=struct.unpack_from('<I',g,base+0x24)[0]
assert (w>>26)==3 and (w&0x3FFFFFF)<<2==base, hex(w)
print("HOST57: C) 0x3d 自递归 jal 复算一致 -> 尾跳重放语义成立")
PYEOF
python3 - <<'PYEOF' || fail=1
import json,os,subprocess
rows=json.load(open('/tmp/h76_fam2.json'))
open('/tmp/h57_all.txt','w').writelines(f"{v:x} {c:x}\n" for v,c,_ in rows)
env=dict(os.environ,H_IRQTICK='1',FWDSERVE='1',FWDLIST='/tmp/h57_all.txt')
o=subprocess.run(['bash','-c','cd /home/gill/bleem/rewrite && timeout 300 ./host/build/fwd_test 2>&1 | grep FWDSUM'],env=env,capture_output=True,text=True,timeout=340).stdout
assert 'ok=2 ctx=22 miss=0' in o, o
print("HOST57: D) 全序 waiter 终态恒定 ok=2 ctx=22")
PYEOF
grep -q "不是\"数据语境洞\"" docs/53_waiter_census.md && grep -q "WAITER 语义" docs/53_waiter_census.md && note "E) docs/53 锚齐" || { note "E) 缺锚"; fail=1; }
d=$(ls docs/*.md | wc -l); if grep -q "53_waiter_census" README.md && [ "$d" -eq 57 ]; then note "F) README 与 docs=$d 一致"; else note "F) FAIL docs=$d"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST57 CHECK PASS" || echo "HOST57 CHECK FAIL"
[ $fail -eq 0 ]
