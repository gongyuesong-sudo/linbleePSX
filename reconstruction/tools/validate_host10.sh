#!/usr/bin/env bash
# validate_host10.sh — task-017 validator: BLEEM_BIOS ROM 接线（route B'）
#   A) 构建：htrace + bleem_host（-m32）
#   B) 回归：BLEEM_BIOS 未设 → 老 HLE 路语义不变（host7 全 img 覆盖证明仍成立）
#   C) 硬失败：BLEEM_BIOS 指向缺失文件 → stderr 明确报错 + rc!=0
#   D) 合成桩 ROM（524288B）→ 载入+SHA-256 登记与 sha256sum 一致 + htrace.log 出现 BIOS-LOADED
set -u
cd "$(dirname "$0")/.."
fail(){ echo "FAIL: $*"; exit 1; }
SCRATCH="${TMPDIR:-/tmp}/h10_rom.$$"
mkdir -p "$SCRATCH" host/build
trap 'rm -rf "$SCRATCH"' EXIT

gcc -m32 -O0 -g -no-pie -o host/build/htrace host/htrace.c host/tramp32.s || fail "htrace build"
gcc -m32 -O0 -g -no-pie -o host/build/bleem_host host/host32.c host/tramp32.s || fail "host32 build"
echo "A build PASS"

# B) legacy 回归（与 task-014 相同的无 ROM 基准）
unset BLEEM_BIOS
timeout 280 ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin 0x400000 0x472000 >/dev/null 2>&1 || true
u=$(grep -E '^004' host/build/htrace.log | cut -c1-8 | sort -u | wc -l)
mx=$(grep -E '^004' host/build/htrace.log | cut -c1-8 | sort -u | tail -1)
[ "$u" = "131" ] && [ "$mx" = "004002b7" ] || fail "legacy regression drifted (u=$u max=$mx)"
echo "B legacy-path regression PASS (131 eips, max 0x4002b7)"

# C) 缺失文件硬失败
BLEEM_BIOS="$SCRATCH/nope.bin" ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin >/dev/null 2>&1
rc=$?
[ "$rc" != "0" ] || fail "missing-ROM run returned 0 (must hard-fail)"
BLEEM_BIOS="$SCRATCH/nope.bin" ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin 2>&1 >/dev/null | grep -q "BLEEM_BIOS error" || fail "missing-ROM error message absent"
echo "C missing-ROM hard-fail PASS (rc=$rc)"

# D) 合成桩 ROM：MIPS-LI 风格字图样，524288B
python3 - "$SCRATCH/stub_rom.bin" <<'PY'
import sys, struct
with open(sys.argv[1],'wb') as f:
    for i in range(0x80000//4):
        f.write(struct.pack('<I', 0x0C000000 | (i & 0x03FFFFFF)))   # MIPS jump-self 族图案
PY
rm -f host/build/bios_sha256.txt
BLEEM_BIOS="$SCRATCH/stub_rom.bin" ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin >/dev/null 2>&1 || true
test -s host/build/bios_sha256.txt || fail "bios_sha256.txt not written"
want=$(sha256sum "$SCRATCH/stub_rom.bin" | cut -d' ' -f1)
grep -q "$want" host/build/bios_sha256.txt || fail "sha256 mismatch: file=$(cut -d' ' -f1 host/build/bios_sha256.txt) want=$want"
grep -q 'BIOS-LOADED real ROM @0xBFC00000' host/build/htrace.log || fail "BIOS-LOADED window copy not observed in htrace.log"
echo "D synthetic-ROM wiring PASS (sha256 registered + window loaded)"

# E) 生产宿主 host32：ROM 载入路径同约定（硬校验 + 登记）
rm -f host/build/bios_sha256.txt
BLEEM_BIOS="$SCRATCH/stub_rom.bin" timeout 120 ./host/build/bleem_host decrypted/stream_10a4c_L71cbf_relocated.bin >/dev/null 2>&1 || true
test -s host/build/bios_sha256.txt || fail "host32 did not register bios_sha256.txt"
grep -q "$want" host/build/bios_sha256.txt || fail "host32 sha256 mismatch"
BLEEM_BIOS="$SCRATCH/nope.bin" ./host/build/bleem_host decrypted/stream_10a4c_L71cbf_relocated.bin >/dev/null 2>&1
[ "$?" != "0" ] || fail "host32 missing-ROM returned 0"
echo "E host32 production wiring PASS"
echo "HOST10 CHECK PASS: BLEEM_BIOS wiring — build/legacy/error-path/ROM-load all proven"
