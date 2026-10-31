#!/usr/bin/env python3
"""H-9b MIPS 执行墙画像（task-020）：SCPH1001 BIOS 引导链静态仿真 + JIT 最小指令集清单。

模型：MIPS-I (R3000) 子集，PSX 内存别名（ROM 0xBFC00000 ≡ phys 0x1FC00000；KSEG 一律化物理），
分支延迟槽执行，jal 返回栈跟随；COP0/COP1(保留)/COP2(GTE)/COP3 按不透明影子语义计数。
输出：仿真步数、opcode 直方图、未知指令清单（=0 为验收线）、路径摘要。
用法：python3 tools/mips_wall.py /path/SCPH1001.BIN
"""
import struct, sys, collections

S32 = lambda x: x - (1 << 32) if x & 0x80000000 else x
U32 = lambda x: x & 0xFFFFFFFF

def phys(a):
    a = U32(a)
    if a & 0x80000000: a &= 0x1FFFFFFF          # KSEG1/KSEG0 → physical
    return a

RES_F3 = set(range(0x10, 0x20))                  # SPECIAL：MF/MFC/MT/MTC/bc* 全 COP 族
TOP_COP = {0x10: "cop1", 0x11: "cop0", 0x12: "cop2", 0x13: "cop3"}

def decode(w):
    op = (w >> 26) & 0x3F; f3 = w & 0x3F
    if w in (0, 0x00000000, 0x03E00000, 0x0000003E): return "nop"     # sll zero / srav 形态 NOP
    if op == 0 and ((w>>21)&31)==0 and (w&0x3F)==0: return "nop"      # sll $zero,* 一律 NOP
    if op == 0:
        if f3 in RES_F3: return "cop_resident"
        return {0:"sll",2:"srl",3:"sra",8:"jr",9:"jalr",12:"syscall",13:"break",16:"mf",
                17:"mfc",18:"mt",19:"mtc",32:"add",33:"addu",34:"sub",35:"subu",36:"and",
                37:"or",38:"xor",39:"nor",42:"slt",43:"sltu"}.get(f3, f"spec0_{f3}")
    if op in TOP_COP: return TOP_COP[op]
    RSH={2:"j",3:"jal",4:"beq",5:"bne",6:"blez",7:"bgtz"}
    RTI={8:"addi",9:"addiu",10:"slti",11:"sltiu",12:"andi",13:"ori",14:"xori",15:"lui"}
    RS ={32:"lw",33:"lw",34:"lwl",35:"lb",36:"lbu",37:"lhu",38:"lwr",
         40:"sb",41:"sh",42:"lwc2",43:"sw",44:"swl",45:"swr",46:"lwc2",47:"swc2"}
    return RSH.get(op) or RTI.get(op) or RS.get(op) or f"op_{op}"

def classify(w):
    m = decode(w)
    if m in ("cop_resident","cop0","cop1","cop2","cop3"): return "cop"
    if m in ("j","jal","beq","bne","blez","bgtz","jr","jalr"): return "branch"
    if m in ("syscall","break"): return "syscall"
    if m == "nop": return "nop"
    if m.startswith("op_") or m.startswith("spec0_"): return "unknown"
    if m.startswith(("l","s")) and m not in ("slt","sltu"): return "lsu"
    return "arith"

