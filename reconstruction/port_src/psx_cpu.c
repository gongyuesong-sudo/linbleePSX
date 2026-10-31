/*
 * psx_cpu.c —— MIPS R3000A 整数解释核 楔3 (task-109, SESSION-120)
 * 译码表 = MIPS I 规范编码 (PSX 核即 LSI R3000A);
 * 延迟槽/异常面语义对齐 rewrite/host/mips_jit.c 已定案行为。
 * 已定案边界(记录于 docs/90): add/sub 不做溢出 trap(按 addu);
 * eret/syscall 中断延迟槽簿记; div0 结果 hi=0 lo=0xFFFFFFFF(未定义面取保守值);
 * GTE(cop1/c2)、lwl/lwr/swl/swr、cache 具体语义留楔4。
 */
#include "psx_cpu.h"
#include "psx_dev.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIGN16(x) ((uint32_t)(int32_t)(int16_t)(x))

static void enter_exception(psx_cpu_t *c, uint32_t excode, uint32_t badv, int has_badv)
{
    c->cop0[14] = c->pc;                    /* epc */
    c->cop0[13] = excode << 2;              /* cause.ExCode */
    if (has_badv) c->cop0[8] = badv;        /* vaddr */
    c->cop0[12] |= 2u;                      /* status.EXL=bit1 (楔5 真值修正: 楔3 误用 bit0=IE) */
    c->branch_pending = 0;
    uint32_t vec = (c->cop0[12] & (1u << 22)) ? 0xBFC00000u : 0x80000080u;
    c->pc  = vec - 4;
    c->npc = vec;
    c->exc_n++;
}

static int mem_rw(psx_cpu_t *c, uint32_t gpa, void **host, uint32_t sz, int store)
{
    if (gpa % sz) { enter_exception(c, store ? 5u : 4u, gpa, 1); return -1; }
    uint32_t w;
    void *p = psx_translate(c->core, gpa, &w);
    if (!p || w == PSX_WIN_BAD) { enter_exception(c, store ? 5u : 4u, gpa, 1); return -1; }
    *host = p;
    return 0;
}

/* 楔4: MMIO 窗命中且挂了 dev → 语义钩(寄存器堆); 否则裸指针。返回 1=走钩 */
static inline int mmio_hooked(psx_cpu_t *c, uint32_t win)
{ return win == PSX_WIN_MMIO && c->core->dev != NULL; }

static int ld_val(psx_cpu_t *c, uint32_t gpa, int sz, uint32_t *val)
{
    void *p;
    if (mem_rw(c, gpa, &p, sz, 0) != 0) return -1;
    uint32_t w = 0; psx_translate(c->core, gpa, &w);
    if (mmio_hooked(c, w)) { *val = psx_dev_read((psx_dev_t *)c->core->dev,
                                gpa - PSX_GPA_MMIO_BASE, sz); return 0; }
    *val = (sz == 1) ? *(uint8_t *)p : (sz == 2) ? *(uint16_t *)p : *(uint32_t *)p;
    return 0;
}
static int st_val(psx_cpu_t *c, uint32_t gpa, int sz, uint32_t val)
{
    void *p;
    if (mem_rw(c, gpa, &p, sz, 1) != 0) return -1;
    uint32_t w = 0; psx_translate(c->core, gpa, &w);
    if (mmio_hooked(c, w)) { psx_dev_write((psx_dev_t *)c->core->dev,
                             gpa - PSX_GPA_MMIO_BASE, val, sz); return 0; }
    if (sz == 1) *(uint8_t *)p = (uint8_t)val;
    else if (sz == 2) *(uint16_t *)p = (uint16_t)val;
    else *(uint32_t *)p = val;
    return 0;
}

#define RS(i) (((i) >> 21) & 31)
#define RT(i) (((i) >> 16) & 31)
#define RD(i) (((i) >> 11) & 31)
#define SA(i) (((i) >> 6) & 31)
#define FUNCT(i) ((i) & 63)
#define IMM(i) ((i) & 0xFFFF)
#define G(n)  c->gpr[n]
#define SETG(n, v) do { if (n) c->gpr[n] = (v); } while (0)

/* 楔5: 中断自动进入链。step 头检查(报告点=下条未执行指令, eret 后重执行它);
 * 条件: status.IE(bit0) && !EXL(bit1) && (I_STAT & I_MASK & 13 源位) */
