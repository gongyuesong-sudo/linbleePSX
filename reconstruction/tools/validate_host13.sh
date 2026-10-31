#!/usr/bin/env bash
# validate_host13.sh — task-020 validator: MIPS 墙画像在案
set -u
cd "$(dirname "$0")/.."
fail(){ echo "FAIL: $*"; exit 1; }
ROM=/home/gill/bleem/psxbios/SCPH1001.BIN
test -f "$ROM" || fail "SCPH1001 ROM missing"
out=$(timeout 120 python3 tools/mips_wall.py "$ROM") || fail "mips_wall.py crashed"
echo "$out" | grep -q '^RESULT=OK$' || fail "mips_wall RESULT not OK"
echo "$out" | grep -q '^unknown_count=0$' || fail "boot-chain unknown != 0"
for mn in jal jr beq lw sw; do echo "$out" | grep -q "$mn" || fail "mnemonic $mn missing from profile"; done
test -s docs/13_mips_wall.md || fail "docs/13 missing"
grep -q '最小指令集' docs/13_mips_wall.md || fail "docs/13 min-iset section missing"
grep -q 'lwl' docs/13_mips_wall.md || fail "docs/13 unaligned-LW handling missing"
grep -q '块级按需' docs/13_mips_wall.md || fail "docs/13 on-demand-block JIT strategy missing"
echo "HOST13 CHECK PASS: MIPS boot-chain profile (60k steps, 0 unknown) + min-ISA list + block-JIT strategy documented"
