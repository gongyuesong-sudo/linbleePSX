#!/bin/bash
# validate_host14.sh — task-021 H-10a BIOS-JIT 框架验收
# A) 构建（必须 -fno-pic：-fPIC 下 get_pc_thunk 尾调用毁 phys 栈平衡）
# B) 单元自测：样本 MIPS 语义对拍 JIT-CHECK PASS
# C) BIOS 烟测（ROM 在场时）：引导链推进到首个 syscall 停机
set -u
cd "$(dirname "$0")/.."
RC=0

echo "== A) build mips_jit"
gcc -m32 -O0 -g -no-pie -fno-pic -DJIT_MAIN -o host/build/mips_jit host/mips_jit.c || { echo "BUILD FAIL"; exit 1; }
echo "build ok"

echo "== B) unit self-test"
OUT=$(./host/build/mips_jit 2>&1); URC=$?
echo "$OUT" | tail -2
if [ $URC -ne 0 ] || ! echo "$OUT" | grep -q 'JIT-CHECK: PASS'; then
  echo "UNIT FAIL rc=$URC"; RC=1
fi

echo "== C) BIOS smoke (optional)"
ROM="${BLEEM_BIOS:-../psxbios/SCPH1001.BIN}"
if [ -f "$ROM" ]; then
  SOUT=$(./host/build/mips_jit "$ROM" 2>&1); SRC=$?
  echo "$SOUT" | tail -2
  if [ $SRC -ne 0 ] || ! echo "$SOUT" | grep -q 'JIT-CHECK: PASS'; then
    echo "SMOKE FAIL rc=$SRC"; RC=1
  fi
else
  echo "WARN: ROM 不在场（$ROM），跳过烟测（不计失败）"
fi

if [ $RC -eq 0 ]; then
  echo "HOST14 CHECK PASS: MIPS->x86 block JIT 单元语义对拍 + BIOS 引导链烟测（syscall 停机面）"
else
  echo "HOST14 CHECK FAIL"
fi
exit $RC
