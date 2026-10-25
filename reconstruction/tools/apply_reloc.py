#!/usr/bin/env python3
"""apply_reloc.py — static application of bleem stub FUN_000553CA reloc semantics.

Block table @VIEW+0xfa0c (16B/entry: src, ?, dst|0x80000000, checksum).
Fixup slots: img imm32 in [0x451000,0x456000) (fixer islands inside blk4).
Fixer bytes recorded as relocation target (+1 = call form); fixer semantics =
VirtualAlloc(MEM_RESERVE|MEM_COMMIT|PAGE_READWRITE) -> absolute address.
Self-healing: applied slots hold positive heap addrs -> `test [reg+0x34],-1` skips.
"""
import struct, sys, os

VIEW = os.environ.get('BLEEM_EXE', 'demo-v1_4.EXE')
IMG  = 'decrypted/stream_10a4c_L71cbf.bin'
OUT  = 'decrypted/stream_10a4c_L71cbf_relocated.bin'

exe = open(VIEW, 'rb').read()
img = bytearray(open(IMG, 'rb').read())

blocks = []
for i in range(6):
    o = 0xfa0c + i * 16
    src, f2, dst, cs = struct.unpack_from('<4I', exe, o)
    if dst == 0 and src == 0 and cs == 0 and i > 0:
        continue
    blocks.append((src, f2, dst, cs, i))

# img module blocks (bit31 set): dst = rva in img coordinates
img_blocks = [(src, dst & 0xffff, cs) for src, f2, dst, cs, i in blocks if dst & 0x80000000]

# VirtualAlloc bump arena (mirrors stub stage-0; RW page-aligned, TOP_DOWN ignored statically)
bump = 0x2a1000
def fake_valloc(size):
    global bump
    addr = bump
    bump += (size + 0xffff) & 0xffff0000
    return addr

FIX_LO, FIX_HI = 0x455338, 0x455df4  # blk4 exact stub body
applied = []
for i in range(0, len(img) - 3):
    v = struct.unpack_from('<I', img, i)[0]
    if FIX_LO <= v < FIX_HI:
        # disassemble fixer at v to recover VirtualAlloc size (push imm before slot0 call)
        f = exe[v - 0x400000: v - 0x400000 + 16]
        size = 0x10000  # default chunk (eng_0x4 pattern)
        for j in range(len(f) - 5):
            if f[j] == 0x68:                      # push imm32
                cand = struct.unpack_from('<I', f, j + 1)[0]
                if 0 < cand <= 0x40000000:
                    size = cand
                    break
        absaddr = fake_valloc(size)
        struct.pack_into('<I', img, i, absaddr)
        applied.append((i, v, size, absaddr))

open(OUT, 'wb').write(bytes(img))

report = {
    'blocks': [{'src': hex(s), 'len': hex(l), 'checksum': hex(c)} for s, l, c in img_blocks],
    'applied': [{'slot': hex(o), 'fixer': hex(f), 'alloc': hex(sz), 'addr': hex(a)} for o, f, sz, a in applied],
    'n_applied': len(applied),
}
import json
json.dump(report, open('decrypted/reloc_report.json', 'w'), indent=1)
print('blocks(img):', [(hex(s), hex(l)) for s, l, c in img_blocks])
print('applied fixups:', len(applied))
for o, f, sz, a in applied:
    print('  slot@%06x fixer=%08x alloc=%#x -> %#x' % (o, f, sz, a))
print('wrote', OUT)
