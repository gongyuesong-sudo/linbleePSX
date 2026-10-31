#!/usr/bin/env python3
# wrap_bios_elf.py — 把 PSX BIOS 原始 ROM（LE MIPS R3000A）封装为 REA/Ghidra 可加载的 ELF32。
# 用法: python3 tools/wrap_bios_elf.py <rom.bin> <out.elf>
# 要点(全部实证踩坑):
#   * PSX ROM 是 小端 —— e_ident[5]=1；按 BE 封装会得到全 halt_baddata 的假反编译
#   * REA raw-binary 不支持 MIPS；Ghidra MIPS 通道要求 e_flags=EF_MIPS_ARCH_32R2(0x70000000)
#   * REA batch_decompile 只认已存在函数、不会就地建 → 必须附 .symtab 符号
#     (默认符号 = A()跳转表 ROM+0x4300×44 + 引导链/rootcall/TTY 桩)
#   * ELF32 sym: pack('<IIIBBH', name,value,0, 0x12(GLOBAL|FUNC),0, shndx)；symtab sh_link 指 .strtab
import struct, sys

def main():
    rom_path, out_path = sys.argv[1], sys.argv[2]
    rom = open(rom_path, 'rb').read()
    assert len(rom) in (0x80000, 0x40000), f"unexpected ROM size {len(rom):#x}"
    base = 0xBFC00000 + (0x80000 - len(rom))   # 512KB ROM @0xBFC00000; 256KB @0xBFC40000
    A = 0x4300 if len(rom) == 0x80000 else None
    syms = [(base + 0x0000, 'bios_reset_vector'), (base + 0x0150, 'bios_init'),
            (base + 0x03CC, 'bios_init_tail'), (base + 0x3CE0, 'printf_dev_walk'),
            (base + 0x4F48, 'tty_init'), (base + 0x4F58, 'tty_open'), (base + 0x4F68, 'tty_close'),
            (base + 0x4F78, 'tty_read'), (base + 0x4F88, 'tty_write'),
            (base + 0xD8C4, 'rootcall_stub2'), (base + 0xD964, 'rootcall_stub1'),
            (base + 0x16684, 'rootcall_stub3'), (base + 0x16694, 'rootcall_stub4'),
            (base + 0x42A14, 'rootcall_stub5'), (base + 0x42A94, 'rootcall_stub6'),
            (base + 0xD570, 'rootcall_site_a'),
            # SESSION-29 H-12a: libinit 五件套/TTY/rootcall 派发（反编译确认的入口）
            (base + 0x1A60, 'libinit_dispatch'), (base + 0x6784, 'init_finalize'),
            (base + 0x703C, 'model_probe'), (base + 0x711C, 'model_init_b'),
            (base + 0x4FAC, 'tty_ret_result'), (base + 0x593C, 'tty_init_core'),
            (base + 0x5558, 'tty_open_core'), (base + 0x5194, 'tty_close_core'),
            (base + 0x57B0, 'tty_write_core'), (base + 0x5A44, 'tty_read_core'),
            (base + 0x3CF0, 'dev_command_walk'), (base + 0xD8C8, 'rootcall_cmd2_body'),
            (base + 0xD588, 'rootcall_ret_b'), (base + 0xD5DC, 'rootcall_ret_c'),
            # SESSION-30 深挖: 异常分发器 + libinit 三件套 + 设备/dma 相关
            (base + 0x1E988, 'exception_dispatcher'),
            (base + 0xDA0, 'libinit_a'), (base + 0xDC0, 'libinit_b'), (base + 0xE2C, 'libinit_c'),
            (base + 0x18D0, 'dev_probe_a'), (base + 0x18E0, 'dev_probe_b'), (base + 0x1910, 'dev_probe_c'),
            (base + 0x67E8, 'autorun_open'), (base + 0x3990, 'gpu_window_init'),
            (base + 0x1A40, 'vector_rearm'), (base + 0x5F08, 'tty_dev_a'),
            (base + 0x6010, 'tty_dev_b'), (base + 0x6038, 'tty_dev_c'), (base + 0x6218, 'tty_dev_d')]
    seen = set(a for a, _ in syms)
    if A is not None:
        for i in range(44):
            e = struct.unpack_from('<I', rom, A + i * 4)[0]
            if e not in seen and base <= e < base + len(rom):
                syms.append((e, f'A{i}_head')); seen.add(e)
    strdata = bytearray(b'\x00'); sym = bytearray(b'\x00' * 16)
    for a, nm in sorted(set(syms)):
        b = nm.encode(); idx = len(strdata); strdata += b + b'\x00'
        sym += struct.pack('<IIIBBH', idx, a, 0, 0x12, 0, 1)
    text_off = 0x1000
    shstr = b'\x00.text\x00.symtab\x00.strtab\x00.shstrtab\x00'
    o_sym = text_off + len(rom); o_str = o_sym + len(sym); o_shstr = o_str + len(strdata)
    def sh(n, t, f, ad, of, sz, lk, inf, al, es): return struct.pack('<10I', n, t, f, ad, of, sz, lk, inf, al, es)
    shdrs = (sh(0,0,0,0,0,0,0,0,0,0) + sh(1,1,6,base,text_off,len(rom),0,0,1,0)
             + sh(7,2,0,0,o_sym,len(sym),3,1,4,16) + sh(15,3,0,0,o_str,len(strdata),0,0,1,0)
             + sh(23,3,0,0,o_shstr,len(shstr),0,0,1,0))
    eh = b'\x7fELF\x01\x01\x01\x00' + b'\x00' * 8
    eh += struct.pack('<HHIIIII', 2, 8, 1, base, 0x34, 0x54, 0x70000000)
    eh += struct.pack('<HHHHHH', 0x34, 0x20, 1, 0x28, 5, 4)
    ph = struct.pack('<IIIIIIII', 1, text_off, base, base, len(rom), len(rom), 5, 0x1000)
    img = eh + ph + shdrs + b'\x00' * (text_off - (0x54 + len(shdrs))) + rom + bytes(sym) + bytes(strdata) + shstr
    open(out_path, 'wb').write(img)
    print(f"ok: {out_path} syms={len(set(syms))} base={base:#x}")

if __name__ == '__main__':
    main()