static int irq_check(psx_cpu_t *c)
{
    psx_dev_t *d = (psx_dev_t *)c->core->dev;
    if (!d) return 0;
    uint32_t st = c->cop0[12];
    if (!(st & 1u) || (st & 2u)) return 0;
    if (!(d->i_stat & d->i_mask & 0x1FFFu)) return 0;
    c->cop0[14] = c->npc;                 /* epc = 未执行指令 */
    c->cop0[13] = (1u << 8);              /* cause: IP8(bit8), ExCode=0 中断 */
    c->cop0[12] |= 2u;
    c->branch_pending = 0;
    uint32_t vec = (st & (1u << 22)) ? 0xBFC00000u : 0x80000000u;
    c->pc  = vec - 4;
    c->npc = vec;
    c->exc_n++;
    return 1;
}

int psx_cpu_step(psx_cpu_t *c)
{
    if (irq_check(c)) { c->cycles++; return 0; }
    uint32_t insn;
    void *fp;
    if (mem_rw(c, c->npc, &fp, 4, 0) != 0) return -1;   /* ibus/对齐 → 地址错 */
    insn = *(uint32_t *)fp;

    uint32_t pend = c->branch_pending, ptarget = c->branch_target;
    c->branch_pending = 0;                              /* 本条=延迟槽, 消费挂起 */
    c->pc = c->npc;
    c->npc = c->pc + 4;

    uint32_t op = insn >> 26, rs = RS(insn), rt = RT(insn), rd = RD(insn),
             sa = SA(insn), f = FUNCT(insn), im = IMM(insn);
    int took = 0; uint32_t tgt = 0;
    void *p;

    switch (op) {
    case 0x00: /* SPECIAL */
        switch (f) {
        case 0x00: SETG(rd, G(rt) << sa); break;
        case 0x02: SETG(rd, G(rt) >> sa); break;
        case 0x03: SETG(rd, (uint32_t)((int32_t)G(rt) >> sa)); break;
        case 0x04: SETG(rd, G(rt) << (G(rs) & 31)); break;
        case 0x06: SETG(rd, G(rt) >> (G(rs) & 31)); break;
        case 0x07: SETG(rd, (uint32_t)((int32_t)G(rt) >> (G(rs) & 31))); break;
        case 0x08: took = 1; tgt = G(rs); break;                    /* jr */
        case 0x09: SETG(rd ? rd : 31, c->npc); took = 1; tgt = G(rs); break; /* jalr */
        case 0x0A: if (G(rt) == 0) SETG(rd, G(rs)); break;          /* movz */
        case 0x0B: if (G(rt) != 0) SETG(rd, G(rs)); break;          /* movn */
        case 0x0C: enter_exception(c, 8u, 0, 0); c->cycles++; return 0;  /* syscall */
        case 0x0D: enter_exception(c, 9u, 0, 0); c->cycles++; return 0;  /* break */
        case 0x10: SETG(rd, c->hi); break;                          /* mfhi */
        case 0x11: c->hi = G(rs); break;                            /* mthi */
        case 0x12: SETG(rd, c->lo); break;                          /* mflo */
        case 0x13: c->lo = G(rs); break;                            /* mtlo */
        case 0x18: { int64_t r = (int64_t)(int32_t)G(rs) * (int32_t)G(rt);   /* mult */
            c->hi = (uint32_t)((uint64_t)r >> 32); c->lo = (uint32_t)r; break; }
        case 0x19: { uint64_t r = (uint64_t)G(rs) * G(rt);          /* multu */
            c->hi = (uint32_t)(r >> 32); c->lo = (uint32_t)r; break; }
        case 0x1A:                                                  /* div */
            if (G(rt) == 0) { c->hi = 0; c->lo = 0xFFFFFFFFu; }
            else { c->lo = (uint32_t)((int32_t)G(rs) / (int32_t)G(rt));
                   c->hi = (uint32_t)((int32_t)G(rs) % (int32_t)G(rt)); }
            break;
        case 0x1B:                                                  /* divu */
            if (G(rt) == 0) { c->hi = 0; c->lo = 0xFFFFFFFFu; }
            else { c->lo = G(rs) / G(rt); c->hi = G(rs) % G(rt); }
            break;
        case 0x20: SETG(rd, (uint32_t)((int32_t)G(rs) + (int32_t)G(rt))); break; /* add(不 trap, 边界已录) */
        case 0x21: SETG(rd, G(rs) + G(rt)); break;
        case 0x22: SETG(rd, (uint32_t)((int32_t)G(rs) - (int32_t)G(rt))); break;
        case 0x23: SETG(rd, G(rs) - G(rt)); break;
        case 0x24: SETG(rd, G(rs) & G(rt)); break;
        case 0x25: SETG(rd, G(rs) | G(rt)); break;
        case 0x26: SETG(rd, G(rs) ^ G(rt)); break;
        case 0x27: SETG(rd, ~(G(rs) | G(rt))); break;               /* nor */
        case 0x2A: SETG(rd, (int32_t)G(rs) < (int32_t)G(rt)); break;
        case 0x2B: SETG(rd, G(rs) < G(rt)); break;
        default: enter_exception(c, 10u, 0, 0); c->cycles++; return 0;
        }
        break;
    case 0x01: /* REGIMM: bltz/bgez/bltzal/bgezal */
        if (rs == 0 || rs == 0x10) took = (int32_t)G(rt) < 0;
        else if (rs == 1 || rs == 0x11) took = (int32_t)G(rt) >= 0;
        else { enter_exception(c, 10u, 0, 0); c->cycles++; return 0; }
        if (rs == 0x10 || rs == 0x11) SETG(31, c->npc);
        if (took) tgt = c->npc + SIGN16(im) * 4;
        break;
    case 0x02: took = 1; tgt = (c->npc & 0xF0000000u) | ((insn & 0x03FFFFFFu) << 2); break;
    case 0x03: SETG(31, c->npc); took = 1;
        tgt = (c->npc & 0xF0000000u) | ((insn & 0x03FFFFFFu) << 2); break;
    case 0x04: took = G(rs) == G(rt); if (took) tgt = c->npc + SIGN16(im) * 4; break;
    case 0x05: took = G(rs) != G(rt); if (took) tgt = c->npc + SIGN16(im) * 4; break;
    case 0x06: took = (int32_t)G(rs) <= 0; if (took) tgt = c->npc + SIGN16(im) * 4; break;
    case 0x07: took = (int32_t)G(rs) > 0;  if (took) tgt = c->npc + SIGN16(im) * 4; break;
    case 0x08: SETG(rt, (uint32_t)((int32_t)G(rs) + (int32_t)SIGN16(im))); break;
    case 0x09: SETG(rt, G(rs) + SIGN16(im)); break;
    case 0x0A: SETG(rt, (int32_t)G(rs) < (int32_t)SIGN16(im)); break;
    case 0x0B: SETG(rt, G(rs) < SIGN16(im)); break;
    case 0x0C: SETG(rt, G(rs) & im); break;
    case 0x0D: SETG(rt, G(rs) | im); break;
    case 0x0E: SETG(rt, G(rs) ^ im); break;
    case 0x0F: SETG(rt, im << 16); break;
    case 0x10: /* COP0 */
        if (rs == 0x10) {                     /* CO special: eret=funct 0x18 */
            if (f == 0x18 || f == 0x10) { /* eret(0x18)/rfe(0x10, PSX rootcall 返回同义) */
                c->cop0[12] &= ~2u;
                c->pc  = c->cop0[14] - 4;
                c->npc = c->cop0[14];
                c->cycles++; return 0; }
            enter_exception(c, 10u, 0, 0); c->cycles++; return 0;
        }
        if (rs == 0) SETG(rt, c->cop0[rd & 15]);        /* mfc0 */
        else if (rs == 4) c->cop0[rd & 15] = G(rt);     /* mtc0 */
        else { enter_exception(c, 10u, 0, 0); c->cycles++; return 0; }
        break;
    case 0x11: /* COP1: 未实现面 */
        enter_exception(c, 10u, 0, 0); c->cycles++; return 0;
    case 0x12: /* COP2 GTE 最小面(楔5/楔6 修): 命令=rs0 且 rt0 且 funct!=0(与 mfc2 $0,$0
                * 冲突面归命令); sw/lw 系 rs0(rt=数据reg)不误伤——BIOS 长跑首卡
                * ac290130=sw $9,0x130($1) 被旧规则吞(战役 wedge1 战果) */
        if (rs == 0) {
            if (rt == 0 && rd == 0 && ((insn >> 6) & 31) == 0) {
                /* 命令域: mfc2 $0,gte[0] 无意义面归命令(SFTRGB funct=0 也须命中) */
                uint32_t sub = insn & 0x3F;
                if (sub == 0x00) {        /* SFTRGB */
                    int code = (int)((c->gte[28] >> 24) & 3u);
                    int sh = (code >= 2) ? 12 : (code == 1 ? 8 : 4);
                    uint32_t v = c->gte[20];  /* IRGB: r20-27/g12-19/b4-11 */
                    uint32_t pk = (((v >> 20) & 0xFFu) << 16) | (((v >> 12) & 0xFFu) << 8)
                                | ((v >> 4) & 0xFFu);
                    for (int k = 0; k < 3; k++) c->gte[8 + k] = pk << sh;
                } else if (sub == 0x09) { /* AVZ3 */
                    uint32_t av = ((c->gte[11] & 0xFFFu) + (c->gte[12] & 0xFFFu)
                                 + (c->gte[13] & 0xFFFu)) / 3u;
                    c->gte[3] = (av << 20) | (av & 1u);
                } else { enter_exception(c, 10u, 0, 0); c->cycles++; return 0; }
                break;
            }
            SETG(rt, c->gte[rd]);                          /* mfc2 */
            break;
        }
        if (rs == 2)      SETG(rt, (rd <= 7) ? ((c->gte[rd] >> 24) & 0xFFu)  /* cfc2 */
                                             : (c->gte[rd] & 0xFFu));
        else if (rs == 4) c->gte[rd] = G(rt);              /* mtc2 */
        else if (rs == 6) c->gte[rd] = (rd <= 7)           /* ctc2 */
            ? ((c->gte[rd] & 0xFFFFFFu) | ((G(rt) & 0xFFu) << 24))
            : ((c->gte[rd] & 0xFFFFFF00u) | (G(rt) & 0xFFu));
        else { enter_exception(c, 10u, 0, 0); c->cycles++; return 0; }
        break;
    case 0x20: { uint32_t v; if (!ld_val(c, G(rs) + SIGN16(im), 1, &v)) SETG(rt, (uint32_t)(int32_t)(int8_t)v); break; } /* lb */
    case 0x21: { uint32_t v; if (!ld_val(c, G(rs) + SIGN16(im), 2, &v)) SETG(rt, SIGN16(v)); break; }              /* lh */
    case 0x23: { uint32_t v; if (!ld_val(c, G(rs) + SIGN16(im), 4, &v)) SETG(rt, v); break; }                       /* lw */
    case 0x24: { uint32_t v; if (!ld_val(c, G(rs) + SIGN16(im), 1, &v)) SETG(rt, v & 0xFFu); break; }               /* lbu */
    case 0x25: { uint32_t v; if (!ld_val(c, G(rs) + SIGN16(im), 2, &v)) SETG(rt, v & 0xFFFFu); break; }             /* lhu */
    case 0x27: { uint32_t v; if (!ld_val(c, G(rs) + SIGN16(im), 4, &v)) SETG(rt, v); break; }                       /* lwu */
    case 0x28: st_val(c, G(rs) + SIGN16(im), 1, G(rt)); break;                                                       /* sb */
    case 0x29: st_val(c, G(rs) + SIGN16(im), 2, G(rt)); break;                                                       /* sh */
    case 0x2B: st_val(c, G(rs) + SIGN16(im), 4, G(rt)); break;                                                       /* sw */
    case 0x2F: break;                                                                   /* cache: 解释核 nop */
    default: enter_exception(c, 10u, 0, 0); c->cycles++; return 0;
    }

    if (took) { c->branch_pending = 1; c->branch_target = tgt; }
    else if (pend) { c->npc = ptarget; } /* 延迟槽跑完: npc 直指 target(不续 pending, 否则目标被当槽二次跳) */
    c->cycles++;
    return 0;
}

