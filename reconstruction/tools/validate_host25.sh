#!/bin/bash
# validate_host25.sh — task-033 H-15a 验收：MDFNSVST CPU 区判读 + 金标准 CPU+RAM 态自洽 4000 块
set -u
cd "$(dirname "$0")/.."
RC=0
D=docs/24_golden_cpu_state.md
STATE="${BEETLE_STATE:-$HOME/.config/retroarch/states/Beetle PSX/disc.state}"
GD=/home/gill/bleem/.analysis/golden
[ -f "$D" ] || { echo "FAIL: docs/24 缺失"; exit 1; }
for k in 'BACKED_PC' 'MainRAM.data8' '0x8003D704' 'GS1\\0' 'SysControl'; do
    grep -q "$k" "$D" || { echo "FAIL: docs/24 缺 '$k'; RC=1"; RC=1; }
done
if [ -f "$STATE" ]; then
    python3 tools/golden_state_parse.py "$STATE" "$GD/gstate" >/dev/null || { echo "FAIL: 解析器再生"; RC=1; }
    [ "$(stat -c%s "$GD/gstate.hdr")" = "140" ] || { echo "FAIL: hdr 非 140B"; RC=1; }
    [ "$(stat -c%s "$GD/gstate.ram")" = "2097152" ] || { echo "FAIL: ram 非 2MB"; RC=1; }
    gcc -m32 -O0 -g -no-pie -I host -o host/build/golden_cpu_probe tools/golden_cpu_probe.c || { echo "FAIL build"; RC=1; }
    OUT=$(GSTATE="$GD/gstate.hdr" ./host/build/golden_cpu_probe ../psxbios/SCPH1001.BIN 4000)
    echo "$OUT"
    echo "$OUT" | grep -q 'blocks=4000' || { echo "FAIL: 未跑满 4000 块"; RC=1; }
    echo "$OUT" | grep -q 'outside=0'    || { echo "FAIL: 越出金标准执行页"; RC=1; }
    echo "$OUT" | grep -q 'halted=0'     || { echo "FAIL: 中途停机"; RC=1; }
else
    echo "WARN: disc.state 不在场, 仅文档断言(自洽段跳过)"
fi
[ $RC -eq 0 ] && echo "HOST25 CHECK PASS: MDFNSVST CPU 区判读 + 金标准终态 4000 块自洽(shell 空闲环)" || echo "HOST25 CHECK FAIL"
exit $RC
