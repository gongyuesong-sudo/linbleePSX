#!/bin/bash
# validate_host2b.sh — task-009 H-2b：BIOS 阶段交付验收
set -e
cd "$(dirname "$0")/.."
mkdir -p host/build
gcc -m32 -O0 -g -no-pie -o host/build/bleem_host host/host32.c host/tramp32.s
gcc -m32 -O0 -g -no-pie -o host/build/htrace   host/htrace.c   host/tramp32.s
rm -f host/build/memmap.txt host/build/host_run.txt
timeout 120 ./host/build/bleem_host decrypted/stream_10a4c_L71cbf_relocated.bin > host/build/host_run.txt 2>&1 || true
grep -q 'slot-recover eip=00060008 -> 00400194 eax=bfc00000' host/build/host_run.txt
grep -q 'H1 PASS' host/build/host_run.txt
test -s host/build/memmap.txt
python3 - <<'PY'
toks=open('host/build/memmap.txt').read().replace('\n',' ').split()
mm=dict(t.split('=',1) for t in toks if '=' in t)
assert mm['bios_base'].strip()=='bfc00000', mm['bios_base']
assert mm['psx_size'].strip()=='00800000', mm['psx_size']
assert mm['win5c'].strip()=='bfc80000'
w=[x.strip() for x in mm['window_slots'].split(',')] if 'window_slots' in mm else []
print('BIOS windows ok:', mm['win4c'], mm['win50'], mm['win5c'])
PY
# htrace: 推进深度必须越过 0x40019E（BIOS 槽写入之后）
timeout 240 ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin >/dev/null 2>&1 || true
MAX=$(grep -E '^0040' host/build/htrace.log | cut -c1-8 | sort -u | tail -1)
python3 - "$MAX" <<'PY'
import sys
m=int(sys.argv[1],16)
assert m>=0x4001c0, 'max eip %08x < 0x4001c0 — BIOS window chain incomplete' % m
print('htrace deep = %08x (BIOS window fill region passed)' % m)
PY
echo "HOST2B CHECK PASS"
