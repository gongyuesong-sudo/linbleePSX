#!/usr/bin/env bash
# validate_host11.sh — task-018 validator: H-8b 真实 BIOS 深执行观测（route B'）
#   A) 构建 htrace（-m32）
#   B) 真实 ROM 深跑：BLEEM_BIOS=SCPH1001.BIN 全 img trace → htrace_deep.log
#   C) 验收：htrace_deep.log 含 DEEP 行；img eip 越过 0x4002b7（深段 eip>0）；
#      BIOS-MIPS-ENTRY ≥1；bios_sha256.txt 与 ROM 实际 SHA-256 一致；
#      htrace_deep.log 内 BIOS-LOADED sha 与 ROM 一致
set -u
cd "$(dirname "$0")/.."
fail(){ echo "FAIL: $*"; exit 1; }
ROM="${BLEEM_BIOS:-/home/gill/bleem/psxbios/SCPH1001.BIN}"
IMG="decrypted/stream_10a4c_L71cbf_relocated.bin"
[ -f "$ROM" ] || fail "real ROM missing: $ROM"
[ "$(stat -c%s "$ROM")" = "524288" ] || fail "ROM size != 524288"

gcc -m32 -O0 -g -no-pie -o host/build/htrace host/htrace.c host/tramp32.s || fail "htrace build"
echo "A build PASS"

rm -f host/build/bios_sha256.txt host/build/htrace_deep.log
BLEEM_BIOS="$ROM" H_SLED_CAP=300 timeout 300 ./host/build/htrace "$IMG" 0x400000 0x472000 >/dev/null 2>&1
rc=$?
[ "$rc" = "0" ] || [ "$rc" = "4" ] || [ "$rc" = "5" ] || fail "deep run abnormal rc=$rc (want 0/4/5 terminal)"
cp host/build/htrace.log host/build/htrace_deep.log
echo "B deep run PASS (rc=$rc, $(wc -l < host/build/htrace_deep.log) lines)"

grep -q '^DEEP eip=' host/build/htrace_deep.log || fail "no DEEP lines in htrace_deep.log"
m=$(grep -E '^004' host/build/htrace_deep.log | cut -c1-8 | sort -u | tail -1)
deepn=$(python3 -c "
import sys
mx=int('$m',16); print(1 if mx>0x4002b7 else 0)")
[ "$deepn" = "1" ] || fail "no img eip beyond 0x4002b7 (max=$m)"
nm2=$(grep -c '^BIOS-MIPS-ENTRY' host/build/htrace_deep.log)
[ "$nm2" -ge 1 ] || fail "no BIOS-MIPS-ENTRY observed"
echo "C deep observation PASS (max img eip=0x$m, BIOS-MIPS-ENTRY x$(grep -c '^BIOS-MIPS-ENTRY' host/build/htrace_deep.log), DEEP x$(grep -c '^DEEP' host/build/htrace_deep.log))"

want=$(sha256sum "$ROM" | cut -d' ' -f1)
test -s host/build/bios_sha256.txt || fail "bios_sha256.txt not registered"
grep -q "$want" host/build/bios_sha256.txt || fail "bios_sha256.txt sha mismatch"
grep -q "BIOS-LOADED real ROM @0xBFC00000 sha256=$want" host/build/htrace_deep.log || fail "htrace_deep.log BIOS-LOADED sha mismatch"
echo "D real-ROM identity PASS (SHA-256 registered + window load attested)"
echo "HOST11 CHECK PASS: deep-exec observation — real BIOS, DEEP anchors, SHA proven"
