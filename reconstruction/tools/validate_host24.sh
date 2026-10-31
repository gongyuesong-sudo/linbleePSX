#!/bin/bash
# validate_host24.sh — task-032 H-14 验收：深段 thunk ABI 反汇编断言 + docs/20 落档
set -u
cd "$(dirname "$0")/.."
RC=0
IMG=decrypted/stream_10a4c_L71cbf_relocated.bin
D=docs/20_deep_thunk_abi.md
[ -f "$D" ] || { echo "FAIL: docs/20 缺失"; exit 1; }
for k in '0x174' '0x400108' '0x1000a' '0x328400' '0x2a000d'; do
    grep -q "$k" "$D" || { echo "FAIL: docs/20 缺 '$k'"; RC=1; }
done
OBJ=$(objdump -D --target=binary -m i386 --adjust-vma=0x400000 --start-address=0x4002b8 --stop-address=0x4002d4 "$IMG" 2>/dev/null)
echo "$OBJ" | grep -q 'call   \*0x174(%edi)' || { echo "FAIL: thunk 分发槽 call *0x174(%edi) 不在场"; RC=1; }
echo "$OBJ" | grep -q '0x328400'            || { echo "FAIL: 0x328400 常量不在场"; RC=1; }
echo "$OBJ" | grep -q 'jmp    0x400108'     || { echo "FAIL: tail dispatcher jmp 不在场"; RC=1; }
DIS=$(objdump -D --target=binary -m i386 --adjust-vma=0x400000 --start-address=0x400108 --stop-address=0x400170 "$IMG" 2>/dev/null)
echo "$DIS" | grep -q 'push   \$0x1000a'    || { echo "FAIL: dispatcher API 常量 0x1000a 不在场"; RC=1; }
echo "$DIS" | grep -q 'call   \*0x4022a4'   || { echo "FAIL: 全局 API 指针 [0x4022a4] 不在场"; RC=1; }
n=$(echo "$DIS" | grep -c 'call   \*0x4022a4')
[ "$n" -eq 4 ] || { echo "FAIL: 四槽清理协议 call 数=$n != 4"; RC=1; }
[ $RC -eq 0 ] && echo "HOST24 CHECK PASS: 深段 thunk(ctx+0x174 分发槽)+tail dispatcher(四槽/0x1000a/[0x4022a4]) 反汇编实证落档" || echo "HOST24 CHECK FAIL"
exit $RC
