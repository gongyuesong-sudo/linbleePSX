#!/usr/bin/env bash
# validate_host5.sh — task-012 validator:
#   blocks.txt 存在且含控制块 pre/post diff 行（engine_written 计数实证）
#   + docs/09 修正记录在案（零写入结论 + H-5 修订路径）
set -u
cd "$(dirname "$0")/.."
fail(){ echo "FAIL: $*"; exit 1; }
test -s host/build/blocks.txt || fail "blocks.txt empty"
grep -q 'CTRL-BLK 0x3e1000' host/build/blocks.txt || fail "control-block diff line missing"
grep -Eq 'engine_written=[0-9]+' host/build/blocks.txt || fail "engine_written count missing"
grep -q '修正' docs/09_engine_hosting.md || fail "docs/09 correction section missing"
grep -q 'BLEEM_BIOS' docs/09_engine_hosting.md || fail "H-5 revised BIOS-ROM path missing"
grep -q 'init 阶段引擎不写任何分配块' docs/09_engine_hosting.md || fail "zero-write conclusion not documented"
echo "HOST5 CHECK PASS: block-write instrumentation live; zero-write proof recorded; H-5 path revised"
