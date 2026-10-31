#!/usr/bin/env bash
# validate_host56.sh — M-9 批2否证终证（task-066，2026-10-31）
# A) 早期窗同态复算: rootcall_vec_5s.bin == GOLDEN[0x100:0x180]
# B) fwd 复测: ctx24 孤跑 ok=2 ctx=22 miss=0 (FWDVECT+EXN2 修正后仍恒定)
# C) docs/52 锚; D) README 计数=53
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST56: $*"; }
python3 - <<'PYEOF' || fail=1
w=open('/home/gill/bleem/.analysis/golden/rootcall_vec_5s.bin','rb').read()
g=open('/home/gill/bleem/.analysis/golden/GOLDEN_RAM_2MB.bin','rb').read()
assert len(w)==0x80 and w==g[0x100:0x180], (len(w),w==g[0x100:0x180])
import struct
nz=sum(1 for a in range(0,0x80,4) if struct.unpack_from('<I',w,a)[0])
assert nz==12, nz
print(f"HOST56: A) 早期窗同态复算一致(12 非零槽逐词等)")
PYEOF
python3 tools/gen_inc.py >/dev/null
gcc -m32 -O0 -g -no-pie -I host -o host/build/fwd_test tools/fwd_test.c host/fwd.c || { note "构建失败"; exit 1; }
[ -s /tmp/h55_ctx.txt ] || { echo "缺 ctx 清单(先跑 host55)"; exit 1; }
FWDVECT=/home/gill/bleem/.analysis/golden/rootcall_vec_5s.bin FWDLIST=/tmp/h55_ctx.txt timeout 300 ./host/build/fwd_test > /tmp/h56.log 2>&1
s=$(grep "^FWDSUM" /tmp/h56.log | tail -1)
if echo "$s" | grep -q "ok=2 ctx=22 miss=0"; then note "B) FWDVECT+EXN2 修正后 ctx 转化恒定: $s"; else note "B) FAIL $s"; fail=1; fi
if grep -q "逐词一致" docs/52_batch2_falsified.md && grep -q "撤回" docs/52_batch2_falsified.md && grep -q "并入批3" docs/52_batch2_falsified.md; then note "C) docs/52 锚齐"; else note "C) 缺锚"; fail=1; fi
d=$(ls docs/*.md | wc -l); if grep -q "52_batch2_falsified" README.md && [ "$d" -eq 59 ]; then note "D) README 与 docs=$d 一致"; else note "D) FAIL docs=$d"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST56 CHECK PASS" || echo "HOST56 CHECK FAIL"
[ $fail -eq 0 ]
