/*
 * psx_dev.c —— 设备寄存器堆 楔4 (task-110, SESSION-121)
 * 偏移域 = gpa - 0x1F800000。未登记偏移: 读 0 / 写丢弃(记录 last_unknown)。
 */
#include "psx_core.h"
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

static psx_sysclk_fn psx_sysclk_provider = NULL;
void psx_dev_set_sysclk(psx_sysclk_fn fn) { psx_sysclk_provider = fn; }

/* task-152: 主钟同步 — 每 timer enable 时从 last_sync 起按 mode 周期推进,
 * target/wrap 语义跨多 tick 循环处理; IRQ 门 ctrl bit6 (psx-spx 公开位义)。
 * 周期: T0-2 mode 0/1/2/3 = 8/16/256/1024 周期(或 h-blank 系, v1 用统一近似);
 * T3(系统时钟模式) = 8。 */
/* task-152 终版: 瞬时值推导模型 — 计数 = (主钟-写基准)/per 折叠(target/wrap 模),
 * 跨 tick 补记 IRQ(仅 enable)。无状态推进 ⇒ 背靠背读天然同值 = 引擎 tick
 * 稳定读契约(80051a08 双读重试环)的根因修复; 真机 BLEEM 同理: 计数是时钟纯函数。 */
void psx_dev_sync(psx_dev_t *d)
{
    uint64_t now = psx_sysclk_provider ? psx_sysclk_provider() : 0u;
    for (int t = 0; t < 4; t++) {
        uint32_t c = d->timer_ctrl[t];
        uint32_t mode = (c >> 1) & 3u;
        uint32_t per;
        if (t == 3) { static const uint32_t p3[4] = {8u,16u,256u,64u}; per = p3[mode]; }
        else        { static const uint32_t pt[4] = {8u,16u,256u,1024u}; per = pt[mode]; }
        int running = (c & 1u) != 0u;
        if (t == 1 && (c & 0x100u)) { per = 564456u; running = 1; }
        /* task-152 真盘实证: 引擎 tick=Timer1 内部 60Hz 时钟源(bit8), 游戏不置
         * bit0 亦期望推进(北斗神拳 [[0x80075054]]=1F801110 稳定读+主循环等差值)。 */
        if (!running || now <= d->timer_last_sync[t]) continue;
        uint64_t ticks = (now - d->timer_last_sync[t]) / per;
        if (ticks == 0u) continue;
        uint32_t span = (c & 0x80u) ? 0x10000u : (d->timer_target[t] ? (d->timer_target[t] & 0xFFFFu) : 0x10000u);
        uint64_t total = (uint64_t)d->timer_cnt[t] + ticks;
        uint32_t cyc = (uint32_t)(total % span);            /* 折叠终值 */
        if (cyc < d->timer_cnt[t] || total >= span) {       /* 至少穿越一次边界 */
            if (c & 0x40u) d->i_stat |= (1u << (10 + t));   /* IRQ 门 ctrl bit6 */
        }
        d->timer_cnt[t] = cyc;
        d->timer_last_sync[t] += ticks * per;
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


/* 楔22: SIO 口(RW 0x1040-0x104F)数据级 trace — 游戏 pad/记忆卡 init 失败
 * 根因链钉字([800750C2]=0 → 驱动拒渲染), cached env 零开销 */
static int sio_tr = -1;
static uint32_t sio_log_n = 0;
static void sio_trace(const char *rw, uint32_t off, uint32_t val, int size)
{
    if (sio_tr < 0) sio_tr = getenv("RETAIL_SIOTR") ? 1 : 0;
    if (!sio_tr || off < 0x1040 || off > 0x104F) return;
    { extern uint32_t g_cur_pc;
      /* 楔23: 只记状态转变或限量(轮询循环按 pc 聚合由外部 uniq 处理) */
      if (sio_log_n++ < 600u)
          fprintf(stderr, "SIOTR %s off=%04x size=%d val=%08x pc=%08x\n", rw, off, size, val, g_cur_pc); }
}
static uint32_t psx_dev_read_i(psx_dev_t *d, uint32_t off, int size);
static void psx_dev_write_i(psx_dev_t *d, uint32_t off, uint32_t val, int size);
uint32_t psx_dev_read(psx_dev_t *d, uint32_t off, int size)
{ uint32_t v = psx_dev_read_i(d, off, size); sio_trace("R", off, v, size); return v; }
static uint32_t psx_dev_read_i(psx_dev_t *d, uint32_t off, int size)
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
    case 0x1130: return d->ram_size;              /* RAM_SIZE 正字位 (task-158 楔19):
        * psx-spx 公开 = 1F801130 读 0=8MB(本模型满幅), 1=2MB。写=忽略(只读)。 */
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
    case 0x1040: case 0x1041: case 0x1042: case 0x1043:
        /* 楔23: 真机 pad 应答序列 0x5A 0x5D(digital 在位)。游戏引擎以非 5A 头判超时
         * (0xFFFF 无垫面被 800666f4 等待环拒 → 走 80052024 失败清理 sh zero [50C2])。 */
        if (d->sio_rxf > 1u) {            /* 楔24: RX 队列头两字节 */
            uint32_t rv = d->sio_rxb0 | (d->sio_rxb1 << 8);
            d->sio_rxb0 = 0xFFu; d->sio_rxb1 = 0xFFu; d->sio_rxf = 0u;
            return rv;                    /* 弹队后=FF 残留(idle) */
        }
        if (d->sio_rxf) {                 /* 尾字节 */
            uint32_t rv = d->sio_rxb0;
            d->sio_rxb0 = 0xFFu; d->sio_rxf = 0u;
            return rv;
        }
        return 0xFFu;                      /* 楔24: 无数据=FF 残留(引擎 flush 阈值 >=0x91 需 FF) */
        break;                            /* (落到 shadow 默认) */
    case 0x1044:
        /* 楔22 实验(门控): LIBSN SIO 引擎(0x8003F670) TX 泵等 1044 非零。
         * RETAIL_SIO1044=1 → 回 TX 计数影子(暂以 104A bit0 语义镜像=1)。 */
        if (d->sio1044) return d->sio1044;
        return d->reg1044;
    case 0x1048: case 0x104C: return 0;
    case 0x104A: { /* 双相位握手: bit0→bit1 逐读推进 (docs/62 状态机化) */
        uint32_t ph = d->phase104a & 1u;
        d->phase104a++;
        return (1u << ph) | (d->reg1044 & 0x100u ? 0x100u : 0u);
    }
    case 0x1100: case 0x1110: case 0x1120: { /* Timer cnt (task-135 全语义 M2):
        * task-158 楔19: 0x1130 剔除 = RAM_SIZE 正字(PSX 仅 T0-T2)
        enable 才走; target 模式(bit7=0)达 target 归零+IRQ; wrap 模式(bit7=1)0xFFFF 回绕+IRQ;
        IRQ 置位受 ctrl bit6 门 (psx-spx 公开位义) */
        psx_dev_sync(d);                                       /* 时钟驱动(task-152):
        * 旧"逐读推进"使连续双读永不相等 → tick 稳定读循环引擎死转。
        * 真机=计数随 CPU 时钟走, 背靠背读同值。 */
        return d->timer_cnt[(off - 0x1100) / 0x10];
    }
    case 0x1104: case 0x1114: case 0x1124:
        return d->timer_ctrl[(off - 0x1104) / 0x10];
    case 0x1108: case 0x1118: case 0x1128:
        return d->timer_target[(off - 0x1108) / 0x10];
    case 0x1801: case 0x1802: case 0x1803:   /* LIBSN 设备握手轮询面: 同步 GPU 即时消费 → 0 */
    case 0x1800: case 0x1810: case 0x1808:
        return 0u;   /* GPUREAD 正字(no$psx 1F801800/1810/1808 读=GPU 读响应 FIFO):
                      * v1 无 GpuRead 响应建模 → 恒 0; 绝不动命令 FIFO (task-141 正字:
                      * 旧 pop 模型会把游戏 GP0 命令流吞掉) */
    case 0x1804: /* GPU 数据端口 word + 就绪字节 (task-151 真盘设备钉字:
                  * LIBSN sb 7,[0x1803] 写 OTG, 轮询 [0x1800]&7==0 / [0x1804] 面;
                  * no$psx GPUSTAT byte1 = ready(0=busy)。字节读由 word 折叠:
                  * 0x1800/0x1804 共享同一 4B 窗 -> 统一返回 ready 字。 */
        return d->gp1_last;
    case 0x1814: return (d->gp0_n < 4 ? (1u << 24) : 0u) | (1u<<26) | (1u<<28) | (1u<<12);
                                /* GPUSTAT 正字(no$psx 1F801814 读): bit24 ready-cmd, bit26 ready-VRAM,
                                 * bit28 ready-DMA-block, bit12 DMA active=0 面; 同步光栅器恒就绪 */
    case 0x181C: return 0u;   /* GPU ACK: 无错 */
    /* SPU 0x1DA4-0x1DAF: task-148 撤专案 → 落全 MMIO 影子回读面(控制字 RMW 语义) */
    case 0x000: case 0x010: case 0x020: case 0x030:
    case 0x040: case 0x050: case 0x060: return d->dma_madr[off / 0x10];
    case 0x004: case 0x014: case 0x024: case 0x034:
    case 0x044: case 0x054: case 0x064: return d->dma_bcr[(off - 0x004) / 0x10];
    case 0x008: case 0x018: case 0x028: case 0x038:
    case 0x048: case 0x058: case 0x068: return d->dma_chcr[(off - 0x008) / 0x10];
    default:
        if (off < 0x10000u) return d->sh[off / 4];
        return 0;
    }
}

