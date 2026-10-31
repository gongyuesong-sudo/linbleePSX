#!/usr/bin/env bash
# validate_host12.sh — task-019 validator:
#   H-9a VA 供给：①双段 arena 落地（140MB 请求不再 -1）②实盘 ROM 深段推进证明：
#   deep(>0x4002b7) 唯一 eip 数 > 4 且 max > 0x4002cf（任务验收标准）
set -u
cd "$(dirname "$0")/.."
fail(){ echo "FAIL: $*"; exit 1; }
test -x host/build/htrace || gcc -m32 -O0 -g -no-pie -o host/build/htrace host/htrace.c host/tramp32.s || fail build
ROM=/home/gill/bleem/psxbios/SCPH1001.BIN
test -f "$ROM" || fail "real ROM missing"

H_ARENA_MB=320 H_SLED_CAP=3000 BLEEM_BIOS="$ROM" timeout 280 ./host/build/htrace \
    decrypted/stream_10a4c_L71cbf_relocated.bin 0x400000 0x472000 >/dev/null 2>&1 || true

grep -E '^004' host/build/htrace.log | cut -c1-8 | sort -u > /tmp/h12_eips.$$
deep=$(awk '$0>"004002b7"' /tmp/h12_eips.$$ | wc -l)
mx=$(tail -1 /tmp/h12_eips.$$)
exhaust=$(grep -c 'out=ffffffff' host/build/htrace.log)
rm -f /tmp/h12_eips.$$
echo "deep=$deep max=$mx alloc_fail_returns=$exhaust"
[ "$exhaust" = "0" ] || fail "arena still returning -1 ($exhaust times) — VA supply not fixed"
[ "$deep" -gt 4 ] || fail "deep eips=$deep (must exceed 4 — baseline H-8b)"
[ "$mx" \> "004002cf" ] || fail "max img eip=$mx (must exceed 004002cf baseline)"
echo "HOST12 CHECK PASS: VA supply closed (-1 eliminated) + deep segment advanced past H-8b baseline"
