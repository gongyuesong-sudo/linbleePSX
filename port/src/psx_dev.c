/*
 * psx_dev.c —— 设备寄存器堆 楔4 (task-110, SESSION-121)
 * 偏移域 = gpa - 0x1F800000。未登记偏移: 读 0 / 写丢弃(记录 last_unknown)。
 */
#include "psx_dev.h"
#include "psx_gpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct psx_dev_s;
static void cd_cmd_exec(struct psx_dev_s *d, uint8_t cmd);
static void gpu_gp0_fwd(void *g, uint32_t w);
static void gpu_gp1_fwd(void *g, uint32_t w);

/* ---- 楔6c trace 面: MMIO offset 直方图(单实例全局) ---- */
#define MH 0x2500   /* 0x9400/4 */
static uint32_t g_rd[MH], g_wr[MH];
void psx_dev_trace_reset(void){ memset(g_rd,0,sizeof g_rd); memset(g_wr,0,sizeof g_wr); }
void psx_dev_trace_dump(int topn)
{
    for (int k = 0; k < topn; k++) {
        uint32_t best = 0; int bi = -1; int isw = 0;
        for (int i = 0; i < MH; i++) if (g_rd[i] > best) { best = g_rd[i]; bi = i; isw = 0; }
        for (int i = 0; i < MH; i++) if (g_wr[i] > best) { best = g_wr[i]; bi = i; isw = 1; }
        if (bi < 0 || best == 0) break;
        if (isw) g_wr[bi] = 0; else g_rd[bi] = 0;
        printf("DEV_TR %s off=0x%04x(gpa 0x1F80%04x) x%u\n", isw?"W":"R",
               bi*4, bi*4, best);
    }
}

void psx_dev_init(psx_dev_t *d)
{
    memset(d, 0, sizeof *d);
    /* 上电初值: I_STAT 全清, Timer 目标 0xFFFF, 握手相 0 */
    for (int i = 0; i < 4; i++) d->timer_target[i] = 0xFFFFu;
    /* 楔8b-① 上电复位 ACK: 驱动器就绪, CDINT0(I_STAT bit2)置位待 BIOS 使能 */
    d->cd_out[0]=d->cd_out[1]=d->cd_out[2]=d->cd_out[3]=0;
    d->gpu = NULL; d->disc = NULL;
    d->cd_cmd=0; d->cd_cmds=0;
    d->i_stat |= 4u;
}


uint32_t psx_dev_read(psx_dev_t *d, uint32_t off, int size)
{
    (void)size;
    if (off / 4 < MH) g_rd[off / 4]++;
    switch (off) {
    case 0x1070: { /* I_STAT: 返回现值; EVTGEN=每 64 次读翻转置位 vblank 位 0x8 */
        uint32_t v = d->i_stat;
        if (++d->istat_reads % 64u == 0u) d->i_stat |= 0x1u;   /* 楔8b-2 实验: BIOS 轮询位=PSX 正字 bit0 VBlank(0x8 为 BLEEM 宿主私有面, 对 BIOS 无效) */
        return v;
    }
    case 0x1074: return d->i_mask;
    case 0x1060: return d->ram_size;              /* 楔6d RAM_SIZE 回读(BIOS 探测) */
    /* 楔8b-① CDROM CDREAD/CDSTAT(0x1F801820-24, 正字 no$psx 面)
     * 清洁房楔3: 响应 FIFO 化 —— 0x1820-23 逐字节弹出, bit7=队列有货 */
    case 0x1820: case 0x1821: case 0x1822: case 0x1823: {
        if (d->cd_resp_n <= 0) return 0;
        uint8_t v = d->cd_resp[0];
        for (int i = 0; i + 1 < d->cd_resp_n; i++) d->cd_resp[i] = d->cd_resp[i + 1];
        d->cd_resp_n--;
        return v;
    }
    case 0x1824: { /* CDSTAT: bit7=响应出口有货, 恒 0x18(drive ready 系) */
        uint32_t rdy = d->cd_resp_n > 0 ? 0x80u : 0u;
        return rdy | 0x18u; }
    case 0x1825: return 0;                        /* CDSEL/CONFIG 回读 */
    case 0x1044: return d->reg1044;
    case 0x1048: case 0x104C: return 0;
    case 0x104A: { /* 双相位握手: bit0→bit1 逐读推进 (docs/62 状态机化) */
        uint32_t ph = d->phase104a & 1u;
        d->phase104a++;
        return (1u << ph) | (d->reg1044 & 0x100u ? 0x100u : 0u);
    }
    case 0x1100: case 0x1110: case 0x1120: case 0x1130: { /* Timer cnt 读推进 M1 */
        int t = (off - 0x1100) / 0x10;
        uint32_t v = d->timer_cnt[t];
        d->timer_cnt[t] = (v + PSX_TIMER_ADV) & 0xFFFFu;   /* 16 位计数域 */
        return v;
    }
    case 0x1104: case 0x1114: case 0x1124: case 0x1134:
        return d->timer_ctrl[(off - 0x1104) / 0x10];
    case 0x1108: case 0x1118: case 0x1128: case 0x1138:
        return d->timer_target[(off - 0x1108) / 0x10];
    case 0x1800: { /* GP0 读=pop FIFO */
        if (d->gp0_n == 0) return 0u;
        uint32_t v = d->gp0_fifo[d->gp0_rp];
        d->gp0_rp = (d->gp0_rp + 1) & 3; d->gp0_n--;
        return v;
    }
    case 0x1808: return d->gp0_n ? d->gp0_fifo[d->gp0_rp] : 0u; /* GPUDATA 顶不 pop */
    case 0x1804: return d->gp1_last;
    case 0x1810: return 0xB0000000u | (d->gp0_n < 4 ? (1u << 24) : 0u); /* docs/57 GPUOK 真值 */
    default: return 0;
    }
}