void psx_cpu_reset(psx_cpu_t *c, const psx_core_t *core)
{
    memset(c, 0, sizeof *c);
    c->core = core;
    c->pc  = 0xBFC00000u - 4;   /* PSX 硬复位走 BIOS 向量 */
    c->npc = 0xBFC00000u;
}

uint32_t psx_cpu_run(psx_cpu_t *c, uint32_t nsteps)
{
    uint32_t n = 0;
    while (n < nsteps) { psx_cpu_step(c); n++; }
    return n;
}

/* ================= --cpu-selftest: 合成语义套件 (楔3 验收面) ================= */

#define EMIT(a, v) *(volatile uint32_t *)(uintptr_t)(a) = (v)
#define RDG(n)     c.gpr[n]
/* 单次求值断言(铁律: port 断言宏禁双 Eval) */
#define CCHK(cond, name)                                                       \
    do { int _ok = (cond) ? 1 : 0;                                             \
         printf("CPU_TEST %s: %s\n", name, _ok ? "PASS" : "FAIL");             \
         if (!_ok) bad++; } while (0)

static uint32_t I_SPEC(uint32_t f_, uint32_t rs_, uint32_t rt_, uint32_t rd_, uint32_t sa_)
{ return (rs_ << 21) | (rt_ << 16) | (rd_ << 11) | (sa_ << 6) | f_; }
static uint32_t I_RT(uint32_t op_, uint32_t rs_, uint32_t rt_, uint32_t im_)
{ return (op_ << 26) | (rs_ << 21) | (rt_ << 16) | (im_ & 0xFFFF); }
static uint32_t I_JMP(uint32_t op_, uint32_t target_)
{ return (op_ << 26) | ((target_ >> 2) & 0x03FFFFFFu); }
/* 注: I_JMP 传完整原址, 依赖 npc 高 4 位一致(测试程序都在 0x8001_0000 域) */

