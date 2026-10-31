#!/usr/bin/env bash
# validate_host61.sh — M-14 设备语义三件+墙收敛终证(task-071, 2026-10-31)
# A) 三件语义在码: H_ISTATCLR/H_EVTGEN/H_GPUOK
# B) 0x3d 出口画像: WR 普查含 1f8010f4 且 halt=800586d0(suspend epilogue) 三件叠加逐字不变
# C) IRQ13 族零增益门: 基线与三件叠加轨迹逐字一致(转化 0)
# D) 全序恒定 ok=2 ctx=22; E) docs/57 锚(含 3→1 修正); F) README docs=58
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST61: $*"; }
python3 tools/gen_inc.py >/dev/null
gcc -m32 -O0 -g -no-pie -I host -o host/build/fwd_test tools/fwd_test.c host/fwd.c 2>&1 | grep -q error && { note "构建失败(必须零 error)"; exit 1; }
grep -q "H_ISTATCLR" host/mips_jit.c && grep -q "H_EVTGEN" host/mips_jit.c && grep -q "H_GPUOK" host/mips_jit.c && note "A) 三件语义在码" || { note "A) 缺语义"; fail=1; }
echo "b0 3d" > /tmp/h61_3d.txt
a=$(FWDLIST=/tmp/h61_3d.txt timeout 90 ./host/build/fwd_test 2>&1 | grep "^FWD vec")
b=$(FWDLIST=/tmp/h61_3d.txt H_GPUOK=1 H_ISTATCLR=1 H_EVTGEN=0x8 FWDSERVE=1 timeout 90 ./host/build/fwd_test 2>&1 | grep "^FWD vec")
echo "b0 3d" > /tmp/h61_3d2.txt
rm -f /tmp/h61_3dwr.log; FWDLIST=/tmp/h61_3d2.txt H_JITDEV=/tmp/h61_3dwr.log timeout 90 ./host/build/fwd_test >/dev/null 2>&1
if [ "$a" = "$b" ] && echo "$a" | grep -q "blocks=119220 haltpc=800586d0" && grep -q "WR 1f8010f4" /tmp/h61_3dwr.log; then note "B) 0x3d 出口=DMA/GPU 路+epilogue 墙+三件逐字不变: $a"; else note "B) FAIL a=$a b=$b"; fail=1; fi
python3 - <<'PYEOF' || fail=1
import os,subprocess
base=[];comb=[]
for line in open('/tmp/h77_irq.txt'):
    v,c=line.split()
    lf=f"/tmp/h61_one_{v}_{c}.txt"; open(lf,'w').write(line)
    e1=dict(os.environ,H_IRQTICK='1',FWDSERVE='1',FWDLIST=lf)
    e2=dict(e1,H_ISTATCLR='1',H_EVTGEN='0x8',H_GPUOK='1')
    r1=subprocess.run(['bash','-c','cd /home/gill/bleem/rewrite && timeout 90 ./host/build/fwd_test 2>&1 | grep "^FWD vec"'],env=e1,capture_output=True,text=True,timeout=120).stdout.strip()
    r2=subprocess.run(['bash','-c','cd /home/gill/bleem/rewrite && timeout 90 ./host/build/fwd_test 2>&1 | grep "^FWD vec"'],env=e2,capture_output=True,text=True,timeout=120).stdout.strip()
    base.append(r1); comb.append(r2)
    assert 'rc=0' not in r2, (v,c,r2)
assert base==comb
print("HOST61: C) IRQ13 族三件叠加轨迹逐字一致(零增益, 等待在切换出口非设备位)")
PYEOF
python3 - <<'PYEOF' || fail=1
import json,os,subprocess
rows=json.load(open('/tmp/h76_fam2.json'))
open('/tmp/h61_all.txt','w').writelines(f"{v:x} {c:x}\n" for v,c,_ in rows)
env=dict(os.environ,FWDLIST='/tmp/h61_all.txt')
o=subprocess.run(['bash','-c','cd /home/gill/bleem/rewrite && timeout 300 ./host/build/fwd_test 2>&1'],env=env,capture_output=True,text=True,timeout=340).stdout
assert 'FWDSUM ok=2 ctx=22 miss=0' in o,o[-200:]
print("HOST61: D) 全序基线恒定")
PYEOF
grep -q "顺序修正为 \*\*3 → 1\*\*" docs/57_devsem_truth.md && grep -q "切换出口钩" docs/57_devsem_truth.md && note "E) docs/57 锚齐" || { note "E) 缺锚"; fail=1; }
d=$(ls docs/*.md | wc -l); if grep -q "57_devsem_truth" README.md && [ "$d" -eq 61 ]; then note "F) README 与 docs=$d 一致"; else note "F) FAIL docs=$d"; fail=1; fi
[ $fail -eq 0 ] && echo "HOST61 CHECK PASS" || echo "HOST61 CHECK FAIL"
[ $fail -eq 0 ]