void psx_dev_write(psx_dev_t *d, uint32_t off, uint32_t val, int size)
{
    (void)size;
    if (off / 4 < MH) g_wr[off / 4]++;
    switch (off) {
    case 0x1070: d->i_stat &= val; break;              /* docs/57 写 AND 清 */
    case 0x1074: d->i_mask = val; break;
    case 0x1060: d->ram_size = val & 0xFFFFu; break;   /* RAM_SIZE 配置存值 */
    /* 楔8b-① CDROM 命令口: 写=驱动 ack 中断 + 装载响应 FIFO。
     * 无盘面保持既有四参 [0x11,0x14,0xC0,0x42] (no$psx GetStat 无盘正字);
     * 盘在位面 (task-130 升级) 见 cd_cmd_exec —— 响应字节序=ACK 先行。 */
    case 0x1820:
        d->cd_cmd = val & 0xFFu; d->cd_cmds++;
        d->cd_param[0] = (uint8_t)((val >> 8) & 0xFFu);   /* param0 随命令字高 8 位 */
        d->i_stat &= ~4u;                          /* ack CDINT(驱动收令) */
        cd_cmd_exec(d, (uint8_t)(val & 0xFFu));
        d->i_stat |= 4u;                           /* data-ready INT(两档合一) */
        break;
    case 0x1824:
        if (size >= 4) { d->cd_param[1] = (uint8_t)val; d->cd_param[2] = (uint8_t)(val >> 8);
                         d->cd_param[3] = (uint8_t)(val >> 16); d->cd_param[4] = (uint8_t)(val >> 24); }
        else d->cd_param[1] = (uint8_t)val;
        break;
    case 0x1825: d->cd_param[2] = (uint8_t)val; break;
    case 0x1826: d->cd_param[3] = (uint8_t)val; break;
    case 0x1827: d->cd_param[4] = (uint8_t)val; break;
    case 0x1010: case 0x1014: case 0x1018: case 0x101c:
    case 0x1020: case 0x1024: case 0x1028: case 0x102c: break; /* 控制区存写弃(观测面) */
    case 0x1044: d->reg1044 = val; break;              /* docs/62 RMW 窗(简化直存) */
    case 0x1104: case 0x1114: case 0x1124: case 0x1134:
        d->timer_ctrl[(off - 0x1104) / 0x10] = val & 0xFFFFu; break;
    case 0x1108: case 0x1118: case 0x1128: case 0x1138:
        d->timer_target[(off - 0x1108) / 0x10] = val; break;
    case 0x1100: case 0x1110: case 0x1120: case 0x1130:
        d->timer_cnt[(off - 0x1100) / 0x10] = val & 0xFFFFu; break;
    case 0x1800:                                        /* GP0 push 深 4 满丢弃 */
        if (d->gp0_n < 4) { d->gp0_fifo[d->gp0_wp] = val;
            d->gp0_wp = (d->gp0_wp + 1) & 3; d->gp0_n++; }
        if (d->gpu) gpu_gp0_fwd(d->gpu, val);            /* 楔5: 消费者转发 */
        break;
    case 0x1804: d->gp1_last = val; if (d->gpu) gpu_gp1_fwd(d->gpu, val); break;
    default: break;
    }
}