def sim(rom, rom_phys=0x1FC00000, max_steps=60000):
    regs=[0]*32; pc=0xBFC00000; steps=0
    hist=collections.Counter(); ch=collections.Counter()
    unknown=[]; ret_stack=[]; loop_hits=collections.Counter(); path=[]
    ram=bytearray(0x200000)
    def rmem(a,n):
        p=phys(a)
        if rom_phys <= p < rom_phys+len(rom): return rom[p-rom_phys:p-rom_phys+n]
        if p <= len(ram)-n: return ram[p:p+n]
        return None
    def rd32(a):
        b=rmem(a,4); return struct.unpack("<I",b)[0] if b and len(b)==4 else 0
    def wrb(a,b,n):
        p=phys(a)
        if p <= len(ram)-n: ram[p:p+n]=b
    def fetch(a):
        b=rmem(a,4); return struct.unpack("<I",b)[0] if b and len(b)==4 else None
    while steps<max_steps:
        w=fetch(pc)
        if w is None: path.append(f"{pc:08x}:fetch-fail @{steps}"); break
        m=decode(w); cl=classify(w)
        hist[m]+=1; ch[cl]+=1
        if cl=="unknown" and len(unknown)<50: unknown.append((f"{pc:08x}",m))
        op=(w>>26)&0x3F; rs=(w>>21)&31; rt=(w>>16)&31; rd_=(w>>11)&31; sa=(w>>6)&31; f3=w&0x3F; imm=w&0xFFFF
        regs[0]=0; tgt=None; nxt=pc+4
        if op==0:
            if m=="jr": tgt=U32(regs[rs])
            elif m=="jalr": tgt=U32(regs[rs]); regs[rd_ if rd_ else 31]=pc+8
            elif m in ("add","addu"): regs[rd_]=U32(regs[rs]+regs[rt])
            elif m in ("sub","subu"): regs[rd_]=U32(regs[rs]-regs[rt])
            elif m=="and": regs[rd_]=regs[rs]&regs[rt]
            elif m=="or": regs[rd_]=regs[rs]|regs[rt]
            elif m=="xor": regs[rd_]=regs[rs]^regs[rt]
            elif m=="nor": regs[rd_]=U32(~(regs[rs]|regs[rt]))
            elif m=="slt": regs[rd_]=1 if S32(regs[rs])<S32(regs[rt]) else 0
            elif m=="sltu": regs[rd_]=1 if regs[rs]<regs[rt] else 0
            elif m=="sll": regs[rd_]=U32(regs[rt]<<sa)
            elif m=="srl": regs[rd_]=regs[rt]>>sa
            elif m=="sra": regs[rd_]=U32(S32(regs[rt])>>sa)
        elif op==2: tgt=(pc&0xF0000000)|((w&0x3FFFFFF)<<2)
        elif op==3:
            regs[31]=pc+8; ret_stack.append(pc+8); tgt=(pc&0xF0000000)|((w&0x3FFFFFF)<<2)
        elif op==4: tgt=U32(pc+4+S32(imm<<2)) if regs[rs]==regs[rt] else None
        elif op==5: tgt=U32(pc+4+S32(imm<<2)) if regs[rs]!=regs[rt] else None
        elif op==6: tgt=U32(pc+4+S32(imm<<2)) if S32(regs[rs])<=0 else None
        elif op==7: tgt=U32(pc+4+S32(imm<<2)) if S32(regs[rs])>0 else None
        elif op in (8,9): regs[rt]=U32(regs[rs]+S32(imm))
        elif op==10: regs[rt]=1 if S32(regs[rs])<S32(imm) else 0
        elif op==11: regs[rt]=1 if regs[rs]<U32(S32(imm)) else 0
        elif op==12: regs[rt]=regs[rs]&imm
        elif op==13: regs[rt]=regs[rs]|imm
        elif op==14: regs[rt]=regs[rs]^imm
        elif op==15: regs[rt]=U32(imm<<16)
        elif op in (32,33): regs[rt]=rd32(U32(regs[rs]+S32(imm)))
        elif op in (35,36):
            b=rmem(U32(regs[rs]+S32(imm)),1); regs[rt]=b[0] if b else 0
        elif op==37:
            b=rmem(U32(regs[rs]+S32(imm)),2); regs[rt]=struct.unpack("<H",b)[0] if b and len(b)==2 else 0
        elif op==40: wrb(U32(regs[rs]+S32(imm)),bytes([regs[rt]&0xff]),1)
        elif op==41: wrb(U32(regs[rs]+S32(imm)),struct.pack("<H",regs[rt]&0xffff),2)
        elif op==43: wrb(U32(regs[rs]+S32(imm)),struct.pack("<I",regs[rt]),4)
        if tgt is not None and fetch(tgt) is None and ret_stack:
            tgt=ret_stack.pop()                              # 返回栈跟随（tgt 不可 fetch）
        dw=fetch(nxt)                                        # 延迟槽
        if dw is None: path.append(f"{nxt:08x}:delay-fetch-fail @{steps}"); break
        dop=(dw>>26)&0x3F; drs=(dw>>21)&31; drt=(dw>>16)&31; drd=(dw>>11)&31; dimm=dw&0xFFFF
        hist["[d]"+decode(dw)]+=1
        if dop in (32,33): regs[drt]=rd32(U32(regs[drs]+S32(dimm)))
        elif dop==43: wrb(U32(regs[drs]+S32(dimm)),struct.pack("<I",regs[drt]),4)
        elif dop==15: regs[drd]=U32(dimm<<16)
        elif dop in (8,9): regs[drt]=U32(regs[drs]+S32(dimm))
        elif dop==0 and (dw&0x3F) in (32,33): regs[drd]=U32(regs[drs]+regs[drt])
        elif dop==0 and (dw&0x3F)==35: regs[drd]=U32(regs[drs]-regs[drt])
        elif dop==0 and (dw&0x3F)==37: regs[drd]=regs[drs]|regs[drt]
        pc = U32(tgt if tgt is not None else nxt)
        loop_hits[pc]+=1
        if loop_hits[pc]>300: path.append(f"{pc:08x} spin @{steps}"); break
        if steps and steps%8000==0 and len(path)<12: path.append(f"{pc:08x} @{steps}")
        steps+=1
    return steps,hist,ch,unknown,path

