#!/bin/bash
# validate_api_tab.sh — task-006 验证：docs/08 存在 + 机制数字与镜像实测一致
set -e
cd "$(dirname "$0")/.."
test -s docs/08_api_dispatch.md
grep -q 'call dword ptr \[eax+disp8\]' docs/08_api_dispatch.md
grep -q '0x4022a4' docs/08_api_dispatch.md
python3 - <<'EOF'
import struct, collections, sys
d=open('decrypted/stream_10a4c_L71cbf.bin','rb').read()
ff15=[i for i in range(len(d)-6) if d[i:i+2]==b'\xff\x15' and struct.unpack_from('<I',d,i+2)[0]==0x4022a4]
assert len(ff15)==11, 'FF15 slot0 count %d != 11' % len(ff15)
# instruction-aware linear walk (same method as docs/08): skip disp bytes of
# matched FF /2 modrm so data-dependent FF40 pairs inside displacements don't double-count.
s=collections.Counter(); i=0; n=len(d)
while i<n-3:
    if d[i]==0xff and (d[i+1]&0xc0)!=0xc0 and (d[i+1]>>3&7)==2:
        mod=d[i+1]>>6; rm=d[i+1]&7
        dl=0 if mod==0 else (1 if mod==1 else 4)
        if rm==4: dl=4 if mod==0 else dl
        if rm==5 and mod==0: dl=4
        if (d[i+1]&0xc7)==0x40: s[d[i+2]]+=1
        i+=2+dl; continue
    i+=1
assert sum(s.values())==91, 'slot-call total %d != 91' % sum(s.values())
assert s[0x08]==19 and s[0xe8]==6, 'slot drift'
print('engine scan OK: FF15=11, [eax+disp8]=91, slots=%d' % len(s))
EOF
python3 - <<'EOF'
d=open('docs/06_decryption.md').read()
assert '8b 01 01 00' in d and 'FUN_000553CA' in d, 'docs/06 correction missing'
assert d.count('0x10068')==1, 'stale 0x10068 claims remain'
print('docs/06 correction OK')
EOF
echo "API TAB CHECK PASS"