/* ============ 清洁房楔3 (task-130): CDROM 命令执行器 + 盘在位面 ============ */
#include "psx_cdrom.h"
#include <stdlib.h>
#include <string.h>
void psx_dev_cd_attach(psx_dev_t *d, void *disc) { d->disc = disc; }
static void gpu_gp0_fwd(void *g, uint32_t w) { psx_gpu_gp0((psx_gpu_t *)g, w); }
static void gpu_gp1_fwd(void *g, uint32_t w) { psx_gpu_gp1((psx_gpu_t *)g, w); }
void psx_dev_gpu_attach(void *dev, void *gpu) { ((psx_dev_t *)dev)->gpu = gpu; }

static void cd_push(psx_dev_t *d, uint8_t b)
{ if (d->cd_resp_n < 8) d->cd_resp[d->cd_resp_n++] = b; }
static uint32_t cd_param_loc(const psx_dev_t *d)   /* param1..3 = 24-bit LBA LE */
{ return (uint32_t)d->cd_param[1] | ((uint32_t)d->cd_param[2] << 8) | ((uint32_t)d->cd_param[3] << 16); }

static void cd_cmd_exec(struct psx_dev_s *d0, uint8_t cmd)
{ psx_dev_t *d = d0;
    int disc = d->disc != NULL;
    d->cd_resp_n = 0;                              /* 新命令冲刷旧响应(真机面) */
    cd_push(d, 0x11);                              /* ACK 恒先行 (psx-spx 面) */
    if (!disc) { cd_push(d, 0x14); cd_push(d, 0xC0); cd_push(d, 0x42);
                 return; }                         /* 无盘面=全命令同型四参(BIOS 马拉松 task-121 定案保真) */
    switch (cmd) {
    case 0x01: /* GetStat: 无盘四参正字; 有盘=ready/闭盘/零错 七参 */
        if (!disc) { cd_push(d, 0x14); cd_push(d, 0xC0); cd_push(d, 0x42); }
        else { cd_push(d, 0x14); cd_push(d, 0x00); cd_push(d, d->cd_mode);
               cd_push(d, 0x00); cd_push(d, 0x00); cd_push(d, 0x00); cd_push(d, 0x14); }
        break;
    case 0x02: d->cd_loc = cd_param_loc(d); break;              /* SetLoc */
    case 0x15: case 0x16:                                       /* SeekL/P */
        d->cd_loc = cd_param_loc(d); d->cd_sect_valid = 0; break;
    case 0x06: { /* ReadN: v1 同步至帧缓冲 (DMA 接线=task-131) */
        d->cd_sect_valid = 0; d->cd_bytes = 0;
        if (disc) {
            uint32_t nsec = d->cd_param[0] & 7u; if (!nsec) nsec = 1u;
            if (nsec > 8u) nsec = 8u;
            uint8_t tmp[8 * 2048];
            if (psx_disc_read((psx_disc_t *)d->disc, d->cd_loc, nsec, tmp) == 0) {
                memcpy(d->cd_sect, tmp, 2048);
                d->cd_bytes = nsec * 2048u; d->cd_sect_valid = 1;
                d->cd_loc += nsec;                              /* 读后指针推进 */
            }
        }
        break; }
    case 0x0A: d->cd_mode = 0x20; break;                        /* Init */
    case 0x0E: d->cd_mode = d->cd_param[0]; break;              /* SetMode */
    case 0x0F: cd_push(d, d->cd_mode); cd_push(d, 0); cd_push(d, 0); cd_push(d, 0); break; /* GetParam */
    case 0x10: cd_push(d, (uint8_t)d->cd_loc); cd_push(d, (uint8_t)(d->cd_loc >> 8));
               cd_push(d, (uint8_t)(d->cd_loc >> 16)); break;   /* GetLocL */
    case 0x19: cd_push(d, disc ? 0x00 : 0x02); break;           /* Test */
    case 0x56: if (disc) { cd_push(d, 0x94); cd_push(d, 0x09);
                           cd_push(d, 0x19); cd_push(d, 0xC0); } break; /* GetId */
    default: break;                                             /* 其余=仅 ACK (v1 诚实面) */
    }
}

