#!/usr/bin/env python3
# tools/hbrew_build.py — H-17a 第一方 homebrew PS-X EXE 组装器（零版权：全自研码）
# 程序语义：A(42) strcpy 经 t9 核面调用 → 主循环 counter++ @0x801FE000，
# 每 16 拍写 GPU0 复位命令(0x1F801810=0)。编码子集 + 自研解码器 round-trip 自证。
# EXE 格式 = 经典 0x800 头(字段表见 docs/28) + data 2048 + text。
import struct, sys

R = dict(z0=0,at=1,v0=2,v1=3,a0=4,a1=5,a2=6,a3=7,t0=8,t1=9,t2=10,t3=11,t4=12,t5=13,
         t6=14,t7=15,s0=16,s1=17,s2=18,s3=19,s4=20,s5=21,s6=22,s7=23,t8=24,t9=25,
         k0=26,k1=27,gp=28,sp=29,fp=30,ra=31)
RN = {v: k for k, v in R.items()}

def enc(nm, *a):
    if nm == "nop":   return 0
    if nm == "break": return 0x0000000D
    if nm == "jr":    return (R[a[0]] << 21) | 0x08
    if nm == "jalr":  return (R[a[1]] << 21) | (R[a[0]] << 11) | 0x09        # jalr rd,rs
    if nm == "lui":   return (15 << 26) | (R[a[0]] << 16) | (a[1] & 0xFFFF)  # lui rt,imm
    if nm == "ori":   return (13 << 26) | (R[a[1]] << 21) | (R[a[0]] << 16) | (a[2] & 0xFFFF)   # ori rt,rs,imm
    if nm == "addiu": return ( 9 << 26) | (R[a[1]] << 21) | (R[a[0]] << 16) | (a[2] & 0xFFFF)   # addiu rt,rs,imm
    if nm == "addu":  return (R[a[1]] << 21) | (R[a[2]] << 16) | (R[a[0]] << 11) | 0x21
    if nm == "and":   return (R[a[1]] << 21) | (R[a[2]] << 16) | (R[a[0]] << 11) | 0x24
    if nm == "sw":    return (43 << 26) | (R[a[1]] << 21) | (R[a[0]] << 16) | (a[2] & 0xFFFF)   # sw rt,off(rs) = 0x2B
    if nm == "lw":    return (35 << 26) | (R[a[1]] << 21) | (R[a[0]] << 16) | (a[2] & 0xFFFF)   # lw rt,off(rs) = 0x23
    if nm == "beq":   return ( 4 << 26) | (R[a[0]] << 21) | (R[a[1]] << 16) | (a[2] & 0xFFFF)   # beq rs,rt,imm(规范序)
    if nm == "bne":   return ( 5 << 26) | (R[a[0]] << 21) | (R[a[1]] << 16) | (a[2] & 0xFFFF)   # bne rs,rt,imm(规范序)
    if nm == "j":     return ( 2 << 26) | ((a[0] >> 2) & 0x3FFFFFF)
    if nm == "jal":   return ( 3 << 26) | ((a[0] >> 2) & 0x3FFFFFF)
    raise ValueError(nm)

def decode(w):
    op = (w >> 26) & 0x3F; rs = (w >> 21) & 0x1F; rt = (w >> 16) & 0x1F
    rd = (w >> 11) & 0x1F; f = w & 0x3F; imm = w & 0xFFFF
    if op == 0:
        if w == 0: return ("nop",)
        if f == 8:    return ("jr", RN[rs])
        if f == 9:    return ("jalr", RN[rd], RN[rs])
        if f == 13:   return ("break",)
        if f == 0x24: return ("and", RN[rd], RN[rs], RN[rt])
        if f == 0x21: return ("addu", RN[rd], RN[rs], RN[rt])
        return ("?", w)
    if op == 15: return ("lui", RN[rt], imm)
    if op == 13: return ("ori", RN[rt], RN[rs], imm)
    if op == 9:  return ("addiu", RN[rt], RN[rs], imm)
    if op == 35: return ("lw", RN[rt], RN[rs], imm)
    if op == 43: return ("sw", RN[rt], RN[rs], imm)
    if op == 4:  return ("beq", RN[rs], RN[rt], imm)   # 规范 rs,rt,imm
    if op == 5:  return ("bne", RN[rs], RN[rt], imm)
    if op == 2:  return ("j", imm << 2)
    if op == 3:  return ("jal", imm << 2)
    return ("?", w)

# --- 程序常量（运行环境契约, docs/28） ---
TADDR = 0x80010000                  # text 装载 VA
DADDR = 0x80011000                  # data 装载 VA（头字段声明用）
STR   = DADDR + 0x00                # banner 串 @data 段首
CNT   = 0x801FE000                  # 内存标记计数器
DST   = 0x801FE010                  # strcpy 目的地
A42   = 0xBFC02B50                  # BIOS libc strcpy-family（SESSION-37 实测判异）
GPU0 = 0x1F801810                   # GP0 命令寄存器
GPUSENT = 0x1F80180C                # GP0 数据寄存器(=0): 核面调用前第二设备锚
SENT = 0xBFC4C1B0                   # ROM break 字哨兵(=jalr 链接目标, 核面调用后干净停机)
SP0   = 0x801FFF00

