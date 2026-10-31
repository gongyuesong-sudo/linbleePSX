#!/bin/bash
# validate_host26.sh — task-034 H-15b 验收：金标准终态 serve 活性 + printf 注入达 stdout 设备派发面(lowrd>0)
set -u
cd "$(dirname "$0")/.."
RC=0
D=docs/25_golden_serve_inject.md
GD=/home/gill/bleem/.analysis/golden
[ -f "$D" ] || { echo "FAIL: docs/25 缺失"; exit 1; }
for k in 'H_EXNVEC' '纯 RAM 轮询环' 'strcpy' 'lowrd'; do
    grep -q "$k" "$D" || { echo "FAIL: docs/25 缺 '$k'"; RC=1; }
done
gcc -m32 -O0 -g -no-pie -I host -o host/build/golden_cpu_probe tools/golden_cpu_probe.c || { echo "FAIL build"; RC=1; }
[ -f "$GD/gstate.hdr" ] || { echo "FAIL: gstate 产物缺失(先跑 golden_state_parse.py)"; exit 1; }
R1=$(GSTATE=$GD/gstate.hdr ./host/build/golden_cpu_probe ../psxbios/SCPH1001.BIN 4000); echo "$R1"
echo "$R1" | grep -q 'blocks=4000' || { echo "FAIL: 自洽段"; RC=1; }
R2=$(H_EXNVEC=1 GSTATE=$GD/gstate.hdr ./host/build/golden_cpu_probe ../psxbios/SCPH1001.BIN 20000); echo "$R2"
echo "$R2" | grep -q 'fails=0' || { echo "FAIL: serve 活性段"; RC=1; }
R3=$(GSTATE=$GD/gstate.hdr INJ=1 ./host/build/golden_cpu_probe ../psxbios/SCPH1001.BIN 200000); echo "$R3"
echo "$R3" | grep -qE 'lowrd=[1-9]' || { echo "FAIL: 注入未达 stdout 设备派发面(lowrd=0)"; RC=1; }
[ $RC -eq 0 ] && echo "HOST26 CHECK PASS: 后引导活性 + 注入达设备派发面" || echo "HOST26 CHECK FAIL"
exit $RC