int psx_cd_dev_integration_probe(void)
{
    int bad = 0;   /* 返回=失败检查数 (0=PASS) */
    psx_dev_t dev; psx_dev_init(&dev);
    uint32_t n = 0xA00u; uint8_t *img = calloc(1, n);
    if (!img) return 99;
    psx_cd_synth_exe(img, n);
    if (psx_iso_build("/tmp/hb_probe.iso", img, n, "PSX.EXE", 0) != 0) bad++;
    psx_disc_t disc;
    if (psx_disc_open(&disc, "/tmp/hb_probe.iso") != 0) bad++;
    else {
        psx_dev_cd_attach(&dev, &disc);
        /* Init → 消费 ACK */
        psx_dev_write(&dev, 0x1820, 0x0A, 1);
        if (psx_dev_read(&dev, 0x1820, 1) != 0x11) bad++;
        /* Test: 有盘=00 */
        psx_dev_write(&dev, 0x1820, 0x19, 1);
        if (psx_dev_read(&dev, 0x1820, 1) != 0x11 || psx_dev_read(&dev, 0x1821, 1) != 0x00) bad++;
        /* SetLoc FILE_LBA → ReadN(1): 帧缓冲 = exe 头 (pc@0x10) */
        psx_dev_write(&dev, 0x1824, PSX_ISO_FILE_LBA, 4);
        psx_dev_write(&dev, 0x1820, 0x02, 1);                    /* SetLoc */
        if (psx_dev_read(&dev, 0x1820, 1) != 0x11) bad++;
        psx_dev_write(&dev, 0x1820, 0x06u | (1u << 8), 2);      /* cmd + param0=1 */
        if (psx_dev_read(&dev, 0x1820, 1) != 0x11) bad++;       /* 消 ACK */
        uint32_t pc = 0xDEAD; memcpy(&pc, dev.cd_sect + 0x10, 4);
        if (!dev.cd_sect_valid || pc != 0x80041000u) bad += 10;
        /* GetStat 有盘形态 = [0x11,0x14,0x00,...] 且 STAT bit7 弹前=1 */
        psx_dev_write(&dev, 0x1820, 0x01, 1);
        if ((psx_dev_read(&dev, 0x1824, 1) & 0x80u) == 0) bad++;
        if (psx_dev_read(&dev, 0x1820, 1) != 0x11 || psx_dev_read(&dev, 0x1821, 1) != 0x14 ||
            psx_dev_read(&dev, 0x1822, 1) != 0x00) bad++;
        /* 无盘面回归: 卸载后 GetStat 四参正字 */
        psx_dev_cd_attach(&dev, NULL);
        psx_dev_write(&dev, 0x1820, 0x01, 1);
        if (psx_dev_read(&dev, 0x1820, 1) != 0x11 || psx_dev_read(&dev, 0x1821, 1) != 0x14 ||
            psx_dev_read(&dev, 0x1822, 1) != 0xC0 || psx_dev_read(&dev, 0x1823, 1) != 0x42) bad++;
        psx_disc_close(&disc);
    }
    free(img);
    return bad;
}

/* ================= --dev-selftest (楔4 验收面) ================= */
#include "psx_core.h"
#include "psx_cpu.h"
#include <stdlib.h>
#include <string.h>

#define DCHK(cond, name)                                                       \
    do { int _ok = (cond) ? 1 : 0;                                             \
         printf("DEV_TEST %s: %s\n", name, _ok ? "PASS" : "FAIL");             \
         if (!_ok) bad++; } while (0)
#define EMIT(a, v) *(volatile uint32_t *)(uintptr_t)(a) = (v)
static uint32_t I_RT(uint32_t op_, uint32_t rs_, uint32_t rt_, uint32_t im_)
{ return (op_ << 26) | (rs_ << 21) | (rt_ << 16) | (im_ & 0xFFFF); }
static uint32_t I_SPEC(uint32_t f_, uint32_t rs_, uint32_t rt_, uint32_t rd_, uint32_t sa_)
{ return (rs_ << 21) | (rt_ << 16) | (rd_ << 11) | (sa_ << 6) | f_; }
static uint32_t I_JMP(uint32_t op_, uint32_t target_)
{ return (op_ << 26) | ((target_ >> 2) & 0x03FFFFFFu); }