int cmd_cpu_selftest(int argc, char **argv)
{
    (void)argc; (void)argv;
    psx_core_t core; int bad = 0;
    if (psx_core_init_fixed(&core) != 0) { printf("CPU_EV fixed init FAIL\n"); return 1; }
    if (psx_load_bios(&core) != 0) { printf("CPU_EV bios load FAIL\n"); return 1; }
    psx_cpu_t c;
    const uint32_t P = 0x80010000u, H = 0x80000080u, D0 = 0x80020000u;

    /* --- P1 算术/移位 --- */
    psx_cpu_reset(&c, &core);
    EMIT(P + 0,  I_RT(0x0F, 0, 2, 0x8013));            /* lui $2,0x8013 */
    EMIT(P + 4,  I_RT(0x08, 0, 3, 0xFFFF));            /* addi $3,$0,-1 */
    EMIT(P + 8,  I_SPEC(0x21, 3, 3, 4, 0));            /* addu $4,$3,$3 */
    EMIT(P + 12, I_RT(0x0A, 3, 5, 0));                 /* slti $5,$3,0 */
    EMIT(P + 16, I_RT(0x0D, 0, 7, 0xFFFF));            /* ori $7,0xFFFF */
    EMIT(P + 20, I_RT(0x0E, 7, 7, 0x0F0F));            /* xori */
    EMIT(P + 24, I_SPEC(0x03, 0, 3, 8, 31));           /* sra $8,$3,31 */
    EMIT(P + 28, I_SPEC(0x02, 0, 3, 9, 31));           /* srl $9,$3,31 */
    EMIT(P + 32, I_SPEC(0x00, 0, 2, 10, 4));           /* sll $10,$2,4 */
    EMIT(P + 36, I_SPEC(0x27, 0, 0, 12, 0));           /* nor $12,0,0 */
    EMIT(P + 40, I_SPEC(0x2A, 0, 3, 13, 0));           /* slt $13,$0,$3 */
    c.pc = P - 4; c.npc = P;
    psx_cpu_run(&c, 11);
    CCHK(RDG(2) == 0x80130000u && RDG(3) == 0xFFFFFFFFu && RDG(4) == 0xFFFFFFFEu &&
         RDG(5) == 1u && RDG(7) == 0xF0F0u && RDG(8) == 0xFFFFFFFFu &&
         RDG(9) == 1u && RDG(10) == 0x01300000u && RDG(12) == 0xFFFFFFFFu &&
         RDG(13) == 0u, "p1_arith_shift");
    CCHK(c.pc == P + 40, "p1_pc_walk");

    /* --- P2 乘除 hi/lo --- */
    EMIT(P + 0,  I_RT(0x08, 0, 2, 7));                 /* addi $2=7 */
    EMIT(P + 4,  I_RT(0x09, 0, 3, 0xFFFB));            /* addiu $3=-5 */
    EMIT(P + 8,  I_SPEC(0x18, 2, 3, 0, 0));            /* mult $2,$3 */
    EMIT(P + 12, I_SPEC(0x10, 0, 0, 4, 0));            /* mfhi $4 */
    EMIT(P + 16, I_SPEC(0x12, 0, 0, 5, 0));            /* mflo $5 */
    EMIT(P + 20, I_SPEC(0x19, 3, 2, 0, 0));            /* multu $3,$2 */
    EMIT(P + 24, I_SPEC(0x10, 0, 0, 6, 0));
    EMIT(P + 28, I_SPEC(0x12, 0, 0, 7, 0));
    EMIT(P + 32, I_SPEC(0x1A, 3, 2, 0, 0));            /* div $3,$2 */
    EMIT(P + 36, I_SPEC(0x10, 0, 0, 8, 0));
    EMIT(P + 40, I_SPEC(0x12, 0, 0, 9, 0));
    EMIT(P + 44, I_SPEC(0x1B, 3, 2, 0, 0));            /* divu $3,$2 */
    EMIT(P + 48, I_SPEC(0x10, 0, 0, 10, 0));
    EMIT(P + 52, I_SPEC(0x12, 0, 0, 11, 0));
    c.pc = P - 4; c.npc = P;
    psx_cpu_run(&c, 3);   /* mult 完成即断(div 段会覆 hi/lo——时序坑固化) */
    CCHK(c.hi == 0xFFFFFFFFu && c.lo == 0xFFFFFFDDu, "p2_mult_neg35");
    psx_cpu_run(&c, 11);
    CCHK(RDG(4) == 0xFFFFFFFFu && RDG(5) == 0xFFFFFFDDu &&
         RDG(6) == 6u && RDG(7) == 0xFFFFFFDDu &&
         RDG(8) == 0xFFFFFFFBu && RDG(9) == 0u &&
         RDG(10) == 6u && RDG(11) == 0x24924923u, "p2_hi_lo_all");

    /* --- P3 load/store 全宽度 + 负 disp --- */
    EMIT(P + 0,  I_RT(0x0D, 0, 2, 0xBEEF));            /* ori $2=0xBEEF */
    EMIT(P + 4,  I_RT(0x0F, 0, 3, 0x8002));            /* lui $3,0x8002 */
    EMIT(P + 8,  I_RT(0x2B, 3, 2, 0));                 /* sw $2,0($3) */
    EMIT(P + 12, I_RT(0x29, 3, 2, 4));                 /* sh $2,4($3) */
    EMIT(P + 16, I_RT(0x28, 3, 2, 6));                 /* sb $2,6($3) */
    EMIT(P + 20, I_RT(0x21, 3, 4, 0));                 /* lh $4,0($3) → 0xBEEF 符号扩 */
    EMIT(P + 24, I_RT(0x25, 3, 5, 4));                 /* lhu $5,4($3) */
    EMIT(P + 28, I_RT(0x20, 3, 6, 5));                 /* lb $6,5($3)=0xBE → -66 */
    EMIT(P + 32, I_RT(0x24, 3, 7, 5));                 /* lbu $7,5($3) */
    EMIT(P + 36, I_RT(0x23, 3, 8, 0xFFFFFFFCu & 0xFFFFu)); /* lw $8,-4($3) */
    c.pc = P - 4; c.npc = P;
    psx_cpu_run(&c, 10);
    printf("CPU_EV p3 d0=%x d4=%x d6=%x r4=%x r5=%x r6=%x r7=%x r8=%x\n",
        *(volatile uint32_t *)(uintptr_t)D0, *(volatile uint16_t *)(uintptr_t)(D0+4),
        *(volatile uint8_t *)(uintptr_t)(D0+6), RDG(4), RDG(5), RDG(6), RDG(7), RDG(8));
    CCHK(*(volatile uint32_t *)(uintptr_t)D0 == 0x0000BEEFu &&
         *(volatile uint16_t *)(uintptr_t)(D0 + 4) == 0xBEEFu &&
         *(volatile uint8_t *)(uintptr_t)(D0 + 6) == 0xEFu &&
         RDG(4) == 0xFFFFBEEFu && RDG(5) == 0xBEEFu &&
         RDG(6) == 0xFFFFFFBEu && RDG(7) == 0xBEu && RDG(8) == 0u,
         "p3_mem_widths_negdisp");

    /* --- P4 分支+延迟槽 --- */
    EMIT(P + 0,  I_RT(0x04, 0, 0, 2));                 /* beq 0,0,+2 (跳 1 条) */
    EMIT(P + 4,  I_RT(0x0D, 0, 20, 111));              /* 槽: ori $20=111 必执行 */
    EMIT(P + 8,  I_RT(0x0D, 0, 22, 999));              /* 被跳过 */
    EMIT(P + 12, I_RT(0x0D, 0, 21, 222));              /* 目标: ori $21=222 */
    EMIT(P + 16, I_JMP(0x03, P + 32));                 /* jal sub@P+32 */
    EMIT(P + 20, I_SPEC(0x00, 0, 0, 0, 0));            /* jal 延迟槽 nop */
    EMIT(P + 24, I_RT(0x0D, 0, 25, 777));              /* 返回点 */
    EMIT(P + 28, I_SPEC(0x00, 0, 0, 0, 0));
    EMIT(P + 32, I_RT(0x0D, 0, 23, 555));              /* sub: ori $23=555 */
    EMIT(P + 36, I_SPEC(0x08, 31, 0, 0, 0));           /* jr $31 (返回) */
    EMIT(P + 40, I_RT(0x0D, 0, 26, 444));              /* 子程延迟槽: 必执行 */
    c.pc = P - 4; c.npc = P;
    psx_cpu_run(&c, 10);
    CCHK(RDG(20) == 111u && RDG(21) == 222u && RDG(22) == 0u &&
         RDG(23) == 555u && RDG(25) == 777u &&
         RDG(26) == 444u && RDG(31) == P + 20, "p4_branch_delay_jal");
    EMIT(P + 0, I_RT(0x06, 3, 0, 2));                  /* blez +2 真跳 1 条 */
    EMIT(P + 4, I_RT(0x0D, 0, 27, 1));                 /* 槽仍执行 */
    EMIT(P + 8, I_RT(0x0D, 0, 28, 0));                 /* 跳过 */
    c.gpr[3] = 0xFFFFFFFFu; c.pc = P - 4; c.npc = P; c.branch_pending = 0;
    psx_cpu_run(&c, 3);
    CCHK(RDG(27) == 1u && RDG(28) == 0u, "p4_blez_sign");

    /* --- P5 syscall → handler → eret --- */
    EMIT(H + 0,  0x40027000u);                         /* mfc0 $2,$14 (epc): rd=14→0x7000 */
    EMIT(H + 4,  I_RT(0x09, 2, 2, 4));                 /* addiu $2,+4 */
    EMIT(H + 8,  0x40827000u);                         /* mtc0 $2,$14 */
    EMIT(H + 12, I_RT(0x0D, 0, 4, 0xE0));              /* ori $4=0xE0 */
    EMIT(H + 16, 0x42000018u);                         /* eret */
    EMIT(H + 20, I_SPEC(0x00, 0, 0, 0, 0));            /* 槽 nop */
    EMIT(P + 0,  I_RT(0x08, 0, 2, 42));                /* addi $2=42 */
    EMIT(P + 4,  0x0000000Cu);                         /* syscall */
    EMIT(P + 8,  I_RT(0x08, 0, 3, 1));                 /* addi $3=1 (eret 后) */
    EMIT(P + 12, I_RT(0x08, 0, 4, 2));                 /* addi $4=2 (覆盖 handler 的 0xE0? 时序在 eret 之后) */
    psx_cpu_reset(&c, &core); /* 清 gpr/cop0 现场 */
    c.pc = P - 4; c.npc = P;
    psx_cpu_run(&c, 4);   /* addi, syscall, [vec..mfc0] */
    CCHK(c.cop0[13] == (8u << 2) && c.cop0[14] == P + 4 &&
         (c.cop0[12] & 2u) == 2u && c.exc_n == 1u, "p5_syscall_cause_epc");
    psx_cpu_run(&c, 8);   /* handler 余下 + eret + 主程序 2 条 */
    printf("CPU_EV p5 r2=%x r3=%x r4=%x exl=%x npc=%x\n", RDG(2), RDG(3), RDG(4), c.cop0[12]&1, c.npc);
    CCHK(RDG(2) == P + 8 && RDG(3) == 1u && RDG(4) == 2u &&
         (c.cop0[12] & 2u) == 0u, "p5_eret_resume");

    /* --- P6 非对齐 → 地址错 --- */
    c.gpr[5] = 0x80020001u;
    EMIT(P + 0, I_RT(0x23, 5, 6, 0));                  /* lw $6,0($5) 非对齐 */
    c.pc = P - 4; c.npc = P; c.branch_pending = 0;
    psx_cpu_step(&c);
    CCHK(c.cop0[13] == (4u << 2) && c.cop0[8] == 0x80020001u, "p6_addr_err_load");
    EMIT(P + 0, I_RT(0x2B, 5, 6, 0));                  /* sw 非对齐 */
    c.pc = P - 4; c.npc = P;
    psx_cpu_step(&c);
    CCHK(c.cop0[13] == (5u << 2), "p6_addr_err_store");

    /* --- P7 保留指令(GTE) → excode10 --- */
    EMIT(P + 0, 0xC4000000u);                          /* lwc1(op=0x31) → 楔4 面 */
    c.pc = P - 4; c.npc = P;
    psx_cpu_step(&c);
    CCHK(c.cop0[13] == (10u << 2), "p7_reserved_gte_trap10");

    /* --- P8 真 BIOS 取指 (BLEEM_BIOS 装载态) --- */
    psx_cpu_reset(&c, &core);
    uint32_t insn0 = *(volatile uint32_t *)(uintptr_t)0xBFC00000u;
    if (insn0 != 0) {
        psx_cpu_step(&c);
        uint32_t op0 = insn0 >> 26;
        int ok = (c.pc == 0xBFC00000u && c.npc == 0xBFC00004u);
        if (op0 == 0x0F) ok = ok && c.gpr[RT(insn0)] == (IMM(insn0) << 16);
        CCHK(ok, "p8_bios_first_insn_exec");
        printf("CPU_EV bios_insn0=%08x pc=%08x npc=%08x\n", insn0, c.pc, c.npc);
    } else {
        psx_cpu_step(&c);
        CCHK(c.pc == 0xBFC00000u && c.npc == 0xBFC00004u, "p8_zero_bios_step");
    }
    /* 越窗: pc 走到 BIOS 窗外 RAM 应经 translate 正常 */
    CCHK(psx_translate(&core, 0x80000000u, &(uint32_t){0}) != NULL, "p8_xlat_sane");

    psx_core_free(&core);
    printf(bad ? "PSX CPU SELFTEST FAIL (bad=%d)\n" : "PSX CPU SELFTEST PASS\n", bad);
    return bad ? 2 : 0;
}

