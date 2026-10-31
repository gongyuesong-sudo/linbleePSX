#!/usr/bin/env python3
# golden_state_parse.py — Beetle(RetroArch) MDFNSVST 状态 → 金标准 CPU+RAM 提取
# 输出: <out>.ram(2MB) + <out>.hdr(magic GS1, u32 npc@4, u32 pc@8, u32 gpr[32]@12)
# 用法: golden_state_parse.py <disc.state> <out前缀>
import sys, struct, zlib, re

def inflate_all(d):
    pos = 0; out = bytearray()
    while True:
        j = d.find(b'\x78\x9c', pos)
        if j < 0: break
        try:
            o = zlib.decompressobj(); raw = o.decompress(d[j:])
            out += raw
            pos = j + max(len(d[j:]) - len(o.unused_data), 1)
        except Exception:
            pos = j + 1
    return out

def main(state, outpfx):
    d = open(state, 'rb').read()
    out = inflate_all(d)
    i = out.find(b'MainRAM.data8')
    assert i >= 0, 'MainRAM 区缺失'
    p = out.find(b'\x00\x00\x20\x00', i, i + 64)          # size u32 = 0x200000
    assert p >= 0, 'MainRAM size 头缺失'
    ram = out[p + 4:p + 4 + 0x200000]
    assert len(ram) == 0x200000, 'MainRAM 数据不足'
    end_ram = p + 4 + len(ram)
    gi = out.find(b'\x03GPR', end_ram, end_ram + 0x200)   # GPR 条目紧随 RAM 数据尾
    if gi < 0:
        raise AssertionError('GPR 区条目缺失')
    gpr_off = gi + 12                     # 'GPR'+0x80 标志 + 8 pad → 数据(u32 对齐)
    gpr = list(struct.unpack_from('<32I', out, gpr_off))
    def cpu_field(nm):
        pat = bytes([len(nm)]) + nm.encode() + b'\x04\x00\x00\x00'   # 条目: len+name+type(4)+val(4)
        qq = out.find(pat, end_ram)
        assert qq >= 0, nm
        return struct.unpack_from('<I', out, qq + 1 + len(nm) + 4)[0]
    pc = cpu_field('BACKED_PC')            # 真 PC = BACKED_PC(shell 空闲点)
    npc = cpu_field('BACKED_new_PC')
    assert pc & 0x80000000, f'PC 异常 {pc:#x}'
    open(outpfx + '.ram', 'wb').write(ram)
    with open(outpfx + '.hdr', 'wb') as f:
        f.write(b'GS1\x00'); f.write(struct.pack('<I', npc)); f.write(struct.pack('<I', pc))
        f.write(struct.pack('<32I', *gpr))
    names = ['z0','at','v0','v1','a0','a1','a2','a3','t0','t1','t2','t3','t4','t5','t6','t7',
             's0','s1','s2','s3','s4','s5','s6','s7','t8','t9','k0','k1','gp','sp','fp','ra']
    print('pc=%08x npc=%08x' % (pc, npc))
    print(' '.join(f'{n}={gpr[k]:08x}' for k, n in enumerate(names) if gpr[k]))
    print('wrote', outpfx + '.ram/.hdr')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
