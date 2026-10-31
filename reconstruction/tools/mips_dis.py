#!/usr/bin/env python3
# 最小 MIPS-I(R3000, LE) 反汇编器——ROM/RAM 取证用(task-044)
import struct, sys
R="zero at v0 v1 a0 a1 a2 a3 t0 t1 t2 t3 t4 t5 t6 t7 s0 s1 s2 s3 s4 s5 s6 s7 t8 t9 k0 k1 gp sp fp ra".split()
I={2:'j',3:'jal',4:'beq',5:'bne',6:'blez',7:'bgtz',8:'addi',9:'addiu',10:'slti',11:'sltiu',12:'andi',13:'ori',14:'xori',15:'lui',32:'lb',33:'lh',35:'lw',36:'sb',37:'sh',43:'sw',40:'lbu',41:'lhu',16:'cop0',17:'cop1'}
F={0:'sll',2:'srl',3:'sra',6:'srlv',7:'srav',8:'jr',9:'jalr',12:'syscall',13:'break',16:'mfhi',17:'mthi',18:'mflo',19:'mtlo',24:'mult',25:'multu',26:'div',27:'divu',32:'add',33:'addu',34:'sub',35:'subu',36:'and',37:'or',38:'xor',39:'nor',42:'slt',43:'sltu'}
RG={0:'bltz',1:'bgez',2:'bltzal',3:'bgezal'}

def dis(w, pc):
    op=w>>26; rs=(w>>21)&31; rt=(w>>16)&31; rd=(w>>11)&31; sa=(w>>6)&31; f=w&63; si=w&0xffff
    if op==0:
        nm=F.get(f,'f%02x'%f)
        if f in (0,2,3): return f"{nm} {R[rd]},{R[rt]},{sa}"
        if f in (6,7): return f"{nm} {R[rd]},{R[rt]},{R[rs]}"
        if f in (8,): return f"{nm} {R[rs]}"
        if f in (9,): return f"{nm} {R[rd]},{R[rs]}"
        if f in (12,13): return f"{nm} 0x{w&0xfffffff:x}"
        if f in (16,18): return f"{nm} {R[rd]}"
        if f in (17,19): return f"{nm} {R[rs]}"
        if f in (24,25,26,27): return f"{nm} {R[rs]},{R[rt]}"
        return f"{nm} {R[rd]},{R[rs]},{R[rt]}"
    if op==1: return f"{RG.get(rt,'rg%d'%rt)} {R[rs]},0x{si:x}"
    if op in (2,3): return f"{I[op]} 0x{((pc+4)&0xf0000000)|(si<<2):08x}"
    if op in (4,5): return f"{I[op]} {R[rs]},{R[rt]},0x{si:x}"
    if op in (6,7): return f"{I[op]} {R[rs]},0x{si:x}"
    if op==15: return f"lui {R[rt]},0x{si:x}"
    if op in I: return f"{I[op]} {R[rt]},{R[rs]},0x{si:x}"
    return f".word 0x{w:08x}"

def dump(buf, base, off0, n, out=sys.stdout):
    for i in range(n):
        o=off0+i*4
        if o+4>len(buf): break
        w=struct.unpack_from('<I',buf,o)[0]
        print(f"{base+o:08x} {w:08x} {dis(w,base+o)}", file=out)

if __name__=='__main__':
    src,off0,n=sys.argv[1],int(sys.argv[2],16),int(sys.argv[3])
    base=int(sys.argv[4],16) if len(sys.argv)>4 else 0xbfc00000
    dump(open(src,'rb').read(), base, off0, n)