/* ================= --bios-run N: BIOS 长跑侦察 (战役工具, task-112) =================
 * 用法: bleem_port --bios-run=200000  (BLEEM_BIOS 必设)
 * 输出: 每道异常(cause/epc/opcode) + 异常 opcode 直方图 + 终态。*/
#define HB 512
int cmd_bios_run(int argc, char **argv)
{
    uint32_t nsteps = 2000;
    for (int i = 1; i < argc; i++)
        if (strncmp(argv[i], "--bios-run=", 11) == 0) nsteps = (uint32_t)strtoul(argv[i] + 11, 0, 10);
    (void)argc; (void)argv;
    psx_core_t core;
    if (psx_core_init_fixed(&core) != 0) { printf("BISO_EV init FAIL\n"); return 1; }
    if (psx_load_bios(&core) != 0) { printf("BISO_EV 需 BLEEM_BIOS(装载失败)\n"); psx_core_free(&core); return 3; }
    psx_cpu_t c; psx_cpu_reset(&c, &core);
    static uint32_t hist[HB];
    uint32_t exc_seen = 0, first_cause = 0, first_epc = 0, first_op = 0;
    uint32_t n = 0;
    while (n < nsteps) {
        uint32_t e0 = c.exc_n;
        psx_cpu_step(&c);
        n++;
        if (c.exc_n != e0) {
            uint32_t epc = c.cop0[14];
            uint32_t op = 0xFFFFFFFFu;
            uint32_t w; void *p = psx_translate(&core, epc & ~3u, &w);
            if (p && w != PSX_WIN_BAD) op = *(volatile uint32_t *)p;
            if (exc_seen == 0) { first_cause = c.cop0[13]; first_epc = epc; first_op = op; }
            if (exc_seen < 12)
                printf("BISO_EXC #%u step=%u cause=%x epc=%x insn=%08x (op=%x rs=%x rt=%x f=%x)\n",
                       exc_seen, n, c.cop0[13], epc, op, op >> 26, (op >> 21) & 31,
                       (op >> 16) & 31, op & 63);
            hist[(op >> 2) % HB]++;
            exc_seen++;
        }
    }
    printf("BISO_EV steps=%u cycles=%u exc_total=%u pc=%x npc=%x v0=%x sp=%x ra=%x\n",
           n, c.cycles, c.exc_n, c.pc, c.npc, c.gpr[2], c.gpr[29], c.gpr[31]);
    printf("BISO_EV first cause=%x epc=%x insn=%08x\n", first_cause, first_epc, first_op);
    /* top-5 异常 opcode 桶 */
    for (int k = 0; k < 5; k++) {
        uint32_t best = 0; int bi = -1;
        for (int i = 0; i < HB; i++) if (hist[i] > best) { best = hist[i]; bi = i; }
        if (bi < 0 || best == 0) break;
        hist[bi] = 0;
        printf("BISO_TOP #%d x%u\n", k, best);
    }
    printf("BISO_EV gpr: ");
    for (int i = 0; i < 32; i++) printf("%08x ", c.gpr[i]);
    printf("\nBIOS RUN DONE\n");
    psx_core_free(&core);
    return 0;
}
