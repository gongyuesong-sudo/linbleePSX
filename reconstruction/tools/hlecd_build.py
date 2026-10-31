#!/usr/bin/env python3
# tools/hlecd_build.py — H-18a: 自研 C2 rootcall 体机器码生成(零 Sony 码, docs/29 ABI)
# ABI: 命令块 [0x802A0000]: 0=无活 / 0x1001=读盘; C2: 拷 sector(0x80041000→0x80050000, 2048B)
#      + ack [0x802A0040]=0x1001 + 清命令; 其他命令: 清命令接受; 返回 v0=1(完成) 经 jr ra。
import sys, struct
sys.path.insert(0, "tools")
from hbrew_build import enc, decode

SCRATCH = 0x802A0100  # 命令块(分发器进入时清 0x2A0000 首块, 实测 docs/29 → 移到 0x100)
SECDATA = 0x80041000; DST = 0x80050000
ins = []; ANCH = {}
def E(n, *a): ins.append((n, a))

def emit():
    E("lui", "k0", (SCRATCH >> 16) & 0xFFFF); E("ori", "k0", "k0", SCRATCH & 0xFFFF)
    E("lui", "a3", (SECDATA >> 16) & 0xFFFF); E("ori", "a3", "a3", SECDATA & 0xFFFF)
    E("lui", "s0", (DST >> 16) & 0xFFFF);     E("ori", "s0", "s0", DST & 0xFFFF)
    E("ori", "t8", "z0", 0x1001)              # t8 = READ 命令码(预载, 事故勘误 docs/28§5.4)
    global A_OUTER
    outer = len(ins)
    E("lw", "v0", "k0", 0)                    # outer: 命令字
    E("beq", "v0", "z0", 0); E("nop")         # (patch) 0 → done   [延迟槽必备]
    E("bne", "v0", "t8", 0); E("nop")         # (patch) 非 READ → other
    E("ori", "s5", "z0", 0x200)               # 剩余字数(count-down: MIPS-I 无 seq, 0x2A 是 slt — 勘误 docs/29)
    E("addu", "s2", "a3", "z0")               # s2 = src 指针(每命令重置)
    L = len(ins)
    E("lw", "t7", "s2", 0); E("sw", "t7", "s0", 0)
    E("addiu", "s2", "s2", 4); E("addiu", "s0", "s0", 4)
    E("addiu", "s5", "s5", -1)
    E("bne", "s5", "z0", 0); E("nop")         # (patch) 剩余≠0 → 拷循环回边 L(负偏移); =0 fall-through → ack
    E("sw", "t8", "k0", 0x100)                # ack 应答块(勘误: +0x40 落分发器清零首块, 移同块 +0x100)
    E("sw", "z0", "k0", 0)                    # 清命令
    E("j", 0); E("nop")                       # (patch) → outer
    other = len(ins)
    E("sw", "z0", "k0", 0)                    # 未知命令: 接受并清
    E("j", 0); E("nop")                       # (patch) → outer
    done = len(ins)
    E("addiu", "v0", "z0", 1); E("jr", "ra"); E("nop")
    ANCH["outer"], ANCH["other"], ANCH["done"], ANCH["L"] = outer, other, done, L

emit()
outer, other, done, L = ANCH["outer"], ANCH["other"], ANCH["done"], ANCH["L"]
# patch 分支偏移: off = target_idx - (branch_idx+1)
for i, (n, a) in enumerate(ins):
    if n == "beq" and a == ("v0", "z0", 0): ins[i] = ("beq", ("v0", "z0", done - (i + 1)))
    if n == "bne" and a == ("v0", "t8", 0): ins[i] = ("bne", ("v0", "t8", other - (i + 1)))
    if n == "bne" and a == ("s5", "z0", 0): ins[i] = ("bne", ("s5", "z0", L - (i + 1)))      # 回边(负)
words = []
for n, a in ins:
    if n == "j":
        tgt = outer if a == (0,) else L       # (0,)→outer 主循环; (1,)→拷循环回边
        w = (2 << 26) | (((0x00040000 + tgt * 4) >> 2) & 0x3FFFFFF)   # j 低26位域(C2 区 0x80040000)
        words.append(w)
    else:
        words.append(enc(n, *a))
for w in words: assert decode(w)[0] != "?", hex(w)
out = sys.argv[1] if len(sys.argv) > 1 else "host/hlecd_code.h"
with open(out, "w") as f:
    f.write("/* 生成: tools/hlecd_build.py — H-18a 自研 C2 体(docs/29) */\n")
    f.write("static const uint32_t g_hlecd_code[] = {\n")
    for i in range(0, len(words), 6):
        f.write("    " + ",".join("0x%08X" % w for w in words[i:i+6]) + ",\n")
    f.write("};\n")
print(f"BUILT {out} words={len(words)}")
for i, w in enumerate(words): print(f"+{i*4:03x} {w:08x} {decode(w)}")