int cmd_dev_selftest(int argc, char **argv)
{
    (void)argc; (void)argv;
    psx_core_t core; psx_dev_t dev; int bad = 0;
    if (psx_core_init_fixed(&core) != 0) { printf("DEV_EV fixed init FAIL\n"); return 1; }
    if (psx_load_bios(&core) != 0) { printf("DEV_EV bios load FAIL\n"); return 1; }
    psx_dev_init(&dev);
    core.dev = &dev;   /* attach: cpu 的 MMIO ld/st 将走 dev 钩 */

    /* 1. I_STAT 写 AND 清 (docs/57) */
    dev.i_stat = 0xF0F0u;
    psx_dev_write(&dev, 0x1070, 0xF00Fu, 4);
    DCHK(dev.i_stat == 0xF000u, "istat_write_and_clear");

    /* 2. EVTGEN 每 64 读翻转置位 (docs/57) */
    dev.i_stat = 0; dev.istat_reads = 0;
    for (int i = 0; i < 63; i++) psx_dev_read(&dev, 0x1070, 4);
    int before = (dev.i_stat & 8) != 0;
    psx_dev_read(&dev, 0x1070, 4);
    /* 楔8b-2 位定案: vblank 节拍位=0x1(PSX 正字 bit0, guest/BIOS 可见面)——
     * 旧 0x8 系 BLEEM 宿主私有语义混入 guest 可见面(docs/57 判读层次错);
     * 实验记录: 单改 bit0 不解锁 idle 主序(等待条件=复合, GPU/DMA 大楔议题) */
    DCHK(!before && (dev.i_stat & 1u) != 0, "evtgen_64read_vblank_bit0");

    /* 3. Timer 读推进 0x400 16 位域 (docs/63 M1) */
    dev.timer_cnt[2] = 0xFE00u;
    uint32_t a = psx_dev_read(&dev, 0x1120, 4);
    uint32_t b = psx_dev_read(&dev, 0x1120, 4);
    DCHK(a == 0xFE00u && b == 0x0200u, "timer2_advance_wrap16");

    /* 4. C2 握手窗 1044/104A 双相位 (docs/62) */
    dev.phase104a = 0;
    uint32_t h0 = psx_dev_read(&dev, 0x104A, 2);
    uint32_t h1 = psx_dev_read(&dev, 0x104A, 2);
    uint32_t h2 = psx_dev_read(&dev, 0x104A, 2);
    DCHK(h0 == 0x1 && h1 == 0x2 && h2 == 0x1, "c2_handshake_two_phase");

    /* D-CDo CDROM 最小面(楔8b-①): 上电 ACK + GetStat 无盘四参 + STAT 位 */
    psx_dev_t dcd; psx_dev_init(&dcd);
    DCHK((dcd.i_stat & 4u) != 0u, "cdrom_poweron_ack_bit2");      /* init 置位 */
    dev.i_stat &= ~4u;   /* 清 ACK 位(模拟 BIOS handler 清位) */
    psx_dev_write(&dev, 0x1820u, 0x0Bu, 1);                        /* GetStat */
    uint32_t o0 = psx_dev_read(&dev, 0x1820u, 1), o1 = psx_dev_read(&dev, 0x1821u, 1);
    uint32_t o2 = psx_dev_read(&dev, 0x1822u, 1), o3 = psx_dev_read(&dev, 0x1823u, 1);
    uint32_t cst = psx_dev_read(&dev, 0x1824u, 1);
    DCHK(o0 == 0x11u && o1 == 0x14u && o2 == 0xC0u && o3 == 0x42u &&
         (cst & 0x18u) == 0x18u && (dev.i_stat & 4u) != 0u && dev.cd_cmds == 1u,
         "cdrom_getstat_nodisc_F1");

    /* 5. GPU GP0 FIFO push/pop + GPUOK (docs/57 真值) */
    psx_dev_write(&dev, 0x1800, 0xE6000400u, 4);        /* E0(显存写命令) */
    psx_dev_write(&dev, 0x1800, 0x00001234u, 4);        /* 参数 */
    psx_dev_write(&dev, 0x1800, 0xFFFFFFFFu, 4);
    psx_dev_write(&dev, 0x1800, 0xFFFFFFFFu, 4);
    psx_dev_write(&dev, 0x1800, 0xDEADBEEFu, 4);        /* 第 5 个=满丢弃 */
    uint32_t g1 = psx_dev_read(&dev, 0x1800, 4);
    uint32_t st = psx_dev_read(&dev, 0x1810, 4);
    DCHK(g1 == 0xE6000400u && dev.gp0_n == 3 &&
         (st & 0xF0000000u) == 0xB0000000u && (st & 0x01000000u),
         "gpu_gp0_fifo4_gpuok_b0000000");

    /* 6. CPU→dev 联动: guest sw 写 I_MASK, lw 读回 I_STAT 触发 EVTGEN 计数 */
    psx_cpu_t c; psx_cpu_reset(&c, &core);
    const uint32_t P = 0x80010000u;
    EMIT(P + 0, I_RT(0x0F, 0, 3, 0x1F80));              /* lui $3,0x1F80 */
    EMIT(P + 4, I_RT(0x0D, 0, 2, 0x0301));              /* ori $2,0x301 */
    EMIT(P + 8, I_RT(0x2B, 3, 2, 0x1074));              /* sw $2,0x1074($3)=I_MASK */
    EMIT(P + 12, I_RT(0x23, 3, 4, 0x1070));             /* lw $4,0x1070($3)=I_STAT */
    dev.i_stat = 0x1234; dev.istat_reads = 0;
    c.pc = P - 4; c.npc = P;
    psx_cpu_run(&c, 4);
    DCHK(dev.i_mask == 0x301u && c.gpr[4] == 0x1234u &&
         dev.istat_reads == 1u && c.cop0[13] == 0,
         "cpu_mmio_sw_lw_hooked");

    /* 7. rootcall 契约链 (docs/41): 0x600 dispatch → 0x674 表 → handler 摸设备 */
    EMIT(0x80000600, I_SPEC(0x00, 0, 2, 2, 2));         /* sll $2,$2,2 (v0 索引→字节) */
    EMIT(0x80000604, I_RT(0x0F, 0, 8, 0x8000));         /* lui $8,0x8000 */
    EMIT(0x80000608, I_RT(0x0D, 8, 8, 0x0674));         /* ori $8=0x80000674 */
    EMIT(0x8000060C, I_SPEC(0x21, 8, 2, 8, 0));         /* addu $8,$8,$2 */
    EMIT(0x80000610, I_RT(0x23, 8, 9, 0));              /* lw $9,0($8) */
    EMIT(0x80000614, I_SPEC(0x08, 9, 0, 0, 0));         /* jr $9 */
    EMIT(0x80000618, I_SPEC(0x00, 0, 0, 0, 0));         /* 槽 nop */
    EMIT(0x80000700, I_RT(0x09, 7, 7, 1));              /* handler: addiu $7+1 */
    EMIT(0x80000704, I_RT(0x0D, 0, 5, 0x00AA));         /* ori $5=0xAA */
    EMIT(0x80000708, I_RT(0x0F, 0, 6, 0x1F80));         /* lui $6,0x1F80 */
    EMIT(0x8000070C, I_RT(0x2B, 6, 5, 0x1074));         /* sw $5→I_MASK(设备摸) */
    EMIT(0x80000710, I_SPEC(0x08, 31, 0, 0, 0));        /* jr $31 */
    EMIT(0x80000714, I_SPEC(0x00, 0, 0, 0, 0));         /* 槽 nop */
    EMIT(0x80000674, 0x80000700u);                      /* 表[0]=handler */
    psx_cpu_reset(&c, &core);
    c.gpr[2] = 0; c.gpr[7] = 41; c.gpr[31] = 0x80010000u; /* 假返回锚 */
    EMIT(P + 0, I_SPEC(0x08, 31, 0, 0, 0));             /* 返回=jr $31 */
    EMIT(P + 4, I_SPEC(0x00, 0, 0, 0, 0));              /* 槽 nop */
    c.pc = 0x800005FC; c.npc = 0x80000600;              /* 入 rootcall 块 */
    psx_cpu_run(&c, 13);  /* 5 dispatch + jr+槽 + 5 handler + 返回槽 */
    DCHK(c.gpr[7] == 42u && dev.i_mask == 0xAAu &&
         c.gpr[5] == 0xAAu && c.npc == P, "rootcall_600_674_contract_chain");

    /* 8. MMIO 越窗仍 BAD (0x1F809400+) */
    uint32_t w;
    DCHK(psx_translate(&core, 0x1F809400u, &w) == NULL && w == PSX_WIN_BAD,
         "mmio_window_edge_oob");

    psx_core_free(&core);
    printf(bad ? "PSX DEV SELFTEST FAIL (bad=%d)\n" : "PSX DEV SELFTEST PASS\n", bad);
    return bad ? 2 : 0;
}

