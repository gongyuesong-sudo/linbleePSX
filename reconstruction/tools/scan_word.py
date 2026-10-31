#!/usr/bin/env python3
# 干净扫描: 找 RAM 窗口内所有 `sw/sh rt,off(base)` 且 base 曾被 lui 0x8008 加载(同函数滑动窗 40 词)
import struct, sys
path, off0, off1, tgt = sys.argv[1], int(sys.argv[2],16), int(sys.argv[3],16), int(sys.argv[4],16)
buf=open(path,'rb').read()
base=0 if off0<0x30000 else 0  # buf 起点即 off0 对应地址
R="zero at v0 v1 a0 a1 a2 a3 t0 t1 t2 t3 t4 t5 t6 t7 s0 s1 s2 s3 s4 s5 s6 s7 t8 t9 k0 k1 gp sp fp ra".split()
addr0=int(sys.argv[5],16) if len(sys.argv)>5 else 0
writers=[];readers=[]
for i in range(0,len(buf)-4,4):
    w=struct.unpack_from('<I',buf,i)[0]
    if (w&0xffff)!=tgt: continue
    op=w>>26
    if op in (43,37,32,36):  # sw sh sb swb? (sw=43 sh=37 sb=40) 修正
        pass
    if op in (43,37,40):
        writers.append((addr0+off0+i,R[(w>>21)&31],R[(w>>16)&31],{43:'sw',37:'sh',40:'sb'}[op]))
    elif op in (35,33,32,41,37,36):
        readers.append((addr0+off0+i,R[(w>>21)&31],R[(w>>16)&31],{35:'lw',33:'lh',32:'lb',41:'lhu',37:'sh!',36:'lbu'}[op]))
print("WRITERS(任何 base, 偏移匹配):",len(writers))
for a,bs,rt,nm in writers[:30]: print(f"  {a:05x} {nm} {rt},[{bs}]")
print("READERS:",len(readers))
for a,bs,rt,nm in readers[:30]: print(f"  {a:05x} {nm} {rt},[{bs}]")
