#!/usr/bin/env python3
"""scan_bios_syscalls.py — task-023 H-11a：BIOS A-call/syscall 静态普查（可复跑）。

证据产出（供 docs/14 引用与 validate_host16 断言）：
  1) A(0)-A(43) 默认跳表 @ROM+0x4300：44 项 BIOS 窗指针；默认处理器=BFC06FDC（A(0)-A(9) 未实现）；
  2) syscall(0x0000000C) 普查 + 代码段过滤；
  3) BIOS 内部 rootcall 桩模式：addiu $4,$0,N ; syscall ; jr $31（N=rootcall 命令号）；
  4) A 处理器入口序言画像（决定 H-11b 需要哪些 MIPS 扩展面）。
退出码 0 = 结构断言全部成立。"""
import struct, sys

ROM = sys.argv[1] if len(sys.argv) > 1 else "/home/gill/bleem/psxbios/SCPH1001.BIN"
A_BASE, A_N, DEF = 0x4300, 44, 0xBFC06FDC

def main():
    rom = open(ROM, 'rb').read()
    assert len(rom) == 0x80000, f"ROM size {len(rom)} != 512KB"
    w = lambda o: struct.unpack_from('<I', rom, o)[0]

    # 1) A 表：指针须落在 BIOS ROM 窗（0xBFC00000..0xBFC7FFFF，含 0x1FC00000 镜像）
    def in_rom_window(v):
        return v in (0xBFC00000, 0xBFC80000) or (0xBFC00000 <= v < 0xBFC80000 and v % 4 == 0) \
               or (0x1FC00000 <= v < 0x1FC80000 and v % 4 == 0)
    tbl = struct.unpack_from('<%dI' % A_N, rom, A_BASE)
    bad = [(i, v) for i, v in enumerate(tbl) if not in_rom_window(v)]
    assert not bad, f"A 表存在非 ROM 窗指针: {bad[:4]}"
    real = [(i, v) for i, v in enumerate(tbl) if v != DEF]
    defaults = [i for i, v in enumerate(tbl) if v == DEF]
    print(f"A-TABLE base=0x{A_BASE:05x} entries={A_N} real={len(real)} default_handlers={len(defaults)}")
    for i, v in real:
        print(f"A({i:2d}) =handler {v:08x}")
    print(f"A-DEFAULT unimplemented={defaults}")

    # 2) syscall 普查（代码段判据：前后 4 字节均可解码为常见 MIPS 指令）
    def plausible_instr(x):
        op = x >> 26
        if op == 0:
            return (x & 63) in (0, 2, 3, 8, 9, 12, 13, 16, 17, 18, 19, 32, 33, 34, 35, 36, 37, 38, 39, 42, 43, 24, 25, 26, 27, 30, 31)
        return op in (2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
                      32, 33, 34, 35, 36, 37, 38, 40, 41, 42, 43, 46)
    pts = [i for i in range(0, len(rom) - 3, 4) if w(i) == 0x0000000C]
    code_pts = [i for i in pts if plausible_instr(w(i + 4)) and plausible_instr(w(i - 4))]
    print(f"SYSCALL total={len(pts)} in-code={len(code_pts)}")

    # 3) rootcall 桩：addiu $4,$0,N ; syscall ; jr $31
    stubs = []
    for i in code_pts:
        p = w(i - 4)
        if (p >> 26) == 9 and ((p >> 21) & 31) == 0 and ((p >> 16) & 31) == 4 and w(i + 4) == 0x03E00008:
            stubs.append((i, p & 0xFFFF))
    print(f"ROOTCALL-STUBS n={len(stubs)}")
    for off, n in stubs:
        print(f"  rootcall cmd={n} @rom+0x{off:06x} (guest {0xBFC00000+off:08x})")

    # 4) A 处理器序言画像 → H-11b 所需 MIPS 扩展面
    ops = {}
    for _, v in real:
        base = v - 0xBFC00000
        for k in range(8):
            x = w(base + k * 4)
            if x == 0:
                continue
            op = x >> 26
            f = x & 63
            key = f"op{op:02d}" + (f":f{f:02d}" if op == 0 else "")
            ops[key] = ops.get(key, 0) + 1
    top = sorted(ops.items(), key=lambda kv: -kv[1])[:14]
    print("A-HANDLER-PROLOGUE-OPS " + " ".join(f"{k}={v}" for k, v in top))
    cop = sum(v for k, v in ops.items() if k in ("op16", "op17", "op18", "op19"))
    print(f"A-HANDLER-COP-ISH ops={cop}  (决定 JIT 是否需 cop0 真实语义)")

    assert len(real) == 34, f"real handlers={len(real)} != 34"
    assert defaults == list(range(10)), f"unexpected default set {defaults}"
    assert len(stubs) >= 2, f"rootcall stubs={len(stubs)} < 2"
    print(f"VERDICT ok stub-cmds={sorted(n for _, n in stubs)}")
    return 0

if __name__ == '__main__':
    sys.exit(main())