/* ================= --eng-selftest: 引擎集成面 (楔5 验收) ================= */
#define ECHK(cond, name)                                                       \
    do { int _ok = (cond) ? 1 : 0;                                             \
         printf("ENG_TEST %s: %s\n", name, _ok ? "PASS" : "FAIL");             \
         if (!_ok) bad++; } while (0)

int cmd_eng_selftest(int argc, char **argv)
{
    (void)argc; (void)argv;
    psx_core_t core; psx_dev_t dev; int bad = 0;
    if (psx_core_init_fixed(&core) != 0) { printf("ENG_EV fixed init FAIL\n"); return 1; }
    if (psx_load_bios(&core) != 0) { printf("ENG_EV bios load FAIL\n"); return 1; }
    psx_dev_init(&dev);
    core.dev = &dev;
    psx_cpu_t c;
    const uint32_t P = 0x80010000u, V0 = 0x80000000u, CNT = 0x80030000u;

    /* E1 中断自动进入链: 轮询 64 次 EVTGEN 置位 → IP8 向量 → handler 清位+eret */
    EMIT(P + 0,  I_RT(0x0F, 0, 3, 0x1F80));            /* lui $3,0x1F80 */
    EMIT(P + 4,  I_RT(0x0D, 0, 2, 0x0001));            /* ori $2,1 */
    EMIT(P + 8,  0x40826000u);                         /* mtc0 $2,$12: status.IE=1 */
    EMIT(P + 12, I_RT(0x0D, 0, 2, 0x0001));            /* ori $2,1 (楔8b-2: vblank=bit0 正字) */
    EMIT(P + 16, I_RT(0x2B, 3, 2, 0x1074));            /* sw→I_MASK=0x1 */
    EMIT(P + 20, I_RT(0x08, 0, 9, 0));                 /* addi $9=0 */
    EMIT(P + 24, I_RT(0x23, 3, 4, 0x1070));            /* loop: lw $4,I_STAT */
    EMIT(P + 28, I_RT(0x0C, 4, 5, 0x0001));            /* andi $5,$4,1 */
    EMIT(P + 32, I_RT(0x04, 5, 0, 0xFFFD));            /* beq $5,$0,-3 → loop(P+24) */
    /* vblank handler @0x80000000 */
    EMIT(V0 + 0,  I_RT(0x08, 9, 9, 1));                /* addi $9+1 */
    EMIT(V0 + 4,  I_RT(0x0F, 0, 8, 0x8003));           /* lui $8,0x8003 */
    EMIT(V0 + 8,  I_RT(0x2B, 8, 9, 0));                /* sw $9,[0x80030000] */
    EMIT(V0 + 12, 0x400A6800u);                        /* mfc0 $10,$13 (cause): rd13=0x6800 */
    EMIT(V0 + 16, I_RT(0x2B, 8, 10, 4));               /* sw $10,[+4] */
    EMIT(V0 + 20, I_RT(0x0F, 0, 3, 0x1F80));           /* lui $3,0x1F80 */
    EMIT(V0 + 24, I_RT(0x0D, 0, 7, 0xFFFE));           /* ori $7,0xFFFE */
    EMIT(V0 + 28, I_RT(0x2B, 3, 7, 0x1070));           /* sw AND 清 bit0 */
    EMIT(V0 + 32, 0x42000018u);                        /* eret */
    EMIT(V0 + 36, I_SPEC(0x00, 0, 0, 0, 0));           /* 槽 nop */
    psx_cpu_reset(&c, &core);
    dev.i_stat = 0x1220u; dev.istat_reads = 0u;   /* 楔8b-2: bit0 不在初值(EVTGEN 置位路径实走), bit3 不在初值(清位断言真验) */
    c.pc = P - 4; c.npc = P;
    psx_cpu_run(&c, 280);   /* 4步/轮(lw/andi/beq/槽 nop)*64 + 中断 11 步 */
    uint32_t cnt = *(volatile uint32_t *)(uintptr_t)CNT;
    uint32_t cause_saved = *(volatile uint32_t *)(uintptr_t)(CNT + 4);
    printf("ENG_EV e1 cnt=%u cause=%x reads=%u pc=%x exc=%u\n",
           cnt, cause_saved, dev.istat_reads, c.pc, c.exc_n);
    ECHK(cnt == 1u && c.gpr[9] == 1u && cause_saved == (1u << 8) &&
         dev.istat_reads >= 64u && dev.istat_reads <= 72u &&
         c.exc_n == 1u && (c.pc >> 28) == 8u, "e1_vblank_irq_autoentry");
    ECHK((dev.i_stat & 8u) == 0u && (c.cop0[12] & 3u) == 1u,
         "e1_stat_cleared_ie_on_exl_off");

    /* E2 GTE 最小面: mfc2/mtc2/cfc2/ctc2 ABI + AVZ3 + SFTRGB(布局=本引擎定义面) */
    psx_cpu_reset(&c, &core);
    c.gte[11] = 300; c.gte[12] = 400; c.gte[13] = 500;
    EMIT(P + 0, 0x48000009u);                         /* AVZ3 */
    EMIT(P + 4, 0x48041800u);                         /* mfc2 $4,gte[3](OTZ) */
    c.pc = P - 4; c.npc = P;
    psx_cpu_run(&c, 2);
    ECHK(((c.gte[3] >> 20) & 0xFFFu) == 400u && c.gpr[4] == (400u << 20),
         "e2_avz3_average");
    c.gte[20] = (0x12u << 20) | (0x34u << 12) | (0x56u << 4);   /* IRGB */
    c.gte[28] = 0;                                              /* sf code0 → sh4 */
    EMIT(P + 0, 0x48000000u);                         /* SFTRGB */
    EMIT(P + 4, 0x48044000u);                         /* mfc2 $4,gte[8] */
    c.pc = P - 4; c.npc = P;
    psx_cpu_run(&c, 2);
    ECHK(c.gte[8] == (0x00123456u << 4) && c.gte[10] == (0x00123456u << 4) &&
         c.gpr[4] == (0x00123456u << 4), "e2_sftrgb_rgb_triplet_sh4");
    EMIT(P + 0, I_RT(0x0D, 0, 2, 0x0001));            /* ori $2,1 */
    EMIT(P + 4, 0x48C2E000u);                         /* ctc2 $2,gte[28](控制字节=高8位) */
    EMIT(P + 8, 0x4842E000u);                         /* cfc2 $2,gte[28] */
    c.pc = P - 4; c.npc = P;
    psx_cpu_run(&c, 3);
    ECHK((c.gte[28] & 0xFFu) == 1u && c.gpr[2] == 1u,
         "e2_ctc2_cfc2_control_byte");   /* rd>7 控制字节=低 8 位(本引擎定义面) */

    /* E3 svc 表 37 槽全量 (docs/81) */
    ECHK(PSX_SVC_SLOTS == 37 &&
         (char *)&core.svc[36] - (char *)&core.svc[0] == (int)(36u * sizeof(void *)) &&
         psx_svc_call(&core, 36, 1, 2) == 0xFFFFFFFFu &&
         psx_svc_call(&core, 4, 9, 9) == 0u, "e3_svc_table_37_slots");

    /* E4b RAM_SIZE 写后回读(楔6d: BIOS reset sw 0x1F801060=0xb88 依赖) */
    dev.ram_size = 0;
    EMIT(P + 0, I_RT(0x0F, 0, 2, 0x1F80));            /* lui $2,0x1F80 */
    EMIT(P + 4, I_RT(0x0D, 0, 3, 0x0B88));            /* ori $3,0xB88 */
    EMIT(P + 8,  I_RT(0x2B, 2, 3, 0x1060));           /* sw $3,0x1060($2) */
    EMIT(P + 12, I_RT(0x23, 2, 4, 0x1060));           /* lw $4,0x1060($2) */
    psx_cpu_reset(&c, &core);
    c.pc = P - 4; c.npc = P;
    psx_cpu_run(&c, 4);
    ECHK(dev.ram_size == 0xB88u && c.gpr[4] == 0xB88u, "e4b_ram_size_writeback");

    /* E4c cache 维护写弃双态(楔7b 转正断言; 仅 BIOS 装载态跑) */
    if (core.bios_sha[0]) {
        extern uint32_t g_cm_skip;
        g_cm_skip = 0; psx_cm_enabled = 1;
        psx_cpu_reset(&c, &core);
        c.pc = 0xBFC00000u - 4; c.npc = 0xBFC00000u;
        psx_cpu_run(&c, 90000);
        uint32_t b0v = *(volatile uint32_t *)(uintptr_t)(PSX_GPA_RAM_BASE + 0xB0u);
        uint32_t skip_on = g_cm_skip;
        psx_cm_enabled = 0; g_cm_skip = 0;
        psx_cpu_reset(&c, &core);
        c.pc = 0xBFC00000u - 4; c.npc = 0xBFC00000u;
        psx_cpu_run(&c, 90000);
        uint32_t skip_off = g_cm_skip;
        uint32_t b0v_off = *(volatile uint32_t *)(uintptr_t)(PSX_GPA_RAM_BASE + 0xB0u);
        psx_cm_enabled = 1;
        ECHK(skip_on >= 256u && b0v == 0x3c080000u && skip_off == 0 && b0v_off == 0u,
             "e4c_cm_write_divert_dual_state");
    } else {
        ECHK(1, "e4c_cm_write_divert_dual_state");   /* 无 ROM 面跳过(计数守恒) */
    }

    /* E4 真 BIOS 烟测: 复位跑 2000 步无崩溃(装载态) */
    if (core.bios_sha[0]) {
        psx_cpu_reset(&c, &core);
        psx_cpu_run(&c, 2000);
        printf("ENG_EV e4 cycles=%u pc=%x exc=%u\n", c.cycles, c.pc, c.exc_n);
        ECHK(c.cycles == 2000u, "e4_bios_2000steps_no_crash");
    } else {
        ECHK(1, "e4_bios_smoke_skipped_no_rom");
    }

    psx_core_free(&core);
    printf(bad ? "PSX ENG SELFTEST FAIL (bad=%d)\n" : "PSX ENG SELFTEST PASS\n", bad);
    return bad ? 2 : 0;
}
