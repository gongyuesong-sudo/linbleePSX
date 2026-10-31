#!/bin/bash
# validate_host18.sh — task-025 H-12a 验收：设备注册/rootcall 派发面静态破译落档
set -u
cd "$(dirname "$0")/.."
RC=0
D=docs/17_device_calls.md
[ -f "$D" ] || { echo "FAIL: docs/17 缺失"; exit 1; }
grep -q 'jalr \[frame+0\]' "$D"            || { echo "FAIL: dev_command_walk 帧协议未落档"; RC=1; }
grep -q '0xE988' "$D"                      || { echo "FAIL: RAM 0x130 异常处理器偏移未落档"; RC=1; }
grep -q 'cdrom:SYSTEM.CNF' "$D"            || { echo "FAIL: init_finalize CD 自动运行铁证未落档"; RC=1; }
grep -q 'RAM 0xB0 槽\|0xb0' "$D"           || { echo "FAIL: root 向量槽间接调用未落档"; RC=1; }
grep -q '设备号分配位图' "$D"               || { echo "FAIL: RAM 0x60/68 语义定案未落档"; RC=1; }
# 封装管线幂等可复跑
: > /tmp/h18empty
python3 tools/wrap_bios_elf.py /tmp/h18empty /tmp/h18.elf >/dev/null 2>&1 && { echo "FAIL: 空输入应报错"; RC=1; }
[ -f /home/gill/bleem/psxbios/SCPH1001.BIN ] && {
  python3 tools/wrap_bios_elf.py /home/gill/bleem/psxbios/SCPH1001.BIN /tmp/h18.elf >/dev/null 2>&1 \
    || { echo "FAIL: wrap_bios_elf 复跑失败"; RC=1; }
  readelf -s /tmp/h18.elf 2>/dev/null | grep -c FUNC | grep -qv '^0$' \
    || { echo "FAIL: 封装符号缺失"; RC=1; }
}
# 反编译产物含 v2 关键函数
F=/home/gill/bleem/.analysis/bios_rea/decompiled/SCPH1001_decompiled.c
[ -f "$F" ] || { echo "FAIL: 反编译产物缺失"; RC=1; }
# A 表/桩静态普查与反编译一致性
python3 tools/scan_bios_syscalls.py >/dev/null 2>&1 || { echo "FAIL: scan_bios_syscalls 复跑失败"; RC=1; }
rm -f /tmp/h18.elf
[ $RC -eq 0 ] && echo "HOST18 CHECK PASS: 设备注册面静态破译落档（帧协议/向量槽/位图语义/CD 铁证）" || echo "HOST18 CHECK FAIL"
exit $RC
