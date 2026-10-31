#!/usr/bin/env python3
# mini MIPS R3000A 判读器(楔4 侦察用; ROM 窗口 GPA-0xBFC00000)
import struct, sys
b = open('/home/gill/bleem/psxbios/SCPH1001.BIN', 'rb').read()
R = ['$0','at','v0','v1','a0','a1','a2','a3','t0','t1','t2','t3','t4','t5','t6','t7',
     's0','s1','s2','s3','s4','s5','s6','s7','t8','t9','k0','k1','gp','sp','fp','ra']
F = {0:'sll',2:'srl',3:'sra',8:'jr',9:'jalr',12:'syscall',13:'break',15:'sync',16:'mfhi',
     17:'mthi',18:'mflo',19:'mtlo',24:'mult',25:'multu',26:'div',27:'divu',32:'add',33:'addu',
     34:'sub',35:'subu',36:'and',37:'or',38:'xor',39:'nor',42:'slt',43:'sltu',52:'addi.w?'}
SPECIAL={16:'mfhi',17:'mthi',18:'mflo',19:'mtlo',24:'mult',25:'multu',26:'div',27:'divu',
         8:'jr',9:'jalr',13:'break',15:'sync',16:'mfhi'}
def dis(u, gpa):
    op=u>>26; rs=(u>>21)&31; rt=(u>>16)&31; rd=(u>>11)&31; sa=(u>>6)&31; f=u&63; im=u&0xFFFF
    sim=im-65536 if im>=32768 else im
    if op==0:
        if f in (8,9): return f"{F[f]} ${R[rs]}" + (f" ->${R[rd]}" if f==9 else "")
        if f in (0,): 
            if u==0: return "nop"
            return f"sll ${R[rd]},${R[rt]},{sa}"
        if f in (2,3): return f"{F[f]} ${R[rd]},${R[rt]},{sa}"
        if f in (24,25,26,27): return f"{F[f]} ${R[rs]},${R[rt]}"
        if f in (32,33,34,35,36,37,38,39,42,43): return f"{F[f]} ${R[rd]},${R[rs]},${R[rt]}"
        if f==16: return f"mfhi ${R[rd]}"
        if f==17: return f"mthi ${R[rs]}"
        if f==18: return f"mflo ${R[rd]}"
        if f==19: return f"mtlo ${R[rs]}"
        if f==36: return f"and ${R[rd]},${R[rs]},${R[rt]}"
        if f==56: return f"trap?"
        if f==20: return f"add ${R[rd]},${R[rs]},${R[rt]}"
        return f"special f={f} rs={rs} rt={rt} rd={rd}"
    if op==2: return f"j 0x{(gpa+4 & 0xF0000000)|((u&0x3FFFFFF)<<2):x}"
    if op==3: return f"jal 0x{(gpa+4 & 0xF0000000)|((u&0x3FFFFFF)<<2):x}"
    if op==4: return f"beq ${R[rs]},${R[rt]},{sim*4:+d}"
    if op==5: return f"bne ${R[rs]},${R[rt]},{sim*4:+d}"
    if op==6: return f"blez ${R[rs]},{sim*4:+d}"
    if op==7: return f"bgtz ${R[rs]},{sim*4:+d}"
    if op==8: return f"addi ${R[rt]},${R[rs]},{sim}"
    if op==9: return f"addiu ${R[rt]},${R[rs]},{sim}"
    if op==10: return f"slti ${R[rt]},${R[rs]},{sim}"
    if op==11: return f"sltiu ${R[rt]},${R[rs]},{sim}"
    if op==12: return f"andi ${R[rt]},${R[rs]},0x{im:x}"
    if op==13: return f"ori ${R[rt]},${R[rs]},0x{im:x}"
    if op==14: return f"xori ${R[rt]},${R[rs]},0x{im:x}"
    if op==15: return f"lui ${R[rt]},0x{im:x}"
    if op==32: return f"lb ${R[rt]},{sim}(${R[rs]})"
    if op==33: return f"lw ${R[rt]},{sim}(${R[rs]})"
    if op==35: return f"lbu ${R[rt]},{sim}(${R[rs]})"
    if op==36: return f"lh ${R[rt]},{sim}(${R[rs]})"
    if op==37: return f"lhu ${R[rt]},{sim}(${R[rs]})"
    if op==39: return f"?? {u:08x}"
    if op==40: return f"sb ${R[rt]},{sim}(${R[rs]})"
    if op==43: return f"sw ${R[rt]},{sim}(${R[rs]})"
    if op==34: return f"lwl ${R[rt]},{sim}(${R[rs]})"
    if op==38: return f"lwr ${R[rt]},{sim}(${R[rs]})"
    if op==42: return f"swl ${R[rt]},{sim}(${R[rs]})"
    if op==46: return f"swr ${R[rt]},{sim}(${R[rs]})"
    if op==16:
        CO=['r31?','r0','r1','r2','r3','r4','r5','r6','badv','count','sr?','cause?','epc','prid']
        return f"c0 op{rs} {['mfc0','mtc0','mfc2?','mtc2?'][rt&3]} ${R[rd]},c{rs}"
    if op==17: return f"cop2 r{rs}"
    if op==24: return f"ll ${R[rt]},{sim}(${R[rs]})"
    if op==25: return f"sc ${R[rt]},{sim}(${R[rs]})"
    if op==28: return f"scd ${R[rt]},{sim}(${R[rs]})"
    return f"op{op} {u:08x}"
start=int(sys.argv[1],16); cnt=int(sys.argv[2]) if len(sys.argv)>2 else 40
for k in range(cnt):
    g=start+k*4; u=struct.unpack_from('<I',b,g-0xBFC00000)[0]
    print(f"{g:x}: {u:08x}  {dis(u,g)}")
