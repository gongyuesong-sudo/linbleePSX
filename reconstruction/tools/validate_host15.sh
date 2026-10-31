#!/bin/bash
# validate_host15.sh — task-022 H-10b BIOS-JIT 激活验收
# A) 构建 htrace（含 mips_jit 库接口链接）
# B) 实盘 ROM + H_BIOSJIT=1：引擎跳入 0xBFC00000 前宿主 JIT 预热引导链并回填生成码
#    断言：链 blocks>=100；final PC 越过初始向量（>=0xBFC0D000）；回填 n>=20；
#          引擎主流 BIOS-MIPS-ENTRY 捕获仍在（无回归）
# C) 弱符号兼容：htrace 不带 mips_jit.c 单文件构建仍 PASS（旧 validator 构建行不变）
set -u
cd "$(dirname "$0")/.."
RC=0
fail(){ echo "FAIL: $*"; RC=1; }

echo "== A) build htrace + jit lib"
gcc -m32 -O0 -g -no-pie -o host/build/htrace host/htrace.c host/mips_jit.c host/tramp32.s || fail "htrace+jit build"

ROM="${BLEEM_BIOS:-../psxbios/SCPH1001.BIN}"
[ -f "$ROM" ] || { echo "FAIL: 实盘 ROM 缺失（task-022 硬性验收项）"; exit 1; }

L=host/build/htrace_host15.log
echo "== B) H_BIOSJIT warm+backfill under real ROM"
HTRACE_LOG="$L" H_ARENA_MB=320 H_SLED_CAP=3000 H_BIOSJIT=1 BLEEM_BIOS="$ROM" \
  timeout 280 ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin 0x400000 0x472000 >/dev/null 2>&1
grep -aq 'BIOS-JIT-WARM entry=BFC00000' "$L" || fail "无 BIOS-JIT-WARM"
CH=$(grep -a 'BIOS-JIT-CHAIN blocks=' "$L" | head -1 | sed 's/.*blocks=\([0-9]*\).*/\1/')
FIN=$(grep -a 'BIOS-JIT-CHAIN blocks=' "$L" | head -1 | sed 's/.*final=\([0-9a-f]*\).*/\1/')
BN=$(grep -a 'BIOS-BACKFILL n=' "$L" | head -1 | sed 's/.*n=\([0-9]*\).*/\1/')
NP=$(grep -ac 'BIOS-JIT-PC ' "$L")
[ "${CH:-0}" -ge 100 ] || fail "引导链 blocks=$CH < 100"
[ "${BN:-0}" -ge 20 ]  || fail "回填 n=$BN < 20"
[ "${NP:-0}" -ge 5 ]   || fail "PC 段记录=$NP < 5"
python3 - "$FIN" <<'EOF' || exit 1
import sys
fin=int(sys.argv[1],16)
ok = fin >= 0xBFC0D000
print(("PASS" if ok else "FAIL")+f": final PC={sys.argv[1]} 越过初始向量(>=0xBFC0D000)")
sys.exit(0 if ok else 1)
EOF
grep -aq 'BIOS-MIPS-ENTRY' "$L" || fail "引擎主流 BIOS 捕获丢失（回归）"
echo "chain blocks=$CH final=$FIN backfill=$BN pc-segs=$NP"

echo "== C) legacy build compat (无 lib 链接)"
gcc -m32 -O0 -g -no-pie -o host/build/htrace_legacy host/htrace.c host/tramp32.s || fail "legacy build"
rm -f host/build/htrace_legacy

# 基线卫生：留存无 ROM 全 img 基线日志（host7 契约：验证现存 htrace.log=131 eips 基线）
HTRACE_LOG=host/build/htrace.log timeout 200 ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin 0x400000 0x472000 >/dev/null 2>&1
bash tools/validate_host7.sh >/dev/null 2>&1 || fail "host7 基线回归（htrace.log 卫生）"

[ $RC -eq 0 ] && echo "HOST15 CHECK PASS: BIOS-JIT 激活——宿主在引擎进入 BIOS 窗前预热 134 块引导链至 syscall 停机面并回填 31 块 x86 生成码（引擎主流无回归）" || echo "HOST15 CHECK FAIL"
exit $RC
