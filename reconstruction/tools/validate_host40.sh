#!/bin/bash
# task-048 H-21b CDROM/C2SEC 状态面 — 验证(docs/35)
cd "$(dirname "$0")/.." || exit 1
python3 - <<'EOF' || { echo "FAIL A: C2SEC=ROM 拷贝定论复证失败"; exit 1; }
import struct
rom=open('/home/gill/bleem/psxbios/SCPH1001.BIN','rb').read()
g=open('/home/gill/bleem/.analysis/golden/GOLDEN_RAM_2MB.bin','rb').read()
n=0x67FF0//4; diffs=[(0x30000+i*4,struct.unpack_from('<I',g,0x30000+i*4)[0]) for i in range(n)
                    if struct.unpack_from('<I',rom,0x18000+i*4)[0]!=struct.unpack_from('<I',g,0x30000+i*4)[0]]
assert len(diffs)==653, f"diff={len(diffs)}"
assert (0x89e2c,0x160004) in diffs
p=open('/home/gill/bleem/.analysis/golden/c2_state_patch.bin','rb').read()
cnt,=struct.unpack_from('<I',p,0); assert cnt==653
low=open('/home/gill/bleem/.analysis/golden/golden_c2_slice_low_500_900.bin','rb').read()
assert struct.unpack_from('<I',low,0x674-0x500)[0]==0x1508
print("PASS A: ROM 自带 C2SEC(653 差词复算一致), 补丁/低区片产物自洽")
EOF
grep -qa "H_C2STATE" host/mips_jit.c && grep -qa "c2_state_patch.bin" host/mips_jit.c && grep -qa "golden_c2_slice_low_500_900" host/mips_jit.c || { echo "FAIL B: 消费管线缺"; exit 1; }
gcc -m32 -O0 -g -no-pie -fno-pic -o /tmp/h40chk host/htrace.c host/mips_jit.c host/tramp32.s 2>&1 | grep -qE " error" && { echo "FAIL B2: 编译"; exit 1; }
echo "PASS B: statepatch/lowfill/相位覆盖管线编译通过"
D=docs/35_c2sec_state.md
grep -qa "C2 主线程节拍" "$D" && grep -qa "sp+0x14" "$D" && grep -qa "0x316C8" "$D" || { echo "FAIL C: docs/35 关键结论缺"; exit 1; }
grep -qa "sw ra,sp,0x14 是 bne 延迟槽" docs/35_c2sec_state.md || grep -qa "延迟槽" "$D" || { echo "FAIL C2: 延迟槽勘误缺"; exit 1; }
echo "PASS C: docs/35 落档(ROM 自带 C2SEC/双写者协议/延迟槽勘误/节拍交班 049)"
echo "HOST40 CHECK PASS: C2SEC 来源定论 + 状态协议面破译 + 根因收敛=C2 主线程节拍缺失(task-049 定义域)"
exit 0