def build_ins():
    ins = []
    def E(nm, *a): ins.append((nm, a))
    E("lui", "s0", 0x801F); E("ori",  "s0", "s0", 0xE000)   # ori 非符号扩展(addiu 0xE000 会 sign-extend 成 -8192!)   # s0 = CNT base
    E("lui", "s4", 0x1F80)                                    # s4 = GPU base
    E("lui", "t7", (SENT >> 16) & 0xFFFF); E("ori", "t7", "t7", SENT & 0xFFFF)  # t7 = ROM break 哨兵(jalr 链接寄存器)
    E("lui", "t6", 0); E("ori", "t6", "t6", 0x2710)           # t6 = N=10000(prologue 预载: bne2 独立块比较不依赖 t8 残值)
    E("ori",  "s1", "z0", 0)                                  # s1 = 0 (counter)
    L = len(ins)                                              # loop 首指令下标(=6)
    # 块布局(差一已按 off=target_idx-(branch_idx+1) 重算):
    # [+0x18..+0x28 bne1] [+0x30 ori v1,+0x34 swG,+0x38 bne2] [+0x40 jN] [+0x48 jback]
    E("addiu", "s1", "s1", 1)                                 # loop: counter++
    E("sw", "s1", "s0", 0)                                    # @CNT
    E("ori", "v1", "z0", 0)                                   # 每拍 GP0(docs/28 §5: 16 拍门控实验让位于端到端收官)
    E("sw", "v1", "s4", GPU0 - 0x1F800000)                    # GP0 复位命令
    E("bne", "s1", "t6", 2); E("nop")                         # !=N → jback(off=target_idx-(branch_idx+1))
    E("j", TADDR + (L+10)*4); E("nop")                        # 到达 N → 核面调用体首(idx L+10)
    E("j", TADDR + L*4); E("nop")                             # → loop 头
    E("addiu", "a0", "s0", DST - CNT)                         # a0 = DST(A(42) dst 在 a0)
    E("lui", "a1", (STR >> 16) & 0xFFFF); E("addiu", "a1", "a1", STR & 0xFFFF)  # a1 = SRC banner
    E("addiu", "a2", "z0", 32)                                # a2 = len
    E("lui", "t9", (A42 >> 16) & 0xFFFF); E("ori", "t9", "t9", A42 & 0xFFFF)
    E("sw", "z0", "s4", GPUSENT - 0x1F800000)                 # +0x?? 写 GP0 数据寄存器(=0): 第二设备锚, 顺带占位
    E("jalr", "t7", "t9"); E("nop")                           # 链接寄存器=t7(非 ra): 返回直接落 prologue 预载的 ROM break 哨兵字(docs/28 §5 返回链事故规避)
    return ins, L

def main():
    ins, L = build_ins()
    words = [enc(nm, *a) for nm, a in ins]
    # 分支目标自检 + round-trip
    for i, ((nm, a), w) in enumerate(zip(ins, words)):
        d = decode(w)
        assert d[0] == nm, (i, nm, a, d)
        if nm in ("j", "jal"):
            assert (d[1] >> 2) & 0x3FFFFF == (a[0] >> 2) & 0x3FFFFF and (a[0] - TADDR) % 4 == 0, (i, d, a)
        if nm in ("beq", "bne"):
            assert (a[2] & 0x8000) == 0 or a[2] < 0x8000, i
    text = b"".join(struct.pack("<I", w) for w in words)
    banner = b"HOMEBREW-SELF-0 OK\n\x00"
    data = banner + b"\x00" * 60
    tsize = len(text); dsize = len(data)
    hdr = bytearray(2048)
    hdr[0x00:0x08] = b"PS-X EXE"
    struct.pack_into("<I", hdr, 0x10, TADDR)      # pc0
    struct.pack_into("<I", hdr, 0x14, SP0)        # gp(保留槽=SP 初值声明)
    struct.pack_into("<I", hdr, 0x18, TADDR)      # t_addr
    struct.pack_into("<I", hdr, 0x1C, tsize)      # t_size
    struct.pack_into("<I", hdr, 0x20, DADDR)      # s_addr
    struct.pack_into("<I", hdr, 0x24, dsize)      # s_size
    struct.pack_into("<I", hdr, 0x30, TADDR)      # initial PC
    out = sys.argv[1] if len(sys.argv) > 1 else "host/build/hbrew.exe"
    open(out, "wb").write(bytes(hdr) + data + text)
    print(f"BUILT {out} pc0=0x{TADDR:08x} t_size={tsize} s_size={dsize} entry_words={len(words)}")
    for i, w in enumerate(words): print(f"+{i*4:03x} {w:08x} {decode(w)}")

if __name__ == "__main__":
    main()