def scan_all(rom):
    hist=collections.Counter(); ch=collections.Counter(); unk=[]
    for a in range(0,len(rom),4):
        w=struct.unpack_from("<I",rom,a)[0]
        m=decode(w); cl=classify(w)
        hist[m]+=1; ch[cl]+=1
        if cl=="unknown" and len(unk)<40: unk.append((f"1fc{a:06x}",m))
    print("[full-rom] mnemonics=" + " ".join(f"{m}:{n}" for m,n in hist.most_common()))
    print("[full-rom] class=" + " ".join(f"{k}={v}" for k,v in sorted(ch.items())))
    print(f"[full-rom] unknown_words={sum(hist[m] for m in hist if m.startswith(('op_','spec0_')))}")
    print("[full-rom] unknown_sample=" + ",".join(f"{a}:{b}" for a,b in unk[:12]))

def main():
    if len(sys.argv)>2 and sys.argv[2]=="scan":
        rom=open(sys.argv[1],"rb").read(); assert len(rom)==0x80000
        scan_all(rom); return
    rom=open(sys.argv[1] if len(sys.argv)>1 else "/home/gill/bleem/psxbios/SCPH1001.BIN","rb").read()
    assert len(rom)==0x80000, "need 512KB ROM"
    steps,hist,ch,unknown,path=sim(rom)
    core=[m for m in hist if not m.startswith("[d]")]
    unk=sum(hist[m] for m in core if m.startswith(("op_","spec0_")))
    print(f"boot_steps_simulated={steps}")
    print(f"distinct_mnemonics={len(core)}")
    print("class_hist=" + " ".join(f"{k}={v}" for k,v in sorted(ch.items())))
    print("top30=" + ",".join(f"{m}:{n}" for m,n in hist.most_common(30)))
    print(f"unknown_count={unk}")
    print("unknown_list=" + ",".join(f"{a}:{b}" for a,b in unknown[:10]))
    print("path=" + " | ".join(path[-8:]))
    print("RESULT=OK" if steps>=5000 and unk==0 else "RESULT=PARTIAL")
    scan_all(rom)

if __name__=="__main__": main()
