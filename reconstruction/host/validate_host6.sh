#!/usr/bin/env bash
# validate_host6.sh — task-013 validator:
#   1. 生产宿主在 BIOS/HLE stub 下 init 干净返回（signal<=1，非 2/3/4）
#   2. stub-sled 处理点已观测（sites 记录或 bios-sled 日志存在）
#   3. htrace 深度证据：引擎在 stub BIOS 下执行超过 100000 条 trace 记录（非错误早退）
#   4. docs/09 §7.2：BIOS ROM 不可伪造结论 + HLE 可行性证据在案
set -u
cd "$(dirname "$0")/.."
fail(){ echo "FAIL: $*"; exit 1; }

timeout 200 ./host/build/bleem_host decrypted/stream_10a4c_L71cbf_relocated.bin > host/build/host_run.txt 2>&1 || true
grep -Eq 'H1 PASS \(signal=[01]\)' host/build/host_run.txt || fail "host init not clean"
grep -Eq 'stub-sled|stub sled' host/build/host_run.txt || test -s host/build/stsites.txt || fail "no stub-sled observation"

test -s host/build/htrace.log || fail "htrace.log empty"
n=$(wc -l < host/build/htrace.log)
[ "$n" -gt 100000 ] || fail "trace volume too low ($n <= 100000)"
m=$(grep -cE '^004' host/build/htrace.log)
[ "$m" -gt 200 ] || fail "engine-range instruction records too low ($m <= 200)"

grep -q '7.2 H-5b' docs/09_engine_hosting.md || fail "docs/09 section 7.2 missing"
grep -q 'BLEEM_BIOS' docs/09_engine_hosting.md || fail "BIOS ROM requirement not documented"
grep -q '162,658' docs/09_engine_hosting.md || fail "depth evidence not documented"
echo "HOST6 CHECK PASS: HLE-BIOS stub tolerated; trace ${n} lines (${m} engine-range); BIOS-ROM requirement documented"
