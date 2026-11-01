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

uint32_t g_watch_lo = 0, g_watch_hi = 0, g_watch_n = 0, g_cm_skip = 0;
int psx_cm_enabled = 1;   /* 楔7b 转正: cache 维护写弃默认 ON; BIOSRUN_NOCM=1 或置 0 关 */
#include <stdlib.h>
int g_watch_post = 0; uint32_t g_watch_done_gpa = 0xFFFFFFFFu, g_watch_last_pc = 0;
static int mem_rw(psx_cpu_t *c, uint32_t gpa, void **host, uint32_t sz, int store)
{
    if (gpa % sz) { enter_exception(c, store ? 5u : 4u, gpa, 1); return -1; }
    uint32_t w;
    void *p = psx_translate(c->core, gpa, &w);
    if (!p || w == PSX_WIN_BAD) {
        enter_exception(c, store ? 5u : 4u, gpa, 1); return -1; }
    /* 楔7b 转正: cache-maintenance 写弃默认 ON——SCPH1001 维护循环
     * (pc 0xBFC01940-0x1A40, 16B 步距扫 D-cache index 空间 [0,0xF80))对
     * RAM 低页的写 = 真机 cache 层行为, 不毁 RAM 向量(golden stub 存活铁证);
     * 本引擎无 cache 层 → 写落 blackhole = 语义对位。BIOSRUN_NOCM=1 显式关。*/
    if (store && psx_cm_enabled) {
        uint32_t norm = gpa & 0x1FFFFFFFu;
        if (c->pc >= 0xBFC01940u && c->pc <= 0xBFC01A40u && norm < 0x1000u) {
            static uint32_t blackhole;
            *host = &blackhole; g_cm_skip++;   /* 写落黑洞=等效写弃(caller 透明) */
            return 0;
        }
    }
    /* 楔4 侦察: RAM 写侦测窗(BIOSRUN_WATCH=lo,hi 归一化地址) */
    if (store && g_watch_n < 400u) {
        uint32_t norm = gpa & 0x1FFFFFFFu;
        if (g_watch_hi && norm >= g_watch_lo && norm < g_watch_hi) {
            g_watch_n++; g_watch_done_gpa = gpa; g_watch_last_pc = c->pc;
        }
    }
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
    if (!(d->i_stat & d->i_mask & 0x3FFFu)) return 0;   /* 0-9 KSEG 源 + 10-13 T0-T3 (task-135: 0x1FFF 漏 T3 位) */
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
    if (mem_rw(c, c->npc, &fp, 4, 0) != 0) return -1;
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
        case 0x09: SETG(rd ? rd : 31, c->npc + 4u); took = 1; tgt = G(rs); break; /* jalr */
        case 0x0A: if (G(rt) == 0) SETG(rd, G(rs)); break;          /* movz */
        case 0x0B: if (G(rt) != 0) SETG(rd, G(rs)); break;          /* movn */
        case 0x0C: {                                             /* syscall */
            uint32_t code = (insn >> 6) & 0xFFFFu;               /* MIPS SYSCALL 立即数=bits21-6 (PSYQ: syscall 0x40 → 0x0000100C; task-141 正字) */
            c->syscall_code = code;

            /* BIOS 几何 [psx-spx]: code 0x20-0x3F InstallHandler 向量优先于 $t2 分派 */
            int vec_done = 0;
            if (code >= 0x20u && code <= 0x3Fu && c->core && c->core->dev) {
                psx_dev_t *d = (psx_dev_t *)c->core->dev;
                uint32_t hv = d->sysvec[code - 0x20u];
                if (hv) { vec_done = 1;
                          c->gpr[31] = c->npc + 4u; c->pc = c->npc - 4u; c->npc = hv;
                          c->branch_pending = 0; }
            }
            if (!vec_done && c->on_rootcall && c->on_rootcall(c) == 1) break; /* HLE 已服务=普通指令续跑 */
            if (!vec_done && code >= 0x20u && code <= 0x3Fu) break; /* 空向量=nop 等价 (BIOS 未装同型) */
            /* 向量页 stub 自噬守卫: pc∈页 且 code 族选择器与 $t2 不合 = 游戏改写页
             * (InitGeom 系写 syscall 0x40/0x48/0x4C 进 0xA0/B0/C0) → nop 续, 防 0x80 死环 */
            if (!vec_done && (c->pc & 0x1FFFFFFFu) < 0x100u) {   /* KSEG0/USEG 别名同判 */
                if (code == 0u) break;
                uint32_t fam = (code >= 0x40u && code <= 0x4Fu) ? (0xA0u + ((code - 0x40u) / 4u) * 0x10u) : 0u;
                if (fam != 0u && fam != c->gpr[10]) break;
            }
            if (0) {
                psx_dev_t *d = (psx_dev_t *)c->core->dev;
                uint32_t h = d->sysvec[code - 0x20u];
                c->cycles++;
                if (h) { c->gpr[31] = c->npc + 4u; c->pc = c->npc - 4u; c->npc = h;
                         c->branch_pending = 0; }
                else { c->pc = c->npc - 4u; c->npc += 4u; }   /* 空向量=nop 等价 */
                return 0;
            }
            enter_exception(c, 8u, 0, 0); c->cycles++; return 0;
        }
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
    case 0x01: /* REGIMM(楔5 修): 操作数=rs, 子操作=rt 字段(MIPS 正字) */
        if (rt == 0 || rt == 16 || rt == 2 || rt == 18) {
            took = (int32_t)G(rs) < 0;
            if (rt == 16 || rt == 18) SETG(31, c->npc + 4u);      /* bltzal (ra=延迟槽后, 真机正字) */
        } else if (rt == 1 || rt == 17 || rt == 3 || rt == 19) {
            took = (int32_t)G(rs) >= 0;
            if (rt == 17 || rt == 19) SETG(31, c->npc + 4u);      /* bgezal (ra=延迟槽后, 真机正字) */
        } else if (rt == 20 || rt == 21) {                          /* PSX reserved */
            enter_exception(c, 10u, 0, 0); c->cycles++; return 0;
        }
        else { enter_exception(c, 10u, 0, 0); c->cycles++; return 0; }
        if (took) tgt = c->npc + SIGN16(im) * 4;
        break;
    case 0x02: took = 1; tgt = (c->npc & 0xF0000000u) | ((insn & 0x03FFFFFFu) << 2); break;
    case 0x03: SETG(31, c->npc + 4u); took = 1;   /* JAL: ra=延迟槽后一条(MIPS 正字; 旧 ra=槽址=返回重放槽毁调用者) */
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
    case 0x22: case 0x26: case 0x2A: case 0x2E: {   /* lwl/lwr/swl/swr (MIPS 未对齐,
                * task-146 真盘钉: 北斗神拳 8a630003 lwl 未实现 = 保留指令风暴 4300 次) */
        uint32_t a32 = G(rs) + SIGN16(im), al = a32 & ~3u, sh = (a32 & 3u) * 8u;
        uint32_t w = 0;
        if (ld_val(c, al, 4, &w) != 0) break;                    /* 已含地址错异常 */
        if (insn >> 26 == 0x22u) { uint32_t m = sh ? ((1u << sh) - 1u) : 0u;   /* lwl */
            SETG(rt, (G(rt) & m) | (w & ~m)); }
        else if (insn >> 26 == 0x26u) { uint32_t m = (sh == 24u) ? 0xFFFFFFFFu : ((1u << (sh + 8u)) - 1u); /* lwr */
            SETG(rt, (G(rt) & ~m) | (w & m)); }
        else {   /* swl/swr: 读改写对齐字 */
            uint32_t nw = w;
            if (insn >> 26 == 0x2Au) { uint32_t m = sh ? ((1u << sh) - 1u) : 0u;
                nw = (w & m) | (G(rt) & ~m); }
            else { uint32_t m = (sh == 24u) ? 0xFFFFFFFFu : ((1u << (sh + 8u)) - 1u);
                nw = (w & ~m) | (G(rt) & m); }
            st_val(c, al, 4, nw);
        }
        break;
    }
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
         RDG(26) == 444u && RDG(31) == P + 24, "p4_branch_delay_jal");   /* ra=延迟槽后正字 */
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

    /* --- P7b REGIMM 标准编码(楔5: BIOS 0x30C8 bltz 现场教训) --- */
    EMIT(P + 0,  0x2404FFFFu);            /* addiu $4,$0,-1 */
    EMIT(P + 4,  0x04800002u);            /* bltz $4,+2 (子操作在 rt=0) */
    EMIT(P + 8,  0x00000000u);            /* nop(延迟槽) */
    EMIT(P + 12, 0x240700BBu);            /* addiu $7,$0,0xBB (不该到) */
    EMIT(P + 16, 0x240700AAu);            /* addiu $7,$0,0xAA (bltz 落点) */
    EMIT(P + 20, 0x04900001u);            /* bltzal $4,+1 (rt=16): 跳 P+28, $31=挂 */
    EMIT(P + 24, 0x00000000u);            /* slot nop */
    EMIT(P + 28, 0x00000000u);            /* bltzal 落点 nop */
    EMIT(P + 32, 0x04A00001u);            /* bgtz? op1 rt=20 reserved 面(另测) */
    c.pc = P - 4; c.npc = P;
    psx_cpu_run(&c, 5);                    /* 0..4: addiu,bltz,nop,addiu(落点P16) */
    CCHK(c.gpr[7] == 0xAAu && c.gpr[4] == 0xFFFFFFFFu, "p7b_bltz_operand_in_rs");
    psx_cpu_step(&c);                      /* P+20 bltzal */
    psx_cpu_step(&c);                      /* slot */
    psx_cpu_step(&c);                      /* 落点 */
    CCHK(c.gpr[31] != 0 && c.pc >= P + 28, "p7b_bltzal_ra_link");

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