void psx_dev_write(psx_dev_t *d, uint32_t off, uint32_t val, int size)
{ sio_trace("W", off, val, size); psx_dev_write_i(d, off, val, size); }
static void psx_dev_write_i(psx_dev_t *d, uint32_t off, uint32_t val, int size)
{
    if (off / 4 < MH) g_wr[off / 4]++;
    switch (off) {
    case 0x1070: d->i_stat &= val; break;              /* docs/57 写 AND 清 */
    case 0x1074: d->i_mask = val; break;
    case 0x1060: d->ram_size = val & 0xFFFFu; break;   /* RAM_SIZE 配置存值 */
    case 0x1130: break;                                 /* RAM_SIZE 只读 (楔19) */
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
    case 0x1048:                                        /* 楔22 门控: baud 写=TX 启动面 */
        /* psx-spx 1044 位义: bits3-6=RX 字节计数。真机 BIOS 于 VBlank 自动 pad 轮询;
         * 本门控直接投递"无垫"完成态: RX=2 字节(0xFFFF 应答) 供游戏读取收敛 init。 */
        /* 楔23 活体钉字(8006587c): 游戏 SIO 引擎等 1044 bit0(TX 就绪)再发送。
         * 真机 bit0=ready; RX 计数 bits3-6 亦就绪=无垫应答在位。 */
        d->sio1044 = (val & 0xFFu) ? 1u : 0u; d->sio_rxf = 0u;  /* 楔24: baud=仅 ready, 应答等 TX 触发 */
        break;
    case 0x104A: d->reg104a_shadow = val; break;        /* 楔22: 104A 控制字 RMW 保留 */
    case 0x1040: case 0x1041: case 0x1042: case 0x1043:
        /* 楔22: 游戏清 RX 状态(sh $zero,($v0) 800520b4 活体钉字) = 请求-响应模型 →
         * 自动投递无垫完成态(RX 2×0xFF + 计数 2 字节) */
        /* 楔24 正字(800666f4 全解): 游戏软件 BUSY 标志 [800D6AF4]&v1 位0 等待清零 =
         * 真机"字节传输完成"。TX 字节写 1040 → 同步完成传输: 清 [6AF4] bit0 +
         * 排 2 字节在位应答(5A 5D) + 1044=ready。 */
        d->sio1044 = 1u | (2u << 3); d->sio_rxf = 2u;
        d->sio_rxb0 = 0x5Au; d->sio_rxb1 = 0x5Du;
        if (d->core) {
            uint32_t bw = 0;
            void *q = psx_translate((const psx_core_t *)d->core, 0x800D6AF4u, &bw);
            if (q && bw == PSX_WIN_RAM) {
                uint32_t f; memcpy(&f, q, 4); f &= ~1u; memcpy(q, &f, 4);
            }
            /* 楔25 实验(RETAIL_SIOBUSY=值): 覆写游戏软 BUSY 状态字 */
            { const char *bs = getenv("RETAIL_SIOBUSY");
              if (bs) { void *r = psx_translate((const psx_core_t *)d->core, 0x800D6AF4u, &bw);
                        if (r && bw == PSX_WIN_RAM) { uint32_t v = (uint32_t)strtoul(bs, NULL, 0); memcpy(r, &v, 4); } } }
        }
        break;
    case 0x1104: case 0x1114: case 0x1124:
        { int t3 = (off - 0x1104) / 0x10;
          d->timer_ctrl[t3] = val & 0xFFFFu;
          d->timer_last_sync[t3] = psx_sysclk_provider ? psx_sysclk_provider() : 0u; } break;
    case 0x1108: case 0x1118: case 0x1128:
        d->timer_target[(off - 0x1108) / 0x10] = val; break;
    case 0x1100: case 0x1110: case 0x1120:
        { int t4 = (off - 0x1100) / 0x10;
          d->timer_cnt[t4] = val & 0xFFFFu;
          d->timer_last_sync[t4] = psx_sysclk_provider ? psx_sysclk_provider() : 0u; } break;
case 0x1810: case 0x1800: case 0x1808:                 /* GP0 字推送 (no$psx 正字 1F801810;
        * 0x1800/0x1808=旧自造兼容) 深 4 满丢弃 */
        if (size >= 4) {                                /* 字推送 = 入队+转发 */
            if (d->gp0_n < 4) d->gp0_fifo[d->gp0_n++] = val;
            d->gpu_port = val;
            if (d->gpu) gpu_gp0_fwd(d->gpu, val);
        } else if (size == 2) {                          /* 半字 = 窗口 LSB 半累积(outb 面) */
            d->gpu_port = (d->gpu_port & 0xFFFF0000u) | (val & 0xFFFFu);
        } else {
            d->gpu_port = (d->gpu_port & 0xFFFFFF00u) | (val & 0xFFu);
        }
        break;
    case 0x1801: case 0x1802: case 0x1803:              /* GPU 数据端口字节别名 (task-151
        * 真盘 LIBSN 96.gp 设备钉字: sb 命令字节/OTG 7 → 0x1800-0x1803,
        * 影子页把 [0x1803] 卡成死 7 = 握手轮询永候)。字节累积成字,
        * MSB(0x1803) 写落 = 命令字齐 → 入 GP0 FIFO 并即时消费(同步 GPU)。 */
        d->gpu_port = (off == 0x1801u) ? ((d->gpu_port & 0xFF00FFFFu) | (val << 16))
                      : (off == 0x1802u) ? ((d->gpu_port & 0xFFFF00FFu) | (val << 8))
                      : ((d->gpu_port & 0x00FFFFFFu) | (val << 24));
        if (off == 0x1803u) {
            if (d->gp0_n < 4) d->gp0_fifo[d->gp0_n++] = d->gpu_port;
            if (d->gpu) gpu_gp0_fwd(d->gpu, d->gpu_port);
        }
        break;    case 0x1814: case 0x1804: d->gp1_last = val; if (d->gpu) gpu_gp1_fwd(d->gpu, val); break;  /* GP1 正字 1F801814 */
    /* SPU 控制面 (task-148 定案): 0x1C00-0x1DFF 区不做专案, 全落 64KB 影子回读面
     * ——真机 SPU 控制字可读回, 游戏做 lhu/ori/sh RMW 握手(北斗神拳 80058b94 实证:
     * 写 [0x1aa]|0x30 后轮询 [0x1aa]&0x30), 专案截胡致写读不一致 → 死等超时。 */
    /* DMA 通道 (no$psx 正字): D0-D6=0x00-0x6F (10/通道: MADR/BCR/CHCR/res),
     * 全局=0xF0-0xFC。写 CHCR(bit30 使能 & bit28 触发)= 立即执行块传输并清
     * bit28 —— 游戏轮询 bit28=0 判完成; 影子面旧行为(存 1 不清)=永久等待
     * (task-145 定性: 北斗神拳 DMA2 OTAG 提交后 GPU 空闲死等)。
     * D2=GPU: bit25=1 → MADR 缓冲 BCR 字送 GP0; 其余通道 v1=传输吸收+完成清位。 */
    case 0x008: case 0x018: case 0x028: case 0x038:
    case 0x048: case 0x058: case 0x068: {
        int ch = (int)((off - 0x008) / 0x10);
        d->dma_chcr[ch] = val;
        if ((val & 0x40000000u) && (val & 0x10000000u)) {   /* enable+trigger */
            if (ch == 3 && !(val & (1u << 25)) && d->core) {   /* CDROM->RAM (task-148):
                * 真盘 PSYQ 读盘闭环 = ReadN 装填 cd_sect → DMA3 写 MADR → 清 CHCR bit28
                * → 置 I_STAT bit2(CDINT) 唤醒等待线程(80051b3c bne 死等闭环)。 */
                uint32_t madr = d->dma_madr[3] & 0x1FFFFFu;
                uint32_t blk  = d->dma_bcr[3];
                uint32_t sects = (blk >> 16) & 0xFFFFu; if (!sects) sects = 1u;
                uint32_t bps   = blk & 0xFFFFu;         if (!bps)  bps = 2048u;
                uint32_t total = sects * bps; if (total > 0x20000u) total = 0x20000u;
                for (uint32_t z = 0; z < total; z += 4u) {
                    uint32_t ww4;
                    void *q = psx_translate((const psx_core_t *)d->core,
                                            0x80000000u + ((madr + z) & 0x1FFFFFu), &ww4);
                    if (!q) break;
                    uint32_t src = z < 2048u ? ((uint32_t *)d->cd_sect)[z / 4u]
                                             : 0u;   /* CDDA/超区=零扇区(诚实面) */
                    *(uint32_t *)q = src;
                }
                d->i_stat |= 0x4u;                    /* CDINT (I_STAT bit2 正字) */
            } else if (ch == 2 && (val & (1u << 25)) && d->gpu && d->core) {  /* GPU C->V */
                uint32_t madr = d->dma_madr[2] & 0x1FFFFFu;
                uint32_t n = d->dma_bcr[2] & 0xFFFFu;       /* v1: 低 16 位=字数 */
                if (n > 0x4000u) n = 0x4000u;
                for (uint32_t z = 0; z < n; z++) {
                    uint32_t w4;
                    void *q = psx_translate((const psx_core_t *)d->core,
                                            0x80000000u + ((madr + z * 4u) & 0x1FFFFFu), &w4);
                    if (q) gpu_gp0_fwd(d->gpu, *(const uint32_t *)q);
                }
            }
            d->dma_chcr[ch] = val & ~0x10000000u;           /* 完成: bit28 清零 */
        }
        break;
    }
    case 0x000: case 0x010: case 0x020: case 0x030:
    case 0x040: case 0x050: case 0x060: {
        int ch = (int)(off / 0x10); d->dma_madr[ch] = val; break;
    }
    case 0x004: case 0x014: case 0x024: case 0x034:
    case 0x044: case 0x054: case 0x064: {
        int ch = (int)((off - 0x004) / 0x10); d->dma_bcr[ch] = val; break;
    }
    default:
        if (off < 0x10000u) { d->sh[off / 4] = val; break; }
        break;
    }
}


