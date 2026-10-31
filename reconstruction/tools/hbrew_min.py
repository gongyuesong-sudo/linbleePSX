#!/usr/bin/env python3
# tools/hbrew_min.py — 极简 homebrew: counter++ @0x801FE000 + 每拍 GPU0 写 + 到 2000 停机 break
import struct, sys
sys.path.insert(0, "tools")
from hbrew_build import enc, decode, R

CNT = 0x801FE000; GPU0 = 0x1F801810; TADDR = 0x80020000; SP0 = 0x801FFF00
ins = []
def E(n, *a): ins.append((n, a))
E("lui", "s0", 0x801F); E("ori",  "s0", "s0", 0xE000)   # ori 非符号扩展(addiu 0xE000 会 sign-extend 成 -8192!)
E("lui", "s4", 0x1F80)
E("ori",  "s1", "z0", 0)
L = len(ins)
E("addiu", "s1", "s1", 1)
E("sw", "s1", "s0", 0)
E("ori", "v1", "z0", 0)
E("sw", "v1", "s4", GPU0 - 0x1F800000)
E("lui", "t8", 0); E("ori", "t8", "t8", 2000)
E("bne", "s1", "t8", -7); E("nop")   # off=target_idx-(branch_idx+1)=4-11=-7 → +0x10 loop 头
E("break"); E("nop")                        # 到数停机
words = [enc(n, *a) for n, a in ins]
for w in words: assert decode(w)[0] != "?"
text = b"".join(struct.pack("<I", w) for w in words)
hdr = bytearray(2048); hdr[0:8] = b"PS-X EXE"
struct.pack_into("<6I", hdr, 0x10, TADDR, SP0, TADDR, len(text), 0, 0)
out = sys.argv[1] if len(sys.argv) > 1 else "host/build/hbrew_min.exe"
open(out, "wb").write(bytes(hdr) + text)
print(f"BUILT {out} words={len(words)} taddr={TADDR:#x}")
for i, w in enumerate(words): print(f"+{i*4:03x} {w:08x} {decode(w)}")
