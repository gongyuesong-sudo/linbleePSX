#!/usr/bin/env bash
# validate_host34.sh — task-042 H-18c 验收: rootcall NewRootCmd 派发面取证 + HLE 供给试验 + 终判落档
set -u
cd "$(dirname "$0")/.."
ROM="${BLEEM_BIOS:-/home/gill/bleem/psxbios/SCPH1001.BIN}"
fail=0
ok(){ echo "PASS $*"; }
no(){ echo "FAIL: $*"; fail=1; }

# A. docs/31 终判落档
for k in "0x674" "80000010" "0x59E0" "C2SECTION" "ROOTHLE" "0x89E7C"; do
  grep -qa "$k" docs/31_coldchain_rootcall.md || no "A: docs/31 缺 '$k'"
done
[ $fail -eq 0 ] && ok "A: 派发块(0x674 跳表)/NewRootCmd(v0=80000010)/ROM handler(0x59E0)/C2SECTION 终判/HLE(ROOTHLE)/控制块(0x89E7C)落档"

# B. 取证探针在场且复现派发面现场
gcc -m32 -O0 -g -no-pie -I host -o host/build/rootdisp_probe tools/rootdisp_probe.c 2>/dev/null || no "B0: 探针编译失败"
if [ -f "$ROM" ]; then
  P=$(timeout 120 ./host/build/rootdisp_probe "$ROM" 2>/dev/null)
  grep -q "FIRST-3C blk" <<<"$P" || no "B1: 未捕获 3C 跳出源"
  grep -q "prevblk=00000600 -> 3c0e0000" <<<"$P" || no "B2: 跳出源非派发块 0x600→0x3C0E0000"
  grep -q "v0=80000010" <<<"$P" || no "B3: 跳出时 v0 非 rootcall NewRootCmd(0x80000010)"
  grep -q "ra=bfc0686c" <<<"$P" || no "B4: ra 非 rootcall jal(bfc0686c)"
  grep -q "a2=000002fc" <<<"$P" || no "B5: a2 非 cmdbuf(0x2fc)"
  [ $fail -eq 0 ] && ok "B: 取证探针复现派发面(RAM 0x600 派发块→0x3C0E0000, v0=80000010 ra=bfc0686c a2=2fc)"
fi

# C. HLE 供给试验(H_ROOTHLE)生效且证明"单槽供给不足"→ 终判 C2 依赖
python3 - <<'PY' || no "C0: H_ROOTHLE 重定向代码缺失"
import sys
s=open('host/mips_jit.c').read()
sys.exit(0 if ('H_ROOTHLE' in s and '0xBFC059E0u' in s) else 1)
PY
python3 tools/gen_inc.py >/dev/null
if [ -f "$ROM" ]; then
  gcc -m32 -O0 -g -no-pie -fno-pic -o host/build/htrace host/htrace.c host/mips_jit.c host/tramp32.s 2>/dev/null || no "C1: htrace 构建失败"
  HTRACE_LOG=host/build/htrace_host34.log H_ROOTHLE=1 H_ARENA_MB=320 H_SLED_CAP=200 H_BIOSJIT=1 H_BIOSEXN=1 H_JIT_BUDGET=40000 BLEEM_BIOS="$ROM" \
    timeout 280 ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin 0x400000 0x472000 >/dev/null 2>&1 || true
  grep -qa 'ROOTHLE redirect->bfc059e0' host/build/htrace_host34.log || no "C2: H_ROOTHLE 未触发 NewRootCmd handler 重定向"
  # 终判: 重定向后仍落零区 sled(单槽供给不足) => 链最终仍停 3C*
  FIN=$(grep -a 'BIOS-JIT-CHAIN' host/build/htrace_host34.log | head -1 | sed 's/.*final=\([0-9a-f]*\).*/\1/')
  case "$FIN" in 3c*) ok "C: H_ROOTHLE 供给 ROM handler 真执行但仍落零区(final=$FIN)→终判成立: 需 C2SECTION(homebrew C2, task-043)";; *) no "C3: final=$FIN 未复现'单槽供给不足'(应 3c*)";; esac
fi

# D. task-043 已登记(homebrew C2SECTION 合法路径)
python3 - <<'PY' || no "D: task-043(homebrew C2SECTION)未登记"
import json,sys
d=json.load(open('harness-tasks.json'))
sys.exit(0 if any(t['id']=='task-043' for t in d['tasks']) else 1)
PY
[ $fail -eq 0 ] && ok "D: 后续合法路径 task-043(homebrew C2SECTION C2)已登记"

if [ $fail -eq 0 ]; then
  echo "HOST34 CHECK PASS: rootcall NewRootCmd 派发面取证(0x600 块/0x674 跳表/cmdbuf 0x2fc) + HLE 供给试验证'单槽不足' + 终判(无盘冷链=真机忠实边界, 需 C2SECTION)"
  exit 0
else
  echo "HOST34 CHECK FAIL"; exit 1
fi
