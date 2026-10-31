#!/usr/bin/env python3
# H_C2HOME 0x40110 分派 stub 生成器(task-044): v0=1(t1!=0xB)/v0=0(0xB)
# 编码均 MIPS-I; jr 延迟槽 nop。base RAM 0x40110, 8 词。
import struct
def L(rt,i): return (15<<26)|(rt<<16)|(i&0xffff)
def O(rt,rs,i): return (13<<26)|(rt<<16)|(rs<<21)|(i&0xffff)
def B(op,rs,rt,off): return (op<<26)|(rs<<21)|(rt<<16)|(off&0xffff)
def JR(rs): return (rs<<21)|8
NOP=0; V0=2; T1=9
words=[L(V0,0), O(V0,V0,1), B(4,T1,0,3), NOP, JR(31), NOP, JR(31), NOP]   # beq t1,0xB->+3(0xb路); 两路 jr ra
for i,w in enumerate(words):
    print(f"{i*4:02x} {w:08x}")
with open('host/c2stub_code.h','w') as f:
    f.write("/* auto-gen tools/c2stub_build.py — H_C2HOME 0x40110 根call分派 stub(task-044) */\n")
    f.write("static const uint32_t c2stub_words[]={"+",".join('0x%08Xu'%w for w in words)+"};\n")
    f.write("#define C2STUB_NWORD %d\n"%len(words))
