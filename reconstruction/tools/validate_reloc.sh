#!/bin/bash
# validate_reloc.sh — task-001 静态重定位复现验证
set -e
cd "$(dirname "$0")/.."
python3 tools/apply_reloc.py
test -s decrypted/stream_10a4c_L71cbf_relocated.bin
python3 - <<'EOF'
import json, struct
rep=json.load(open('decrypted/reloc_report.json'))
assert rep['n_applied']==2, 'expected 2 fixup slots, got %d' % rep['n_applied']
img=open('decrypted/stream_10a4c_L71cbf_relocated.bin','rb').read()
orig=open('decrypted/stream_10a4c_L71cbf.bin','rb').read()
assert len(img)==len(orig), 'size drift'
diff=[i for i in range(len(img)) if img[i]!=orig[i]]
slots={int(e['slot'],16) for e in rep['applied']}
assert slots == {0x38427, 0x4e3c7}, 'slot set drift: %s' % [hex(x) for x in slots]
for i in diff:
    assert any(sl<=i<sl+4 for sl in slots), 'byte %d outside slots' % i
# every recorded slot actually changed value to its arena addr
for e in rep['applied']:
    assert struct.unpack_from('<I', img, int(e['slot'],16))[0] != struct.unpack_from('<I', orig, int(e['slot'],16))[0], 'slot unchanged '+e['slot']
# self-healing invariant: applied slots positive & in arena; no stub-range refs left
for e in rep['applied']:
    v=struct.unpack_from('<I', img, int(e['slot'],16))[0]
    assert 0x2a1000 <= v < 0x2d1000, 'addr %08x not in arena' % v
left=[i for i in range(0,len(img)-3) if 0x455338<=struct.unpack_from('<I',img,i)[0]<0x455df4]
assert not left, 'unresolved fixers remain'
print('reloc applied: 2/2 slots, 8 bytes, arena addrs OK, zero unresolved')
EOF
test -s docs/08_api_dispatch.md   # mechanism doc referenced
echo "RELOC CHECK PASS"
