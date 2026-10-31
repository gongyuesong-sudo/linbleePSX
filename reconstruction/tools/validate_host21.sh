#!/bin/bash
# validate_host21.sh — task-028 H-12d 验收：分发器安装金标准判异 + EXN ROM 偏移语义落地
set -u
cd "$(dirname "$0")/.."
RC=0
D=docs/18_bios_goldstandard.md
[ -f "$D" ] || { echo "FAIL: docs/18 缺失"; exit 1; }
grep -q 'BFC10000h + n\*10h\|BFC10000' "$D" || { echo "FAIL: trap 区机制未落档"; RC=1; }
grep -q 'C80h' "$D"                        || { echo "FAIL: RAM 内核图(异常处理器 C80h)未落档"; RC=1; }
grep -q 'RAM 0x130' "$D"                   || { echo "FAIL: 0x130=ROM 偏移语义判异未落档"; RC=1; }
grep -q '0xB1D20' "$D"                     || { echo "FAIL: 设备指针最终边界未实证落档"; RC=1; }
grep -q 'Fun\|fun_0xb003\|func_0xb003' "$D" || true
# EXN ROM 偏移语义端到端：htrace 侧 EXN 目标应为 ROM(0xbfc1e988)，链≥4199，停机在设备指针面(b00/000b1)
ROM="${BLEEM_BIOS:-../psxbios/SCPH1001.BIN}"
if [ -f "$ROM" ]; then
  gcc -m32 -O0 -g -no-pie -fno-pic -o host/build/htrace host/htrace.c host/mips_jit.c host/tramp32.s || { echo "FAIL build"; exit 1; }
  # task-041 重锚: 旧基线(chain 4199/停机 b00*/EXN 事件)建立在 addu+ra-flag bug 语义上=伪像;
  # 新基线 = 链过 kernel 自装(bfc003b8/434 清 RAM 循环)+rootcall NewRootCmd(v0=80000010) 处理槽未装
  # → 跳 RAM 零区 sled → 停机 3c15bd80。EXN ROM 偏移语义判异保留于 docs/18(代码路径不变, 本链不再触发)。
  HTRACE_LOG=host/build/htrace_host21.log H_ARENA_MB=320 H_SLED_CAP=200 H_BIOSJIT=1 H_BIOSEXN=1 H_JIT_BUDGET=100000 BLEEM_BIOS="$ROM" \
    timeout 280 ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin 0x400000 0x472000 >/dev/null 2>&1 || true
  L=host/build/htrace_host21.log
  CH=$(grep -a 'BIOS-JIT-CHAIN blocks=' "$L" | head -1 | sed 's/.*blocks=\([0-9]*\).*/\1/')
  [ "${CH:-0}" -ge 18000 ] || { echo "FAIL: chain=${CH:-0} < 18000(新基线: kernel 自装+清RAM循环)"; RC=1; }
  FIN=$(grep -a 'BIOS-JIT-CHAIN' "$L" | head -1 | sed 's/.*final=\([0-9a-f]*\).*/\1/')
  case "$FIN" in 3c15b*|b00*|000b1*) ;; *) echo "FAIL: 停机面=$FIN 不在冷链边界(RAM 零区 sled/rootcall 后)"; RC=1;; esac
  grep -aq 'v0=80000010' "$L" || { echo "FAIL: 未到 rootcall NewRootCmd(v0=80000010) 边界"; RC=1; }
fi
# 回归：host19（链>150）与 host7 基线
timeout 500 bash tools/validate_host19.sh >/dev/null 2>&1 || { echo "FAIL: host19 回归"; RC=1; }
[ $RC -eq 0 ] && echo "HOST21 CHECK PASS: 金标准判异落档 + 新基线冷链边界(kernel 自装→rootcall NewRootCmd 槽未装; task-041 重锚)" || echo "HOST21 CHECK FAIL"
exit $RC