int psx_load_golden_vectors(psx_core_t *c, const uint8_t *g)
{
    if (!c || !g || !c->fixed) return -1;
    int n = 0;
    for (uint32_t a = 0x400; a < 0x1800; a += 4) {
        uint32_t v; memcpy(&v, g + a, 4);
        if (v >= PSX_GPA_ROM_BASE && v < PSX_GPA_ROM_BASE + PSX_BIOS_SIZE) {
            *(uint32_t *)(uintptr_t)(PSX_GPA_RAM_BASE + a) = v; n++;
        }
    }
    return n;
}

/* ================= --bios-run N: BIOS 长跑侦察 (战役工具, task-112) =================
 * 用法: bleem_port --bios-run=200000  (BLEEM_BIOS 必设)
 * 输出: 每道异常(cause/epc/opcode) + 异常 opcode 直方图 + 终态。*/
#define HB 512
int cmd_bios_run(int argc, char **argv)
{
    if (getenv("BIOSRUN_NOCM")) psx_cm_enabled = 0;
    if (getenv("BIOSRUN_WATCH")) {
        unsigned lo=0, hi=0; sscanf(getenv("BIOSRUN_WATCH"), "%x,%x", &lo, &hi);
        g_watch_lo = lo; g_watch_hi = hi; g_watch_n = 0;
    }

    uint32_t nsteps = 2000;
    for (int i = 1; i < argc; i++)
        if (strncmp(argv[i], "--bios-run=", 11) == 0) nsteps = (uint32_t)strtoul(argv[i] + 11, 0, 10);
    (void)argc; (void)argv;
    psx_core_t core; static psx_dev_t bdev;
    if (psx_core_init_fixed(&core) != 0) { printf("BISO_EV init FAIL\n"); return 1; }
    if (psx_load_bios(&core) != 0) { printf("BISO_EV 需 BLEEM_BIOS(装载失败)\n"); psx_core_free(&core); return 3; }
    psx_dev_init(&bdev); core.dev = &bdev;   /* 楔6c: 挂设备钩(MMIO trace+CDROM 应答) */
    if (getenv("BIOSRUN_GOLDEN")) {   /* 楔6d: 金标准引导表装载(宿主职责对等) */
        const char *gp = getenv("BIOSRUN_GOLDEN");
        FILE *fg = fopen(gp, "rb");
        if (!fg) { printf("BISO_EV golden open FAIL\n"); return 4; }
        static uint8_t *gbuf; gbuf = malloc(0x200000u);
        if (!gbuf || fread(gbuf, 1, 0x200000u, fg) < 0x1800) { fclose(fg); printf("BISO_EV golden short\n"); return 4; }
        fclose(fg);
        int gn = psx_load_golden_vectors(&core, gbuf);
        printf("BISO_EV golden_vectors=%d\n", gn);
    } else if (getenv("BIOSRUN_COLD")) {
        FILE *fc = fopen(getenv("BIOSRUN_COLD"), "rb");
        static uint8_t *cb; if (fc) { cb = malloc(0x200000u);
            if (cb && fread(cb, 1, 0x200000u, fc) >= 0x800)
                printf("BISO_EV cold_seed_words=%u\n", psx_boot_seed_cold_vectors(&core, cb));
            fclose(fc); }
    }
    if (getenv("BIOSRUN_MMIOTRACE")) psx_dev_trace_reset();
    psx_cpu_t c; psx_cpu_reset(&c, &core);
    static uint32_t hist[HB];
    uint32_t exc_seen = 0, first_cause = 0, first_epc = 0, first_op = 0;
    uint32_t n = 0;
    static uint32_t tpch[4096]; int pch_on = getenv("BIOSRUN_PCHIST") != NULL;
    /* 楔6b: 分段计数 + "跳入零区首瞬"侦测(前一条=跳转且目标指令字=0) */
    uint32_t seg_rom=0,seg_lo=0,seg_main=0,seg_dev=0,seg_other=0, lost=0;
    while (n < nsteps) {
        uint32_t e0 = c.exc_n;
        uint32_t pc_pre = c.npc;              /* 本步将执行的指令地址 */
        uint32_t insn_at = 0xFFFFFFFFu;
        { uint32_t w2; void *p2 = psx_translate(&core, pc_pre, &w2);
          if (p2 && w2 != PSX_WIN_BAD) insn_at = *(volatile uint32_t *)p2; }
        { char *tw = getenv("BIOSRUN_TRACE");
          if (tw) { unsigned t0,t1; sscanf(tw, "%u,%u", &t0, &t1);
            if (n >= t0 && n <= t1)
                printf("TR %u pc=%x insn=%08x a0=%x a2=%x a3=%x ra=%x k0=%x k1=%x t1=%x\n",
                       n, pc_pre, insn_at, c.gpr[4], c.gpr[6], c.gpr[7], c.gpr[31],
                       c.gpr[26], c.gpr[27], c.gpr[9]); } }
        psx_cpu_step(&c);
        { char *tw2 = getenv("BIOSRUN_TRACE");
          if (tw2) { unsigned t0,t1; sscanf(tw2, "%u,%u", &t0, &t1);
            if (n >= t0 && n <= t1)
                printf("TR2 %u npc=%x k0=%x k1=%x at=%x t1=%x pend=%x tgt=%x\n",
                       n, c.npc, c.gpr[26], c.gpr[27], c.gpr[1], c.gpr[9],
                       c.branch_pending, c.branch_target); } }
        if (g_watch_done_gpa != 0xFFFFFFFFu) {
            uint32_t w9; void *p9 = psx_translate(&core, g_watch_done_gpa, &w9);
            uint32_t v9 = (p9 && w9 != PSX_WIN_BAD) ? *(volatile uint32_t *)p9 : 0xDEADu;
            printf("BISO_WATCH pc=%x -> gpa=%x post=%x step=%u\n", g_watch_last_pc, g_watch_done_gpa, v9, n);
            g_watch_done_gpa = 0xFFFFFFFFu;
        }
        if (pch_on) tpch[(c.pc >> 4) & 4095u]++;
        uint32_t q = c.pc;
        if (q >= 0xBFC00000u && q < 0xBFC80000u) seg_rom++;
        else if (q < 0x10000u) seg_lo++;
        else if (q < 0x200000u) seg_main++;
        else if (q >= 0x1F800000u && q < 0x1F810000u) seg_dev++;
        else seg_other++;
        uint32_t jmp = ((insn_at>>26)==2 || (insn_at>>26)==3 ||
                        ((insn_at>>26)==0 && ((insn_at&63)==8 || (insn_at&63)==9)));
        /* 楔4 真目标侦测: 计算跳转实际落点, 目标区 16 字全零 = 跳进零区现场 */
        uint32_t jtgt = 0xFFFFFFFFu;
        if (jmp) {
            uint32_t op6 = insn_at>>26;
            if (op6==2 || op6==3) jtgt = (c.npc & 0xF0000000u) | ((insn_at & 0x03FFFFFFu) << 2);
            else { uint32_t rs = (insn_at>>21)&31; jtgt = c.gpr[rs]; if (jtgt >= 0x80000000u) jtgt -= 0; }
            if (jtgt != 0xFFFFFFFFu) {
                uint32_t nz = 0;
                for (uint32_t k = 0; k < 16u; k++) {
                    uint32_t w4; void *p4 = psx_translate(&core, jtgt + k*4u, &w4);
                    if (p4 && w4 != PSX_WIN_BAD && *(volatile uint32_t *)p4) { nz = 1; break; }
                }
                if (!nz && lost < 8) {
                    printf("BISO_JUMPZERO step=%u from=%x insn=%08x -> tgt=%x ra=%x v0=%x a0=%x t9=%x t8=%x sp=%x\n",
                           n, pc_pre, insn_at, jtgt, c.gpr[31], c.gpr[2], c.gpr[4], c.gpr[25], c.gpr[8], c.gpr[29]);
                    lost++;
                }
            }
        }
        uint32_t nb = 0;
        { uint32_t w3; void *p3 = psx_translate(&core, c.npc, &w3);
          if (p3 && w3 != PSX_WIN_BAD) nb = *(volatile uint32_t *)p3; }
        static uint32_t prev_pc = 0;
        if (getenv("BIOSRUN_MMIOTRACE") && c.pc < 0x10000u &&
            (prev_pc >= 0xBFC00000u || (prev_pc >= 0x10000u && prev_pc < 0x800000u)) )
            printf("BISO_LEAVE_ROM step=%u from=%x -> lowpc=%x ra=%x v0=%x a0=%x t0=%x t1=%x\n",
                   n, prev_pc, c.pc, c.gpr[31], c.gpr[2], c.gpr[4], c.gpr[8], c.gpr[9]);
        prev_pc = c.pc;
        if (jmp && nb == 0 && lost < 6) {   /* nb=target 待 pchist 外再证(jal/j slot 面) */
            printf("BISO_LOST step=%u from=%x insn=%08x -> zero_target=%x ra=%x v0=%x\n",
                   n, c.pc, insn_at, c.npc, c.gpr[31], c.gpr[2]);
            lost++;
        }
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
    if (g_cm_skip) printf("BISO_EV cm_skip=%u\n", g_cm_skip);
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
    printf("BISO_SEG rom=%u ramlo=%u rammain=%u dev=%u other=%u\n",
           seg_rom, seg_lo, seg_main, seg_dev, seg_other);
    if (getenv("BIOSRUN_MMIOTRACE")) psx_dev_trace_dump(14);
    if (getenv("BIOSRUN_DUMP")) {   /* 战役侦察: dump RAM 低段运行态指令流 */
        FILE *fp = fopen(getenv("BIOSRUN_DUMP"), "wb");
        if (fp) { fwrite((void *)(uintptr_t)PSX_GPA_RAM_BASE, 1, 0x200000, fp); fclose(fp); }
    }
    if (pch_on) {
        for (int k = 0; k < 8; k++) {
            uint32_t best = 0; int bi = -1;
            for (int i = 0; i < 4096; i++) if (tpch[i] > best) { best = tpch[i]; bi = i; }
            if (bi < 0 || best == 0) break;
            tpch[bi] = 0;
            printf("BISO_PC #%d pc~0x%x x%u\n", k, (uint32_t)bi << 4, best);
        }
    }
    printf("BISO_EV gpr: ");
    for (int i = 0; i < 32; i++) printf("%08x ", c.gpr[i]);
    printf("\nBIOS RUN DONE\n");
    psx_core_free(&core);
    return 0;
}
