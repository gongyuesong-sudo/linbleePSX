#!/usr/bin/env bash
# validate_host7.sh — task-014 validator:
#   全 img 无界 trace 证据在案：RANGE 行存在、img 唯一 eip=131、max=0x4002b7、深段=0
#   + docs/10 覆盖实证文档（含 154 函数前提证伪与路线影响）
set -u
cd "$(dirname "$0")/.."
fail(){ echo "FAIL: $*"; exit 1; }

test -s host/build/htrace.log || fail "htrace.log empty"
grep -q 'RANGE 00400000..00472000' host/build/htrace.log || fail "full-image RANGE line missing (re-run htrace with 0x400000 0x472000)"

u=$(grep -E '^004' host/build/htrace.log | cut -c1-8 | sort -u | wc -l)
[ "$u" -eq 131 ] || fail "unique img eips = $u (expected 131)"
mx=$(grep -E '^004' host/build/htrace.log | cut -c1-8 | sort -u | tail -1)
[ "$mx" = "004002b7" ] || fail "max img eip = $mx (expected 004002b7)"
deep=$(grep -E '^004' host/build/htrace.log | cut -c1-8 | sort -u | awk '$0>"004002b7"' | wc -l)
[ "$deep" -eq 0 ] || fail "deep img eips = $deep (expected 0)"

test -s docs/10_jit_templates.md || fail "docs/10 missing"
grep -q '154' docs/10_jit_templates.md || fail "docs/10 must address the falsified 154-function premise"
grep -q 'DuckStation' docs/10_jit_templates.md || fail "docs/10 route impact missing"
echo "HOST7 CHECK PASS: full-image coverage proof (131 eips, max 0x4002b7, zero deep) + falsification documented"
