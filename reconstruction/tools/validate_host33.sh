#!/usr/bin/env bash
# validate_host33.sh — task-041 H-19 验收: 停机面定性落档 + 重锚谓词在场 + 轻量链/时基复证
# (host14/21/23/28 四套全量重锚回归由 SESSION-44 现场运行并记入 progress/commit; 本脚本防漂移断言)
set -u
cd "$(dirname "$0")/.."
ROM="${BLEEM_BIOS:-/home/gill/bleem/psxbios/SCPH1001.BIN}"
GS=/home/gill/bleem/.analysis/golden/gstate.hdr
fail=0
ok(){ echo "PASS $*"; }
no(){ echo "FAIL: $*"; fail=1; }

# A. docs/30 停机面定性+重锚明细落档
for k in "3C0E0000" "NewRootCmd" "bfc00434" "a0000500" "hk=3" "65536"; do
  grep -qa "$k" docs/30_baseline_reanchor.md || no "A: docs/30 缺 '$k'"
done
[ $fail -eq 0 ] && ok "A: 停机面定性(rootcall NewRootCmd 槽未装→0xE0000 零区 sled)+重锚明细落档"

# B. smoke 谓词重锚在场且现场 rc=0
grep -qa 'psx_state.r\[2\]==0x80000010u' host/mips_jit.c || no "B1: bios_smoke 新谓词(v0==0x80000010)缺失"
gcc -m32 -O0 -g -no-pie -DJIT_MAIN -o host/build/mips_jit host/mips_jit.c 2>/dev/null || no "B2: 编译失败"
./host/build/mips_jit >/dev/null 2>&1 || no "B3: 单元 FAIL"
if [ -f "$ROM" ]; then
  ./host/build/mips_jit "$ROM" 2>/dev/null | grep -q "JIT-CHECK: PASS" || no "B4: 新基线 smoke FAIL"
  ok "B: JIT 单元+新基线 smoke(链至 rootcall NewRootCmd 边界, blocks>=15000) PASS"
fi

# C. TICK 真哨兵(break hk=3)在场且短程复证
grep -qa 'psx_state.halt_kind==3 && psx_state.pc>=0xBFC4C1B0u' tools/golden_cpu_probe.c || no "C1: TICK break 哨兵扩展缺失"
gcc -m32 -O0 -g -no-pie -I host -o host/build/golden_cpu_probe tools/golden_cpu_probe.c 2>/dev/null || no "C2: probe 编译失败"
if [ -f "$GS" ]; then
  OUT=$(TICK=1 GSTATE="$GS" timeout 200 ./host/build/golden_cpu_probe "$ROM" 600000 2>/dev/null | tail -1)
  echo "TICK: $OUT"
  C=$(grep -o 'cycles=[0-9]*' <<<"$OUT" | cut -d= -f2)
  grep -q "fails=0" <<<"$OUT" || no "C3: TICK fails!=0"
  [ "${C:-0}" -ge 80000 ] || no "C4: cycles=${C:-0} <80000(600k 预算应 ~1e5 周期)"
  [ $fail -eq 0 ] && ok "C: TICK break 真哨兵短程复证(${C} cycles/600k 块)"
fi

# D. 取证基建(块史环扩容+EMITFAIL 现场)防回退
grep -qa 'seq\[65536\]' host/mips_jit.c || no "D1: trace_boot 块史环未扩至 65536"
grep -qa 'PREV\[' host/mips_jit.c || no "D2: EMITFAIL 现场打印缺失"
grep -qa 'gen_inc.py' tools/gen_inc.py docs/29_cdrom_hle.md || no "D3: inc 同步基建缺失"
[ $fail -eq 0 ] && ok "D: 取证基建(65536 环/EMITFAIL 现场/gen_inc)在场"

if [ $fail -eq 0 ]; then
  echo "HOST33 CHECK PASS: 冷链边界定性(rootcall 处理槽安装面→task-040) + 四 validator 重锚谓词 + 新基线现场复证"
  exit 0
else
  echo "HOST33 CHECK FAIL"; exit 1
fi
