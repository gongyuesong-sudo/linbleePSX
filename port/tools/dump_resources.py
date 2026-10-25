#!/usr/bin/env python3
"""
dump_resources.py —— 从原 EXE 的 .rsrc 提取全部资源叶子到 data/。

.rsrc 数据目录叶子 RVA 以映像基址为参照 (段 VA 0x403000, Raw 0x1A00)。
本脚本与 analysis/res/ 下产物一致, 收录进工程作为可重复提取工具。
"""
import os, struct, sys

def dump(exe_path, outdir):
    d = open(exe_path, 'rb').read()
    RS_RAW, RS_RVA = 0x1A00, 0x3000
    u32 = lambda p: struct.unpack_from('<I', d, p)[0]
    u16 = lambda p: struct.unpack_from('<H', d, p)[0]

    def name_at(so):
        ln = u16(RS_RAW + so)
        return ''.join(chr(u16(RS_RAW + so + 2 + 2 * j)) for j in range(ln))

    def walk(o, path=''):
        o0 = RS_RAW + o
        for i in range(u16(o0 + 12) + u16(o0 + 14)):
            e = o0 + 16 + i * 8
            name, ident = u32(e), u32(e + 4)
            nm = name_at(name & 0x7FFFFFFF) if name & 0x80000000 else name
            label = f'{path}/{nm}'
            if ident & 0x80000000:
                yield from walk(ident & 0x7FFFFFFF, label)
            else:
                de = RS_RAW + (ident & 0x7FFFFFFF)
                yield (label, u32(de), u32(de + 4))

    os.makedirs(outdir, exist_ok=True)
    n = 0
    for label, rva, size in walk(0):
        fo = RS_RAW + (rva - RS_RVA)
        parts = label.strip('/').split('/')
        fn = os.path.join(outdir, f"type{parts[0]}_id{parts[1]}.bin")
        open(fn, 'wb').write(d[fo:fo + size])
        n += 1
    print(f'{n} resources -> {outdir}')

if __name__ == '__main__':
    dump(sys.argv[1] if len(sys.argv) > 1 else '../../demo-v1_4.EXE',
         sys.argv[2] if len(sys.argv) > 2 else 'data')
