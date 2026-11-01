#!/usr/bin/env python3
# wrap_engine_elf.py — 把解密引擎流(stream_10a4c)封装为 ELF32-i386 + .symtab 函数入口表，
# 供 REA/Ghidra 建真函数(批反编译只认已存在函数——wrap_bios_elf.py 同款管线，x86 lane)。
# 用法: python3 tools/wrap_engine_elf.py <img.bin> <entries.json> <out.elf>
# 约定: 加载基址=0，符号 st_value == 流内偏移(与 decrypted/decompiled 存档地址同一坐标系)。
import json, struct, sys

def main():
    img_path, ent_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
    base = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0
    entry = int(sys.argv[5], 16) if len(sys.argv) > 5 else 0x4
    prefix = sys.argv[6] if len(sys.argv) > 6 else 'eng'
    img = open(img_path, 'rb').read()
    ents = json.load(open(ent_path))['entries']
    syms = [(base + entry, 'entry')]
    for e in ents:
        a = int(e['addr'], 16)
        tag = 'deep' if 'zone172-deepptr' in e['src'] else ('arch' if 'archived' in e['src'] else 'cand')
        syms.append((base + a, f'{prefix}_{tag}_{a:05x}'))
    syms = sorted(set(syms))
    strdata = bytearray(b'\x00'); sym = bytearray(b'\x00' * 16)
    for a, nm in syms:
        b = nm.encode(); idx = len(strdata); strdata += b + b'\x00'
        sym += struct.pack('<IIIBBH', idx, a, 0, 0x12, 0, 1)   # GLOBAL|FUNC, shndx=1(.text)
    text_off = 0x1000
    shstr = b'\x00.text\x00.symtab\x00.strtab\x00.shstrtab\x00'
    o_sym = text_off + len(img); o_str = o_sym + len(sym); o_shstr = o_str + len(strdata)
    def sh(n, t, f, ad, of, sz, lk, inf, al, es): return struct.pack('<10I', n, t, f, ad, of, sz, lk, inf, al, es)
    shdrs = (sh(0,0,0,0,0,0,0,0,0,0) + sh(1,1,6,base,text_off,len(img),0,0,1,0)
             + sh(7,2,0,0,o_sym,len(sym),3,1,4,16) + sh(15,3,0,0,o_str,len(strdata),0,0,1,0)
             + sh(23,3,0,0,o_shstr,len(shstr),0,0,1,0))
    eh = b'\x7fELF\x01\x01\x01\x00' + b'\x00' * 8
    eh += struct.pack('<HHIIIII', 2, 3, 1, base + entry, 0x34, 0x54, 0)   # EM_386=3
    eh += struct.pack('<HHHHHH', 0x34, 0x20, 1, 0x28, 5, 4)
    ph = struct.pack('<IIIIIIII', 1, text_off, base, base, len(img), len(img), 7, 0x1000)  # PF_RWX
    blob = eh + ph + shdrs + b'\x00' * (text_off - (0x54 + len(shdrs))) + img + bytes(sym) + bytes(strdata) + shstr
    open(out_path, 'wb').write(blob)
    print(f"ok: {out_path} syms={len(syms)} size={len(blob)}")

if __name__ == '__main__':
    main()