/* ============ 清洁房楔3 (task-130): CDROM 命令执行器 + 盘在位面 ============ */
#include "psx_cdrom.h"
#include <stdlib.h>
#include <string.h>
void psx_dev_cd_attach(psx_dev_t *d, void *disc) { d->disc = disc; }
static void gpu_gp0_fwd(void *g, uint32_t w) { psx_gpu_gp0((psx_gpu_t *)g, w); }
static void gpu_gp1_fwd(void *g, uint32_t w) { psx_gpu_gp1((psx_gpu_t *)g, w); }
uint32_t psx_dev_vframes_get(psx_dev_t *d) { return d->vframes; }
void psx_dev_vframe(psx_dev_t *d)
{
    if (!d) return;
    d->i_stat |= 0x1u;      /* VBlank (bit0 正字, 楔8b-2 定案) */
    d->vframes++;
}

void psx_dev_gpu_attach(void *dev, void *gpu) { ((psx_dev_t *)dev)->gpu = gpu; }
void psx_dev_core_attach(void *dev, void *core) { ((psx_dev_t *)dev)->core = core; }

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
        if (!dev.cd_sect_valid || pc != 0x80040100u) bad += 10;
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

uint64_t g_dev_fake_clk = 0;
static uint64_t dev_fake_sysclk(void) { return g_dev_fake_clk; }

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

    /* 3. Timer 时钟驱动语义 (task-152 重写): 计数随主钟走, 背靠背读同值
     * (引擎 tick 稳定读契约), wrap/target 跨 tick 结算, IRQ 门 ctrl bit6 */
    psx_dev_set_sysclk(dev_fake_sysclk);
    g_dev_fake_clk = 1000u;
    dev.timer_ctrl[2] = 0x81u;                      /* enable + wrap 模式, IRQ 关 */
    dev.timer_cnt[2] = 0xFE00u;
    dev.i_stat &= ~(1u << 12);                      /* 初值 0x1220 含 bit12, 差值面自清 */
    g_dev_fake_clk += 8u;                           /* 1 tick */
    uint32_t a = psx_dev_read(&dev, 0x1120, 4);
    uint32_t a2 = psx_dev_read(&dev, 0x1120, 4);    /* 同刻双读必须相等 */
    DCHK(a == a2, "timer2_backread_stable");
    g_dev_fake_clk += 256u * 0x400u;                /* T2 周期=256: 推 0x400 tick → wrap 多次 */
    uint32_t b = psx_dev_read(&dev, 0x1120, 4);
    DCHK(b <= 0xFFFFu && (dev.i_stat & (1u<<12)) == 0u, "timer2_wrap_mode");
    dev.timer_ctrl[2] = 0x41u;                      /* enable + target 模式 + IRQ on */
    dev.timer_target[2] = 0x0300u; dev.timer_cnt[2] = 0x0100u;
    dev.i_stat &= ~(1u << 12);
    g_dev_fake_clk += 256u * 0x300u;                /* 越 target → 归零循环+IRQ */
    (void)psx_dev_read(&dev, 0x1120, 4);
    DCHK(dev.timer_cnt[2] < 0x0300u && (dev.i_stat & (1u<<12)) != 0u, "timer2_target_irq");
    dev.timer_ctrl[2] = 0u; dev.timer_cnt[2] = 0x5555u;   /* disable 不走 */
    DCHK(psx_dev_read(&dev, 0x1120, 4) == 0x5555u && dev.timer_cnt[2] == 0x5555u,
         "timer2_disabled_frozen");

    DCHK(psx_dev_read(&dev, 0x1130, 4) == 0u, "ram_size_1130_reads_0_8mb");   /* task-158 楔19 */
    psx_dev_write(&dev, 0x1130, 0xBEEF2u, 4);
    DCHK(psx_dev_read(&dev, 0x1130, 4) == 0u, "ram_size_1130_write_ignored");
    DCHK(psx_dev_read(&dev, 0x1100, 4) == psx_dev_read(&dev, 0x1100, 4), "timer0_stable_read");

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

    /* 5. GPU GP0 正字口 + GPUSTAT 正字 (task-141: GP0 写=1F801810, STAT 读=1F801814,
     * GPUREAD 不吞命令流, 满丢弃面) */
    psx_dev_write(&dev, 0x1810, 0xE6000400u, 4);        /* E0(显存写命令) */
    psx_dev_write(&dev, 0x1810, 0x00001234u, 4);        /* 参数 */
    psx_dev_write(&dev, 0x1810, 0xFFFFFFFFu, 4);
    psx_dev_write(&dev, 0x1810, 0xFFFFFFFFu, 4);
    psx_dev_write(&dev, 0x1810, 0xDEADBEEFu, 4);        /* 第 5 个=满丢弃(转发仍全量) */
    uint32_t gr = psx_dev_read(&dev, 0x1810, 4);        /* GPUREAD=0, 不动命令 FIFO */
    uint32_t st = psx_dev_read(&dev, 0x1814, 4);        /* GPUSTAT 正字 */
    DCHK(gr == 0u && dev.gp0_n == 4 &&
         (st & (1u<<26)) && (st & (1u<<28)) && (st & (1u<<24)) == 0u,
         "gpu_gp0_port1810_gpustat1814");

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
    /* task-150 正字: R3000 中断向量=0x80000180(BEV=0), 非 0x80000000
     * (旧模型 150 前实证毁 ra/EXL 卡死; handler 0..36 恰好落在 180..1A4,
     *  1A8=rfe 蹦床, 1B0=halt 环) */
    const uint32_t P = 0x80010000u, V0 = 0x80000180u, CNT = 0x80030000u;

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
    EMIT(V0 + 32, 0x42000010u);                        /* rfe (task-153 R3000 正字: 中断出口 IE=旧EXL) */
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
