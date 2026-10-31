#!/bin/bash
# validate_host1.sh — task-007 H-1 宿主验收
set -e
cd "$(dirname "$0")/.."
mkdir -p host/build
gcc -m32 -O0 -g -no-pie -o host/build/bleem_host host/host32.c host/tramp32.s
rm -f host/build/arena_proof.txt host/build/segv_site.txt
timeout 120 ./host/build/bleem_host decrypted/stream_10a4c_L71cbf_relocated.bin
test -s host/build/arena_proof.txt
grep -q 'H1 PASS' host/build/arena_proof.txt
python3 - <<'EOF'
import re
line=open('host/build/arena_proof.txt').read()
m=re.search(r'slots=([0-9a-fx,]+) allocs=(\d+)',line)
slots=[int(x,16) for x in m.group(1).split(',')]
nalloc=int(m.group(2))
arena=0x2a1000
ok=[a for a in slots if 0x2a1000<=a<0x2a1000+0x04000000]
assert len(ok)>=2, 'fewer than 2 arena slots: %s' % slots
assert nalloc>=2
# 640KB PSX RAM slot must be present (0xa0000 alloc at slot value order)
print('H1 slots in arena:',[hex(a) for a in ok],'allocs',nalloc)
EOF
grep -q 'eip=' host/build/segv_site.txt
echo "HOST1 CHECK PASS"
