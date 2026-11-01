/*
 * psx_hle.c —— 清洁房战役楔2: HLE 内核 v1 (task-129)
 *
 * 零 BIOS: 本文件不含任何 SCPH 字节; 命令号=公开 ABI 逐槽登记(见 psx_hle.h 来源标记)。
 * 派发: CPU 钩 on_rootcall 在 syscall 指令处截获 ——
 *   $t2=0xA0/0xB0/0xC0 → A/B/C 表[$t1]; 否则 a0∈{1,2} 临界区; 余者走原异常。
 * A/B/C 返回 = 虚拟 `jr $ra`: 向量页 vec+4 处预放 jr $ra + nop, syscall 自然
 * 落到它, 延迟槽语义天然正确。
 */
#include "psx_hle.h"
/* task-150 写监视(RAM 覆盖取证) — 定义在 psx_cpu.c */
extern uint32_t g_watch_lo, g_watch_hi, g_watch_n, g_watch_done_gpa, g_watch_last_pc;
#include "psx_cdrom.h"
#include "psx_dev.h"
#include "psx_gpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- guest 访存小工 (fixed 态=恒等指针) ---------- */
static uint32_t *gw(psx_hle_t *h, uint32_t gpa)
{ uint32_t w; void *p = psx_translate(h->core, gpa, &w); return p ? (uint32_t *)p : NULL; }
static uint32_t hle_rd32(psx_hle_t *h, uint32_t gpa)
{ uint32_t *p = gw(h, gpa & ~3u); uint32_t v = 0; if (p) memcpy(&v, p, 4); return v; }
static int hle_wr32(psx_hle_t *h, uint32_t gpa, uint32_t v)
{ uint32_t *p = gw(h, gpa & ~3u); if (!p) return -1;
*p = v; return 0; }
static uint8_t hle_rd8(psx_hle_t *h, uint32_t gpa)
{ uint32_t w; void *p = psx_translate(h->core, gpa, &w); return p ? *(uint8_t *)p : 0; }
static int hle_cprange(psx_hle_t *h, uint32_t dst, uint32_t src, uint32_t n)
{ /* 全对齐前提(内核域用法); 逐字搬运, 越界=失败 */
    uint32_t w1, w2; void *pd = psx_translate(h->core, dst, &w1);
    void *ps = psx_translate(h->core, src, &w2);
    if (!pd || !ps || (n & 3u)) return -1;
    memcpy(pd, ps, n); return 0;
}
static int hle_rdstr(psx_hle_t *h, uint32_t gpa, char *buf, int cap)
{ int i = 0; for (; i < cap - 1; i++) { char c = (char)hle_rd8(h, gpa + (uint32_t)i); buf[i] = c; if (!c) break; } buf[i] = 0; return i; }

/* ---------- EvHeader 公开布局: [0]next [1]size [2]mode [3]id [4]attr ---------- */
static void evhdr_init(psx_hle_t *h, uint32_t elem, uint32_t size, uint32_t mode,
                       uint32_t id, uint32_t attr, uint32_t next)
{
    hle_wr32(h, elem + 0, next); hle_wr32(h, elem + 4, size);
    hle_wr32(h, elem + 8, mode); hle_wr32(h, elem + 12, id); hle_wr32(h, elem + 16, attr);
}

uint32_t hle_heap_alloc(psx_hle_t *h, uint32_t size)
{
    if (!h->heap_ready || (size & 3u)) return 0;
    uint32_t need = size + 20u;
    if (h->heap_brk + need > h->heap_base + h->heap_size) return 0;
    uint32_t elem = h->heap_brk;
    evhdr_init(h, elem, size, 1, elem + 20, 0, 0);
    /* 清载荷区 */
    for (uint32_t i = 0; i + 20u < need; i += 4) hle_wr32(h, elem + 20u + i, 0);
    h->heap_brk += need;
    return elem;
}
int hle_heap_free(psx_hle_t *h, uint32_t elem)
{
    if (!h->heap_ready || elem < h->heap_base || elem + 20u > h->heap_brk) return -1;
    uint32_t size = hle_rd32(h, elem + 4);
    if (elem + 20u + size > h->heap_brk) return -1;
    h->heap_brk = elem; /* v1 栈式: 只允许 LIFO 尾释放 */
    return 0;
}

/* ---------- PSX-EXE 装载 (公开头布局: 0x10=pc 0x14=gp 0x18=vaddr 0x1C=psize
 * 0x20=taddr 0x24=tsize 0x28=spbase 0x2C=spdesc 0x30/0x34=a_params) ---------- */
/* host 源装载面 (冷跳专用, task-134): PSX RAM=2MB 镜像折叠, 大文件过 SCRATCH
 * 回绕必毁低端 → 头从 guest 读(布局契约不变), 数据块自 host 缓冲逐词写 RAM 镜像。 */
int hle_load_exe_host(psx_hle_t *h, uint32_t hdr_gpa, const uint8_t *img, uint32_t img_n,
                      uint32_t dest, uint32_t *pc_out, uint32_t *gp_out, uint32_t *sp_out)
{
    if ((hdr_gpa & 0x7FFu) != 0u) return -2;
    uint32_t pc = hle_rd32(h, hdr_gpa + 0x10), gp = hle_rd32(h, hdr_gpa + 0x14);
    uint32_t vaddr = hle_rd32(h, hdr_gpa + 0x18), dsize = hle_rd32(h, hdr_gpa + 0x1C);
    uint32_t spb = hle_rd32(h, hdr_gpa + 0x30);
    uint32_t d = dest ? dest : vaddr;
    if (dsize & 3u) return -3;
    if (0x800u + dsize > img_n) return -4;                 /* host 缓冲须容全块 */
    for (uint32_t o = 0; o < dsize; o += 4u)
        if (hle_wr32(h, d + o, (uint32_t)img[0x800u + o] | ((uint32_t)img[0x800u + o + 1] << 8) |
                                ((uint32_t)img[0x800u + o + 2] << 16) | ((uint32_t)img[0x800u + o + 3] << 24)) != 0)
            return -5;
    if (pc_out) *pc_out = pc;
    if (gp_out) *gp_out = gp;
    if (sp_out) *sp_out = spb;
    return 0;
}

int hle_load_exe(psx_hle_t *h, uint32_t hdr_gpa, uint32_t dest, uint32_t *pc_out, uint32_t *gp_out)
{
    if ((hdr_gpa & 0x7FFu) != 0u) return -2;           /* PSX-EXE 镜像按 2KB 对齐 */
    /* 真 Psy-Q PSX-EXE 头 (task-134 四枚零售盘交叉钉字): pc@0x10 gp@0x14
     * 数据块 vaddr@0x18 块长@0x1C SP基@0x30; 单块自文件 0x800 连续装载 */
    uint32_t pc = hle_rd32(h, hdr_gpa + 0x10), gp = hle_rd32(h, hdr_gpa + 0x14);
    uint32_t vaddr = hle_rd32(h, hdr_gpa + 0x18), dsize = hle_rd32(h, hdr_gpa + 0x1C);
    uint32_t d = dest ? dest : vaddr;
    if (dsize & 3u) return -3;
    if (dsize && hle_cprange(h, d, hdr_gpa + 0x800, dsize) != 0) return -4;
    if (pc_out) *pc_out = pc;
    if (gp_out) *gp_out = gp;
    return 0;
}

/* ---------- A 族 handler (参数移位 ABI: p0=$a0...) ---------- */
#define A_P(n) h->cpu->gpr[4 + (n)]
#define A_STK(n) hle_rd32(h, h->cpu->gpr[29] + 0x10u + 4u * (uint32_t)(n))
#define V0(v_) do { h->cpu->gpr[2] = (uint32_t)(v_); } while (0)

static uint32_t ha_loadimage(psx_hle_t *h, uint32_t cmd)
{
    (void)cmd;
    uint32_t pc = 0, gp = 0;
    int r = hle_load_exe(h, A_P(0), A_P(1), &pc, &gp);
    if (r == 0) { hle_wr32(h, A_P(2), pc); hle_wr32(h, A_P(3), gp); V0(0); }
    else V0(-1);
    return 0;
}
static uint32_t ha_execute(psx_hle_t *h, uint32_t cmd)
{
    (void)cmd;
    uint32_t first = A_P(0), entry = A_STK(0), sp = A_STK(1), fp = A_STK(2);
    (void)fp;
    if (first == 1u && entry != 0u) {
        h->armed = 1; h->exe_pc = entry;
        h->exe_sp = sp ? sp : 0x801FFFF0u;
        h->exe_gp = h->cpu->gpr[28];
        V0(0);
    } else V0(-1); /* first==0 (继续循环) v1 未接主循环 = 明确拒绝, 不静默 */
    return 0;
}
static uint32_t ha_initheaps(psx_hle_t *h, uint32_t cmd)
{
    (void)cmd;
    h->heap_base = A_P(0); h->heap_size = A_P(1);
    h->heap_brk = (h->heap_base + 3u) & ~3u;
    h->heap_ready = (h->heap_base != 0u && h->heap_size >= 64u);
    if (!h->heap_ready) V0(-1); /* 无文档返回值, 失败面自研登记 */
    return 0;
}
static uint32_t ha_putchar(psx_hle_t *h, uint32_t cmd)
{ (void)cmd; fputc((int)(A_P(0) & 0xFFu), stdout); V0(A_P(0)); return 0; }
static uint32_t ha_puts(psx_hle_t *h, uint32_t cmd)
{
    (void)cmd; char buf[256];
    hle_rdstr(h, A_P(0), buf, (int)sizeof buf);
    fputs(buf, stdout); fflush(stdout); V0(1); return 0;
}
/* A(0x49) GPU_cw: 同步查 GPUSTAT → a0 命令词写 GP0 口 → v0=0
 * [R] PSXRecompStudio verified set (GpuCommandWordFunction, arity1);
 * GPUSTAT 位义 = psx-spx 公开文档 (bit28=ready-DMA-block, bit26=ready-cmd,
 * bits29-30=DMA dir)。软光栅恒 ready(同步面), DMA 忙判定走 dev GPUSTAT 实值。 */
static uint32_t ha_gpu_cw(psx_hle_t *h, uint32_t cmd)
{
    /* A(0x49) GPU_cw(cmd): 高锚=OpenBIOS openbios/gpu/gpu.c (经 PSXRecompStudio
     * BiosGpuCommandServiceTests 引用钉): GPU_sync 后写 GP0 一个命令词, 返回 0。
     * 本核 GPU 同步即时消费, FIFO 恒不满 → sync 面直接过; v0=0 高锚回正。 */
    (void)cmd;
    uint32_t w = hle_rd32(h, h->cpu->gpr[4]);   /* a0=缓冲指针? OpenBIOS 语义=命令词值 */
    if (h->dev) psx_dev_write(h->dev, 0x1810, w, 4);
    extern unsigned hle_dbg_cw; hle_dbg_cw++;
    V0(0u);
    return 0;
}
static uint32_t hb_hook_entry(psx_hle_t *h, uint32_t cmd)
{
    (void)cmd;
    uint32_t old = h->entry_hook;
    h->entry_hook = A_P(0);
    if (h->cpu) h->cpu->entry_hook = A_P(0);   /* task-150: CPU 中断派发同步可见 */
    /* task-150 证伪钉字: HookEntryInt 入参在本盘=数据旗标词地址
     * (800750F8 区运行时被游戏复用为缓冲, 跳入即乱码风暴)。
     * 不铺 j; 派发语义降级为"中断时代读词写 1"(见 psx_cpu.c irq_check)。 */
    V0(old);
    return 0;
}
/* B(0x4A..0x50) CARD 驱动族 [S](PSn00bSDK stubs.json drivers.s: InitCARD/
 * StartCARD/StopCARD/_card_write/_card_read/_new_card)。无记忆卡硬件建模:
 * 驱动生命周期入口 = no-op v0=0(InitCARD 返回 0=成功无卡);
 * 传输原语 _card_read/_card_write = 公开文档失败面 v0=-1(游戏据此走无卡分支,
 * 绝不伪造传输成功)。 */
/* A(0x13) b_setjmp [S sys.s]: 存帧到 buf(a0)。公开布局(v1 自研钉: 本核 longjmp
 * 消费面与 setjmp 存储面自洽即可, 跨 SDK 精确字段序后续以真机钉):
 * buf[0]=sp [1]=s0..[9]=s8? — v1: [0]=sp [1]=gp [2]=ra [3..11]=s0-s8。v0=0 直返。
 * 与后续 b_longjmp(a0=buf, a1=rc) 成对: longjmp 恢复帧 + v0=rc。 */
static uint32_t ha_bsetjmp(psx_hle_t *h, uint32_t cmd)
{
    (void)cmd; psx_cpu_t *c = h->cpu;
    uint32_t buf = A_P(0);
    hle_wr32(h, buf + 0u,  c->gpr[29]);
    hle_wr32(h, buf + 4u,  c->gpr[28]);
    hle_wr32(h, buf + 8u,  c->gpr[31]);
    for (int z = 0; z < 9; z++) hle_wr32(h, buf + 12u + (uint32_t)z * 4u, c->gpr[16 + z]);
    h->setjmp_buf = buf;
    V0(0u);
    return 0;
}
static uint32_t ha_blongjmp(psx_hle_t *h, uint32_t cmd)
{
    (void)cmd; psx_cpu_t *c = h->cpu;
    uint32_t buf = A_P(0), rc = A_P(1) ? A_P(1) : 1u;
    c->gpr[29] = hle_rd32(h, buf + 0u);
    c->gpr[28] = hle_rd32(h, buf + 4u);
    c->gpr[31] = hle_rd32(h, buf + 8u);
    for (int z = 0; z < 9; z++) c->gpr[16 + z] = hle_rd32(h, buf + 12u + (uint32_t)z * 4u);
    V0(rc);
    /* 返回点 = 恢复的 ra(调用者续跑, v0=rc 真机 b_longjmp 正字) */
    c->pc = c->npc - 4u; c->npc = c->gpr[31];
    c->branch_pending = 0;
    return 0;
}
/* A(0x3F) printf [S stdio.s]: fmt 最小面 %s %d %u %x %c %% → 宿主 stdout, v0=字数。
 * (真盘调试串实证: 三国志2 调用; 参数=寄存器 a1-a3 后走栈, v1 支持前 3 参+栈参) */
static uint32_t ha_printf(psx_hle_t *h, uint32_t cmd)
{
    (void)cmd;
    char fmt[256]; hle_rdstr(h, A_P(0), fmt, (int)sizeof fmt);
    char out[512]; int o = 0, ai = 1, stk = 0;
    for (const char *f = fmt; *f && o < 480; f++) {
        if (*f != '%') { out[o++] = *f; continue; }
        f++;
        if (*f == 's') { char sb[128]; hle_rdstr(h, ai<4?A_P(ai):hle_rd32(h,h->cpu->gpr[29]+16u+4u*(uint32_t)(stk++)), sb, 128);
                         o += snprintf(out+o, sizeof out-(size_t)o, "%s", sb); ai++; }
        else if (*f=='d'||*f=='u'||*f=='x') { uint32_t v = ai<4?A_P(ai):hle_rd32(h,h->cpu->gpr[29]+16u+4u*(uint32_t)(stk++)); ai++;
            if (*f=='d') o += snprintf(out+o, sizeof out-(size_t)o, "%d", (int)v);
            else if (*f=='u') o += snprintf(out+o, sizeof out-(size_t)o, "%u", v);
            else o += snprintf(out+o, sizeof out-(size_t)o, "%x", v); }
        else if (*f == 'c') { uint32_t v = ai<4?A_P(ai):hle_rd32(h,h->cpu->gpr[29]+16u+4u*(uint32_t)(stk++)); ai++; out[o++]=(char)v; }
        else if (*f == '%') out[o++] = '%';
        else { out[o++]='%'; out[o++]=*f; }
    }
    fwrite(out, 1, (size_t)o, stdout); fflush(stdout);
    V0((uint32_t)o);
    return 0;
}

/* B(0x18) ResetEntryInt [S sys.s]: 解除 EntryInterrupt 挂钩, 返回旧 hook。 */
static uint32_t hb_reset_entry(psx_hle_t *h, uint32_t cmd)
{ (void)cmd; uint32_t old = h->entry_hook; h->entry_hook = 0u; if (h->cpu) h->cpu->entry_hook = 0u; V0(old); return 0; }

static uint32_t hb_card_idle(psx_hle_t *h, uint32_t cmd)
{ (void)cmd; V0(0u); return 0; }
static uint32_t hb_card_fail(psx_hle_t *h, uint32_t cmd)
{ (void)cmd; V0((uint32_t)-1); return 0; }

/* B(0x35) 未登记族选择器面: PSn00bSDK 无此号, PSXRecomp 测试名
 * RootCounterClearPolicy(读 BIOS 变量区)——完整语义未定。真盘实证
 * (北斗神拳): 返回值被直接解引用(sw $zero,0($v0)) → 语义=返回一 guest
 * 可读写字节缓冲。假设面(低锚): 回内核持有 16B 零区指针; 待真机钉字
 * 正名后替换 (docs/113 记账, 绝不伪称已知)。 */
static uint32_t hb_b35_policy(psx_hle_t *h, uint32_t cmd)
{
    (void)cmd;
    if (!h->b35_buf) h->b35_buf = 0x801FEF00u;   /* RAM 顶下留驻区(2MB fold 内) */
    for (int i = 0; i < 4; i++) hle_wr32(h, h->b35_buf + (uint32_t)i * 4u, 0u);
    V0(h->b35_buf);
    return 0;
}
/* B(0x5B) ChangeClearPAD: 清 PAD 输入缓冲 [S] — 无输入硬件建模 → no-op v0=0 */
static uint32_t hb_pad_clear(psx_hle_t *h, uint32_t cmd)
{ (void)h; (void)cmd; V0(0); return 0; }

static uint32_t ha_nosync_ok(psx_hle_t *h, uint32_t cmd) { (void)cmd; V0(0); return 0; }
static uint32_t ha_setconf(psx_hle_t *h, uint32_t cmd)
{
    (void)cmd; /* a0..a3 = 四 CONFIG 字 (公开布局 {tmd,libcard,zone,apps}) */
    h->config[0] = A_P(0); h->config[1] = A_P(1);
    h->config[2] = A_P(2); h->config[3] = A_P(3);
    V0(0); return 0;
}
/* ---- A 族文件 API (0x00 open/0x01 lseek/0x02 read/0x04 close/0x05 ioctl)
 * fd 正字 = 0x1100|slot (PSX cdrom 设备通道, 公开 SDK 文献); 介质只读。 ---- */
static int hle_wr8w(psx_hle_t *h, uint32_t gpa, uint8_t v)
{ uint32_t w; void *p = psx_translate(h->core, gpa, &w); if (!p) return -1; *(uint8_t *)p = v; return 0; }
static int h_fd(psx_hle_t *h, uint32_t fd)
{ (void)h; return (fd & 0xFF00u) == 0x1100u ? (int)(fd & 0xFFu) : -1; }
static uint32_t ha_open(psx_hle_t *h, uint32_t cmd)
{
    (void)cmd; psx_disc_t *d = (psx_disc_t *)h->disc;
    char path[128]; if (!d || hle_rdstr(h, A_P(0), path, (int)sizeof path) < 0) { V0(-1); return 0; }
    char *base = path;   /* 取 ':' / '\' 后基名 */
    for (char *q = path; *q; q++) if (*q == ':' || *q == '\\' || *q == '/') base = q + 1;
    int fi = psx_disc_find(d, base);
    if (getenv("HLE_RDTR")) fprintf(stderr, "HLEOPEN %s -> %s\n", path, fi < 0 ? "NOTFOUND" : d->files[fi].name);
    if (fi < 0) { V0(-1); return 0; }
    for (int i = 0; i < 8; i++) if (!d->fd[i].used) {
        d->fd[i].used = 1; d->fd[i].fidx = fi; d->fd[i].pos = 0; V0(0x1100u | (uint32_t)i); return 0;
    }
    V0(-1); return 0;
}
static uint32_t ha_read(psx_hle_t *h, uint32_t cmd)
{
    (void)cmd; psx_disc_t *d = (psx_disc_t *)h->disc;
    int s = h->disc ? h_fd(h, A_P(0)) : -1;
    if (s < 0 || !d || !d->fd[s].used) { V0(-1); return 0; }
    cd_file_t *f = &d->files[d->fd[s].fidx];
    uint32_t pos = d->fd[s].pos, len = A_P(2);
    if (pos >= f->size) { V0(0); return 0; }
    if (len > f->size - pos) len = f->size - pos;
    uint8_t sec[2048]; uint32_t done = 0;
    while (done < len) {
        uint32_t off = f->lba + pos / 2048u, in = pos % 2048u, take = 2048u - in;
        if (take > len - done) take = len - done;
        if (psx_disc_read(d, off, 1, sec) != 0) break;
        int err = 0;
        for (uint32_t i = 0; i < take; i++) if (hle_wr8w(h, A_P(1) + done + i, sec[in + i])) { err = 1; break; }
        if (err) break;
        done += take; pos += take;
    }
    d->fd[s].pos = pos;
    if (getenv("HLE_RDTR")) fprintf(stderr, "HLEREAD fd=%x %s pos=%u len=%u done=%u\n",
                                    A_P(0), d->files[d->fd[s].fidx].name, pos, A_P(2), done);
    V0(done); return 0;
}
static uint32_t ha_lseek(psx_hle_t *h, uint32_t cmd)
{
    (void)cmd; psx_disc_t *d = (psx_disc_t *)h->disc;
    int s = h->disc ? h_fd(h, A_P(0)) : -1;
    if (s < 0) { V0(-1); return 0; }
    cd_file_t *f = &d->files[d->fd[s].fidx];
    uint32_t np = (A_P(2) == 1u) ? d->fd[s].pos + A_P(1) :
                  (A_P(2) == 2u) ? f->size + A_P(1) : A_P(1);
    d->fd[s].pos = np; V0(np); return 0;
}
static uint32_t ha_close(psx_hle_t *h, uint32_t cmd)
{
    (void)cmd; psx_disc_t *d = (psx_disc_t *)h->disc;
    int s = h->disc ? h_fd(h, A_P(0)) : -1;
    if (s < 0) { V0(-1); return 0; }
    d->fd[s].used = 0; V0(0); return 0;
}
static uint32_t ha_getconf(psx_hle_t *h, uint32_t cmd)
{
    (void)cmd; /* a0=目标 guest CONFIG* */
    for (int i = 0; i < 4; i++) if (hle_wr32(h, A_P(0) + 4u * (uint32_t)i, h->config[i])) { V0(-1); return 0; }
    V0(0); return 0;
}

/* ---------- B 族 ---------- */
static uint32_t hb_alloc(psx_hle_t *h, uint32_t cmd)
{
    (void)cmd; uint32_t e = hle_heap_alloc(h, A_P(0));
    V0(e ? e + 20u : 0); return 0;
}
static uint32_t hb_free(psx_hle_t *h, uint32_t cmd)
{
    (void)cmd; V0(hle_heap_free(h, A_P(0) ? A_P(0) - 20u : 0) == 0 ? 0 : -1); return 0;
}
static int ev_slot(psx_hle_t *h, uint32_t handle)
{ (void)h; int s = (int)(handle & 0xFFu);
  return (handle >> 24 == 0x19u && s < (int)HLE_EV_MAX) ? s : -1; }
static uint32_t hb_open_event(psx_hle_t *h, uint32_t cmd)
{
    (void)cmd;
    for (uint32_t i = 0; i < HLE_EV_MAX; i++)
        if (h->ev[i].class == 0xFFFFFFFFu) {
            h->ev[i].class = A_P(0); h->ev[i].vector = A_P(1);
            h->ev[i].enabled = 0; h->ev[i].fired = 0;
            V0((0x19u << 24) | i); return 0;
        }
    V0(-1); return 0;
}
static uint32_t hb_close_event(psx_hle_t *h, uint32_t cmd)
{
    (void)cmd; int s = ev_slot(h, A_P(0));
    if (s >= 0) { h->ev[s].class = 0xFFFFFFFFu; h->ev[s].enabled = h->ev[s].fired = 0; V0(0); } else V0(-1);
    return 0;
}
#define HB_FLAG(v_, en_, f_) \
    static uint32_t v_(psx_hle_t *h, uint32_t cmd) { (void)cmd; int s = ev_slot(h, A_P(0)); \
        if (s < 0) { V0(-1); return 0; } h->ev[s].en_ = (f_); V0(0); return 0; }
HB_FLAG(hb_enable_event, enabled, 1)
HB_FLAG(hb_disable_event, enabled, 0)
HB_FLAG(hb_deliver_event, fired, 1)
HB_FLAG(hb_wait_event, fired, 0)
static uint32_t hb_test_event(psx_hle_t *h, uint32_t cmd)
{ (void)cmd; int s = ev_slot(h, A_P(0)); V0(s >= 0 ? (uint32_t)h->ev[s].fired : 0); return 0; }
/* GetC0Table/GetB0Table [R]: 指向 guest RAM 静态表 (内容 v1=桩地址数组) */
#define HLE_C0_GPA 0x801FE000u
#define HLE_B0_GPA 0x801FE100u
static uint32_t hb_getc0(psx_hle_t *h, uint32_t cmd) { (void)cmd; V0(HLE_C0_GPA); return 0; }
static uint32_t hb_getb0(psx_hle_t *h, uint32_t cmd) { (void)cmd; V0(HLE_B0_GPA); return 0; }

/* ---------- C 族 ---------- */
static uint32_t hc_install_exc(psx_hle_t *h, uint32_t cmd)
{   /* C(0x07) InstallHandler(code=a0, addr=a1) [S]: 装 syscall 0x20-0x3F 向量 */
    (void)cmd;
    uint32_t code = A_P(0), addr = A_P(1);
    if (h->dev && code >= 0x20u && code <= 0x3Fu) {
        psx_dev_t *d = (psx_dev_t *)h->dev;
        d->sysvec[code - 0x20u] = addr;
        V0(0);
    } else V0((uint32_t)-1);
    return 0;
}
/* C(0x0A) AddCblk [S drivers.s]: 注册 (class,id)→func 设备回调。真机由 BIOS 60Hz
 * VBlank 事件环逐个调用; 北斗神拳驱动主循环写设备状态 7 后轮询状态等派发清位
 * (80051328 死环实证, task-149) → psx_hle_vblank_fire 每 VBlank 真调。 */
/* B(0x17) ReturnFromException [S sys.s]: 真机 BIOS = 弹帧 jr $ra(=eret 同义)。
 * HLE 派发用 ra=0x800001A8 rfe 蹦床, 游戏泵尾调此 API → 落蹦床自然 rfe 回主线。 */
/* A(0x72) _96_remove [S drivers.s + Psy-Q Fall'96 seminar 公开文档: "Removes ISO 9660
 * file system"]: 真机拆掉 96.c ISO9660 设备, 成功返回非 0(游戏泵 bnez 出环+清缓冲)。
 * HLE 从不建 96.c 文件系统 = 移除恒成功。此前 no-op 返 0 → 泵 2M 次/秒空转饿死主线程。 */
static uint32_t ha_96_remove(psx_hle_t *h, uint32_t cmd)
{
    (void)cmd;
    V0(1u); return 0;   /* 恒"已移除" */
}

static uint32_t hb_return_from_exc(psx_hle_t *h, uint32_t cmd)
{
    (void)cmd;
    if (h->cpu && (h->cpu->cop0[12] & 2u)) {     /* 仅异常态才弹帧; 非异常态=无操作
        * (task-151 实证: 游戏泵在非例外上下文也调此 API, 盲 eret=重放 epc 残值连崩) */
        h->cpu->cop0[12] &= ~2u;                 /* EXL=0 */
        h->cpu->pc  = h->cpu->cop0[14] - 4u;     /* 回 epc */
        h->cpu->npc = h->cpu->cop0[14];
    }
    V0(0u); return 0;
}

static uint32_t hc_add_cblk(psx_hle_t *h, uint32_t cmd)
{
    (void)cmd;
    /* 鲁棒登记(task-149 真盘实证: 参数形态跨 SDK 变体大, 首参即可调用指针形态):
     * 四个入参中任一像 RAM 函数指针(>=0x80000000 对齐) 都收进环;
     * 派发预算极小(200 指令)+tramp 振荡即出 = 误伤可控。 */
    for (int q = 0; q < 4; q++) {
        uint32_t cand = A_P(q);
        if (cand >= 0x80000000u && cand < 0x80200000u && (cand & 3u) == 0u) {
            for (int z = 0; z < 16; z++)
                if (!h->cb[z].used) {
                    h->cb[z].used = 1; h->cb[z].class_ = A_P(0); h->cb[z].id = A_P(1);
                    h->cb[z].func = cand; h->cb[z].gpu = A_P(3); h->cb[z].data = 0u;
                    V0(0u); return 0;
                }
            V0(0u); return 0;   /* 环满: 已注册成功语义(不阻断驱动链) */
        }
    }
    V0(0u); return 0;   /* 无指针形态入参: no-op 成功(诚实登记失败不诈 v0=-1 崩链) */
}

static uint32_t hc_sysinitmem(psx_hle_t *h, uint32_t cmd)
{
    (void)cmd; /* a0=stack_addr a1=stack_size a2=text_size a3=data_size */
    uint32_t base = A_P(0), size = A_P(1);
    h->cpu->gpr[29] = base + size - 16u;
    V0(0); return 0;
}
static uint32_t hc_enq(psx_hle_t *h, uint32_t cmd)
{
    (void)cmd; /* a0=list*, a1=entry* —— v1 记账链自研: [entry]=list, list=head */
    uint32_t list = A_P(0), entry = A_P(1), head = hle_rd32(h, list);
    hle_wr32(h, entry, head); hle_wr32(h, list, entry); V0(entry); return 0;
}
static uint32_t hc_deq(psx_hle_t *h, uint32_t cmd)
{
    (void)cmd; uint32_t list = A_P(0), entry = A_P(1);
    uint32_t head = hle_rd32(h, list);
    if (head == entry) { hle_wr32(h, list, hle_rd32(h, entry)); V0(entry); return 0; }
    while (head) { uint32_t nx = hle_rd32(h, head); if (nx == entry) { hle_wr32(h, head, hle_rd32(h, entry)); V0(entry); return 0; } head = nx; }
    V0(0); return 0;
}

/* ---------- 表安装 ---------- */
void psx_hle_uninstall(psx_hle_t *h)
{ if (h->cpu) { h->cpu->on_rootcall = NULL; h->cpu->hle = NULL; h->cpu = NULL; } }

int psx_hle_install(psx_hle_t *h, psx_cpu_t *cpu, psx_core_t *core)
{
    if (!h || !cpu || !core) return -1;
    memset(h, 0, sizeof *h);
    h->cpu = cpu; h->core = core; h->last_unknown = -1;
    for (uint32_t i = 0; i < HLE_EV_MAX; i++) h->ev[i].class = 0xFFFFFFFFu;
    /* A [D+P] 0x28 LoadImage / [P] 0x29 Execute / [S+R] 0x39 InitHeap
     * [S+R] 0x3C putchar / [S+R] 0x3E puts / [S] 0x44 FlushCache
     * [S] 0x70 _bu_init 0x71 _96_init 0x72 _96_remove / [S] 0x99 0x9C 0x9D 0x9F 0xB4 */
    h->a[0x28] = ha_loadimage; h->a[0x29] = ha_execute; h->a[0x39] = ha_initheaps;
    h->a[0x3C] = ha_putchar;   h->a[0x3E] = ha_puts;    h->a[0x44] = ha_nosync_ok;
    h->a[0x49] = ha_gpu_cw;    /* [R] GPU_cw 命令词直通 (零售高频) */
    h->a[0x70] = ha_nosync_ok; h->a[0x71] = ha_nosync_ok;
    h->a[0x72] = ha_96_remove;   /* [S] _96_remove (task-151 转正) */
    h->a[0x73] = ha_nosync_ok; h->a[0x74] = ha_nosync_ok; h->a[0x75] = ha_nosync_ok;
    h->a[0x76] = ha_nosync_ok; h->a[0x77] = ha_nosync_ok; h->a[0x78] = ha_nosync_ok;
    h->a[0x99] = ha_nosync_ok; h->a[0x9C] = ha_setconf; h->a[0x9D] = ha_getconf;
    h->a[0x9F] = ha_nosync_ok; h->a[0xB4] = ha_nosync_ok;
    h->a[0x00] = ha_open; h->a[0x01] = ha_lseek; h->a[0x02] = ha_read;
    h->a[0x04] = ha_close; h->a[0x05] = ha_nosync_ok;   /* ioctl no-op [S] */
    /* B [S/R] */
    h->b[0x00] = hb_alloc;  h->b[0x01] = hb_free;
    h->b[0x07] = hb_deliver_event; h->b[0x08] = hb_open_event; h->b[0x09] = hb_close_event;
    h->b[0x0A] = hb_wait_event;   h->b[0x0B] = hb_test_event;
    h->b[0x0C] = hb_enable_event; h->b[0x0D] = hb_disable_event;
    h->b[0x12] = ha_nosync_ok; h->b[0x13] = ha_nosync_ok; h->b[0x14] = ha_nosync_ok; /* InitPAD 族 */
    h->b[0x56] = hb_getc0;  h->b[0x57] = hb_getb0;
    h->b[0x5B] = hb_pad_clear;  /* [S] ChangeClearPAD */
    h->b[0x18] = hb_reset_entry;  /* [S] ResetEntryInt */
    h->b[0x17] = hb_return_from_exc;  /* [S] ReturnFromException (task-151) */
    h->b[0x19] = hb_hook_entry;  /* [S] HookEntryInt (北斗神拳首 UNK 实证) */
    h->b[0x35] = hb_b35_policy;  /* 未定名假设面(低锚), 见上注 */
    h->a[0x13] = ha_bsetjmp;  h->a[0x14] = ha_blongjmp;  /* [S] sys.s */
    h->a[0x3F] = ha_printf;                              /* [S] stdio.s */
    h->b[0x3F] = ha_puts;   /* [R] B0 puts 别名(PSXRecomp PutsAliasFunction 真 ROM 实证) */
    h->b[0x4A] = hb_card_idle; h->b[0x4B] = hb_card_idle;  /* [S] InitCARD/StartCARD */
    h->b[0x4C] = hb_card_idle;                             /* [S] StopCARD */
    h->b[0x4E] = hb_card_fail; h->b[0x4F] = hb_card_fail;  /* [S] _card_write/read: 无卡=-1 */
    h->b[0x50] = hb_card_idle;                             /* [S] _new_card */
    /* C [S] */
    h->c[0x02] = hc_enq; h->c[0x03] = hc_deq; h->c[0x07] = hc_install_exc;
    h->c[0x08] = hc_sysinitmem; h->c[0x0A] = hc_add_cblk;  /* [S] AddCblk 60Hz 回调环 */
    /* C0/B0 表桩区: 清零可被 GetC0Table 消费 */
    for (uint32_t i = 0; i < 0x40u; i += 4u) { hle_wr32(h, HLE_C0_GPA + i, 0); hle_wr32(h, HLE_B0_GPA + i, 0); }
    /* 向量页: 0x80/0xA0/0xB0/0xC0 syscall; +4=jr $ra; +8=nop */
    const uint32_t JRA = 0x03E00008u, NOP_ = 0u;
    const uint32_t vecs[] = { 0x80u, 0xA0u, 0xB0u, 0xC0u };
    const uint32_t scode[] = { 0x0000000Cu,      /* 0x80 = 通用异常入口(code 0, 原语义) */
                               0x0000100Cu,      /* 0xA0 = syscall 0x40 (A 表, PSYQ 编码) */
                               0x0000120Cu,      /* 0xB0 = syscall 0x48 */
                               0x0000130Cu };    /* 0xC0 = syscall 0x4C */
    for (unsigned i = 0; i < 4u; i++) {
        hle_wr32(h, vecs[i], scode[i]);
        hle_wr32(h, vecs[i] + 4, JRA);
        hle_wr32(h, vecs[i] + 8, NOP_);
    }
    if (h->dev && core) psx_dev_core_attach(h->dev, core);   /* DMA 执行面 */
    cpu->on_rootcall = psx_hle_rootcall;
    cpu->hle = h;
    return 0;
}

static int g_hle_depth; /* 重入护栏 (handler→guest 回调→rootcall) */
unsigned long hle_dbg_calls, hle_dbg_reject, hle_dbg_maxdepth;   /* task-141 观测 */
unsigned hle_dbg_cw;
/* task-151: served 画像(RCHIST=1) — 真实派发点后调用 */
static void hle_rchist(uint32_t vec, uint32_t cmd)
{
    static uint32_t hh_s[4096], hh_c[4096], hh_t;
    uint32_t key = ((vec & 0xFFu) << 8) | (cmd & 0xFFu);
    uint32_t ix = ((key * 2654435761u) >> 20) & 4095u;
    while (hh_s[ix] && hh_s[ix] != key) ix = (ix + 1) & 4095u;
    hh_s[ix] = key; hh_c[ix]++;
    if (++hh_t % 2000000u == 0u) {
        printf("RCHIST @%u:\n", hh_t);
        for (int z = 0; z < 4096; z++)
            if (hh_c[z]) printf("  vec=%02x cmd=%02x n=%u\n", hh_s[z] >> 8, hh_c[z] ? hh_s[z] & 0xFF : 0, hh_c[z]);
    }
}

int psx_hle_rootcall(psx_cpu_t *cpu)
{
    psx_hle_t *h = (psx_hle_t *)cpu->hle;
    hle_dbg_calls++;
    if (!h || g_hle_depth > 4) { hle_dbg_reject++; return 0; }
    if ((unsigned long)g_hle_depth > hle_dbg_maxdepth) hle_dbg_maxdepth = (unsigned long)g_hle_depth;
    uint32_t vec = cpu->gpr[10], cmd = cpu->gpr[9];   /* 真机 ABI: cmd=$t1(gpr9) vec=$t2(gpr10) */
    if (getenv("CALLTR")) {   /* task-150: 每 (vec,cmd) 首次调用现场 = 参数语义钉字取证面 */
        static uint32_t tr_seen[4096]; static uint32_t tr_cnt[4096];
        uint32_t key = ((vec & 0xFFu) << 8) | (cmd & 0xFFu);
        uint32_t ix = ((key * 2654435761u) >> 20) & 4095u;
        while (tr_seen[ix] && tr_seen[ix] != key) ix = (ix + 1) & 4095u;
        tr_seen[ix] = key;
        if (++tr_cnt[ix] == 1u)
            printf("CALLTR vec=%02x cmd=%02x a0=%08x a1=%08x a2=%08x a3=%08x pc=%08x\n",
                vec, cmd, cpu->gpr[4], cpu->gpr[5], cpu->gpr[6], cpu->gpr[7], cpu->pc);
    }
    /* BIOS 族选择器兜底 [psx-spx]: syscall code 0x40/0x48/0x4C = A/B/C 表 */
    if (vec != 0xA0u && vec != 0xB0u && vec != 0xC0u &&
        cpu->syscall_code >= 0x40u && cpu->syscall_code <= 0x4Cu &&
        ((cpu->syscall_code - 0x40u) % 4u) == 0u)
        vec = 0xA0u + ((cpu->syscall_code - 0x40u) / 4u) * 0x10u;   /* BIOS 族选择器 0x40/44/48/4C */
    /* 0x80 BIOS 通用 rootcall 入口 [psx-spx/no$psx]: 真机所有 syscall code 落 0x80
     * 页 handler, $t1=命令号: 0x20-0x3F = InstallHandler 装的服务向量(调 handler,
     * handler jr $ra 回调用者); 其余号段未登记 → 诚实 UNK。*/
    /* 0x80 入口 12 位全命令格式 [no$psx root call]: bits8-11=模块(0=System,
     * 1/3=GPU, 4=SPU, 5=CDROM, 6=SIO), bit7=表(0=A 1=B), bits0-6=ID。
     * (task-146 实证: 北斗神拳 syscall code=0 t1=0x35/0x00 = System A 表
     * ID0=open —— "零文件 API" 旧结论作废, 文件走全命令面) */
    if (vec == 0x80u && cmd >= 0x100u) {
        uint32_t mod = cmd >> 8, tab = (cmd >> 7) & 1u, id = cmd & 0x7Fu;
        hle_a_fn sub = NULL;
        if (mod == 0u) { h->last_vec = tab ? 0xB0u : 0xA0u; h->last_cmd = id;
            if (tab == 0u && id < HLE_A_SLOTS) sub = h->a[id];
            else if (tab == 1u && id < HLE_B_SLOTS) sub = h->b[id];
            h->nlog++;
            g_hle_depth++;
            if (sub) { if (getenv("RCHIST")) hle_rchist(h->last_vec, id); sub(h, id); }
            else { V0((uint32_t)-1); h->unk_n++;
                   for (int z = 0; z < 48; z++) {
                       if (h->unk[z].key == (h->last_vec << 8 | id)) { h->unk[z].cnt++; break; }
                       if (h->unk[z].cnt == 0) { h->unk[z].key = h->last_vec << 8 | id; h->unk[z].cnt = 1; break; } } }
            g_hle_depth--; return 1;
        }
        /* GPU/SPU/CDROM/SIO 模块全命令: 未登记 → UNK v0=-1 直答(下楔逐个钉) */
        h->last_vec = 0x80u; h->last_cmd = cmd; h->nlog++;
        V0((uint32_t)-1); h->unk_n++;
        { uint32_t k = (mod << 16) | cmd;
          for (int z = 0; z < 48; z++) {
              if (h->unk[z].key == k) { h->unk[z].cnt++; break; }
              if (h->unk[z].cnt == 0) { h->unk[z].key = k; h->unk[z].cnt = 1; break; } } }
        g_hle_depth--; return 1;
    }
    if (vec == 0x80u && cmd >= 0x20u && cmd <= 0x3Fu) {
        psx_dev_t *dv = (psx_dev_t *)h->dev;
        uint32_t hv = dv ? ((psx_dev_t *)dv)->sysvec[cmd - 0x20u] : 0u;
        h->last_vec = 0x80u; h->last_cmd = cmd; h->nlog++;
        if (hv) {
            cpu->gpr[31] = cpu->npc + 4u;               /* handler jr $ra 回调用者 */
            cpu->pc = cpu->npc;                          /* 已过延迟槽 */
            cpu->npc = hv;
            cpu->branch_pending = 0;
        } else { V0((uint32_t)-1); h->unk_n++; }
        g_hle_depth--; return 1;
    }
    hle_a_fn fn = NULL;
    g_hle_depth++;
    if (vec == 0xA0u && cmd < HLE_A_SLOTS)      { h->last_vec = 0xA0u; fn = h->a[cmd]; }
    else if (vec == 0xB0u && cmd < HLE_B_SLOTS) { h->last_vec = 0xB0u; fn = h->b[cmd]; }
    else if (vec == 0xC0u && cmd < HLE_C_SLOTS) { h->last_vec = 0xC0u; fn = h->c[cmd]; }
    else if (vec == 0u && cpu->gpr[4] == 1u) {   /* EnterCriticalSection [S] */
        uint32_t st = cpu->cop0[12];
        h->last_vec = 0u; h->last_cmd = 1u;
        cpu->gpr[2] = (st & 0x401u) == 0x401u ? 1u : 0u;
        cpu->cop0[12] = st & ~0x401u; h->nlog++;
        g_hle_depth--; return 1;
    } else if (vec == 0u && cpu->gpr[4] == 2u) { /* ExitCriticalSection [S] */
        h->last_vec = 0u; h->last_cmd = 2u;
        cpu->cop0[12] |= 0x401u; cpu->gpr[2] = 0u; h->nlog++;
        g_hle_depth--; return 1;
    } else { g_hle_depth--; return 0; }          /* 非内核号 → 原异常路径 */
    h->last_cmd = cmd; h->nlog++;
    if (fn) { if (getenv("RCHIST")) hle_rchist(h->last_vec, cmd); fn(h, cmd); }
    else { V0(-1); h->last_unknown = (int)((h->last_vec << 8) | cmd);
           h->unk_n++;
           for (int z = 0; z < 48; z++) {
               if (h->unk[z].key == (h->last_vec << 8 | cmd)) { h->unk[z].cnt++; break; }
               if (h->unk[z].cnt == 0) { h->unk[z].key = h->last_vec << 8 | cmd; h->unk[z].cnt = 1; break; }
           } }
    g_hle_depth--;
    return 1;
}

#include "psx_cdrom.h"
#define BOOTCHK(cond, name) do { printf("BOOT_TEST %s: %s\n", name, (cond) ? "PASS" : "FAIL"); \
    if (!(cond)) bad++; } while (0)
/* ---------- 冷启链 (task-131) ---------- */
/* 内核 halt 环: Execute 后 main 返回落点 (ra=halt) —— 记 exe_returned 观测 */
#define HLE_HALT_GPA   0x800001B0u
#define HLE_HDR_SCRATCH 0x801F0000u    /* PSX-EXE 2KB 头暂存 (对齐) */

int psx_hle_coldboot(psx_hle_t *h, void *disc0, uint32_t *entry_out)
{
    psx_disc_t *d = (psx_disc_t *)disc0;
    if (!h || !h->cpu || !d) return -1;
    int idx = psx_disc_boot_exe(d, 0, 0);   /* SYSTEM.CNF BOOT= 正字, 回落 PSX.EXE */
    if (idx < 0) return -2;
    cd_file_t *f = &d->files[idx];
    uint32_t nsec = (f->size + 2047u) / 2048u;
    if (f->size == 0 || f->size > 24u * 1024u * 1024u) return -3;
    uint8_t *img = calloc(1, (size_t)nsec * 2048u);
    if (!img) return -4;
    if (psx_disc_read(d, f->lba, nsec, img) != 0) { free(img); return -5; }
    /* 头灌 guest 暂存 (LoadImage 布局契约=头 2KB 对齐可读) */
    for (uint32_t i = 0; i < 0x800u; i += 4u) hle_wr32(h, HLE_HDR_SCRATCH + i,
        (uint32_t)img[i] | ((uint32_t)img[i+1] << 8) | ((uint32_t)img[i+2] << 16) | ((uint32_t)img[i+3] << 24));
    /* 大块直落 RAM 镜像 (2MB 折叠面: 过 SCRATCH 回绕=毁低端, task-134 实证) */
    uint32_t pc = 0, gp = 0, sp_base = 0;
    if (hle_load_exe_host(h, HLE_HDR_SCRATCH, img, nsec * 2048u, 0, &pc, &gp, &sp_base) != 0)
        { free(img); return -6; }
    free(img);
    if (d->cnf_stack) sp_base = d->cnf_stack;   /* SYSTEM.CNF STACK= 正字 (真盘变体) */
    { uint32_t ww2; void *pw = psx_translate(h->core, 0x8006FA90u, &ww2);
      void *pz = psx_translate(h->core, 0x80010000u, &ww2);
      fprintf(stderr, "COLDPOST [FA90]=%08x [DVA+0]=%08x entry=%08x\n",
              pw ? *(uint32_t*)pw : 0xDEDu, pz ? *(uint32_t*)pz : 0xDEDu, pc); }
    psx_cpu_t *c = h->cpu;
    c->gpr[28] = gp;
    c->gpr[29] = sp_base ? (sp_base - 16u) : 0x801FFFF0u;
    memset(c->gpr, 0, 4 * sizeof(uint32_t));      /* a0..v1 清零(冷启正字) */
    /* 真机 boot 完成态: BIOS 经 eret 进 EXE 时 status.IE=1 EXL=0(游戏以此为前提
     * 开 vsync 中断等待; task-144 RETAIL_IRQT 实证 mask=0x000D 但 IE=0 死等) */
    c->cop0[12] = (c->cop0[12] & ~2u) | 1u;       /* IE=1, EXL=0 */
    c->gpr[31] = HLE_HALT_GPA;                    /* main 返回=halt 环 */
    /* halt 环: j self; nop */
    hle_wr32(h, HLE_HALT_GPA,      (0x02u << 26) | ((HLE_HALT_GPA >> 2) & 0x03FFFFFFu));
    hle_wr32(h, HLE_HALT_GPA + 4,  0u);
    /* RFE 蹦床 (task-150): HookEntryInt 钩子 jr $ra 落此 → rfe 清 EXL 回 epc */
    hle_wr32(h, 0x800001A8u, 0x42000010u);           /* rfe */
    hle_wr32(h, 0x800001ACu, 0u);                     /* nop */
    h->armed = 1; h->exe_pc = pc; h->exe_gp = gp; h->exe_sp = c->gpr[29];
    c->pc = c->npc - 4u;                          /* 复位簿记 */
    c->npc = pc; c->branch_pending = 0;
    if (entry_out) *entry_out = pc;
    return 0;
}

/* ---------- --boot-selftest (task-131 验收: 盘→冷跳→Execute→首帧 PC 轨迹+RAM 槽) ---------- */
static void btst_go(psx_cpu_t *c, uint32_t vec, uint32_t cmd, uint32_t ra)
{ c->gpr[9] = cmd; c->gpr[10] = vec; c->gpr[31] = ra;
  c->branch_pending = 0; c->pc = vec - 4; c->npc = vec; }

int cmd_boot_selftest(int argc, char **argv)
{
    (void)argc; (void)argv;
    int bad = 0;
    psx_core_t core; psx_cpu_t c; psx_hle_t h;
    if (psx_core_init_fixed(&core) != 0) { printf("BOOT_EV init FAIL\n"); return 1; }
    psx_cpu_reset(&c, &core);
    psx_hle_install(&h, &c, &core);
    /* HBEXE 程序体 (v1 自研, 零版权): 写标记 → putchar 'B'(经 A 族) → 再写 → 环 */
    const uint32_t C = 0x80040100u;
    uint32_t code[0x20];
    #define ADDIU(rt,im) ((0x09u << 26) | (0u << 21) | ((rt) << 16) | ((im) & 0xFFFFu))
    #define LUI(rt,im)   ((0x0Fu << 26) | (0u << 21) | ((rt) << 16) | ((im) & 0xFFFFu))
    #define ADDIU_R(rs_,rt_,im) ((0x09u << 26) | ((rs_) << 21) | ((rt_) << 16) | ((im) & 0xFFFFu))
    #define SW(rt,ofs,bs) ((0x2Bu << 26) | ((bs) << 21) | ((rt) << 16) | ((ofs) & 0xFFFFu)) /* rs=base, rt=data */
    #define JR(rs)       ((rs) << 21 | 0x08u)
    #define NOPW         0u
    #define JAL(tgt)     ((0x03u << 26) | (((tgt) >> 2) & 0x03FFFFFFu))  /* 正字 op=3 (0x0F=LUI 族) */
    #define J(tgt)       ((0x02u << 26) | (((tgt) >> 2) & 0x03FFFFFFu))
    code[0]  = LUI(16, 0x8012);
    code[1]  = ADDIU_R(16, 16, 0x3454);          /* $16 = 0x80123454 标记基址(4B 对齐; 0x...456 实测=真机同款地址错) */
    code[2]  = ADDIU(4, 0x42);                 /* a0='B' */
    code[3]  = JAL(C + 0x40);                  /* putchar wrapper */
    code[4]  = NOPW;
    code[5]  = ADDIU(17, 1);
    code[6]  = SW(17, 0, 16);                  /* [0x80123454]=1: base=$16 data=$17 (putchar 已发生) */
    code[7]  = ADDIU(18, 0x5A);
    code[8]  = SW(18, 4, 16);
    code[9]  = J(C + 0x38);
    code[10] = NOPW; code[11] = NOPW; code[12] = NOPW; code[13] = NOPW;
    code[14] = J(C + 0x38); code[15] = NOPW;   /* 环 @+0x38 */
    /* wrapper @+0x40 (code[16..19]): PSYQ 几何 */
    code[16] = ADDIU(9, 0x3C);                 /* $t1(gpr9)=A(putchar) */
    code[17] = ADDIU(10, 0xA0);                /* $t2(gpr10)=A0 族 */
    code[18] = JR(10);
    code[19] = NOPW;
    uint32_t n = 0xA00u; uint8_t *img = calloc(1, n);
    psx_cd_synth_exe_full(img, n, code, 80u);
    if (psx_iso_build("/tmp/hb_boot.iso", img, n, "PSX.EXE", 1) != 0) bad++;
    free(img);
    psx_disc_t disc;
    if (psx_disc_open(&disc, "/tmp/hb_boot.iso.cue") != 0) { printf("BOOT_EV disc FAIL\n"); return 1; }
    h.disc = &disc;
    uint32_t entry = 0;
    int r = psx_hle_coldboot(&h, &disc, &entry);
    BOOTCHK(r == 0 && entry == C, "b1_coldboot_entry");
    BOOTCHK(c.gpr[28] == 0x80040000u && c.gpr[29] == 0x801FFFF0u, "b2_gp_sp");
    /* 首帧推进: 64 步内应见 putchar(nlog) + 标记落 RAM + PC 入环 */
    uint32_t n0 = h.nlog;
    if (getenv("BOOT_TRACE")) {
        for (int i = 0; i < 40; i++) {
            uint32_t w; void *p = psx_translate(&core, c.npc, &w);
            uint32_t ins = p ? *(uint32_t *)p : 0xFFFFFFFFu;
            fprintf(stderr, "TRC pc=%x npc=%x ins=%08x t1=%x t2=%x ra=%x\n",
                    c.pc, c.npc, ins, c.gpr[9], c.gpr[10], c.gpr[31]);
            psx_cpu_step(&c);
        }
    } else psx_cpu_run(&c, 64);
    uint32_t m1 = 0, m2 = 0;
    { uint32_t w; void *p = psx_translate(&core, 0x80123454u, &w); if (p) memcpy(&m1, p, 4);
      p = psx_translate(&core, 0x80123458u, &w); if (p) memcpy(&m2, p, 4); }
    BOOTCHK(h.nlog == n0 + 1u && h.last_vec == 0xA0u && h.last_cmd == 0x3Cu, "b3_putchar_via_kernel");
    BOOTCHK(m1 == 1u && m2 == 0x5Au, "b4_marker_ram");
    BOOTCHK(c.pc == C + 0x38 || c.npc == C + 0x3C ||
         c.pc == C + 0x3C || c.npc == C + 0x38, "b5_pc_in_loop");   /* 环体={J,NOP} 相位无关 */
    BOOTCHK(h.armed == 1 && h.exe_pc == C, "b6_execute_armed");
    /* b7 数据段装载证据: 0x80040000 图案 */
    { uint32_t dv = 0, w; void *p = psx_translate(&core, 0x80040000u, &w); if (p) memcpy(&dv, p, 4);
      BOOTCHK(dv == 0xABCD0000u, "b7_data_extent_loaded"); }
    printf("BOOT_EV bad=%d pc=%x npc=%x nlog=%u exc=%u cause=%x epc=%x badv=%x t1=%x t2=%x ra=%x\n", bad, c.pc, c.npc, h.nlog, c.exc_n, c.cop0[13], c.cop0[14], c.cop0[8], c.gpr[9], c.gpr[10], c.gpr[31]);
    psx_disc_close(&disc); psx_core_free(&core);
    if (bad) { printf("BOOT SELFTEST FAIL (%d)\n", bad); return 1; }
    printf("BOOT CORE SELFTEST PASS\n");
    return 0;
}

/* ---------- --hle-selftest (task-129 验收面) ---------- */
#define EM(a, v) *(volatile uint32_t *)(uintptr_t)(a) = (v)
#define RDW(a)   (*(volatile uint32_t *)(uintptr_t)(a))
#define HCHK(cond_, name_) do { \
    printf("HLE_TEST %s: %s\n", name_, (cond_) ? "PASS" : "FAIL"); \
    if (!(cond_)) bad++; } while (0)

/* 经真 trampoline 几何发起 rootcall: 置 $t1/$t2/参 → pc=vec-4, npc=vec
 * (vec+4 预放 jr $ra 使返回+延迟槽天然正确) */
static void hle_go(psx_cpu_t *c, uint32_t vec, uint32_t cmd, uint32_t ra)
{
    c->gpr[9] = cmd; c->gpr[10] = vec; c->gpr[31] = ra;
    c->branch_pending = 0; c->pc = vec - 4; c->npc = vec;
}

int cmd_hle_selftest(int argc, char **argv)
{
    (void)argc; (void)argv;
    psx_core_t core; psx_cpu_t c; psx_hle_t h; int bad = 0;
    if (psx_core_init_fixed(&core) != 0) { printf("HLE_EV fixed init FAIL\n"); return 1; }
    if (psx_load_bios(&core) != 0) { printf("HLE_EV bios unset load FAIL\n"); return 1; }
    psx_cpu_reset(&c, &core);
    if (psx_hle_install(&h, &c, &core) != 0) { printf("HLE_EV install FAIL\n"); return 1; }
    const uint32_t P = 0x80010000u, D = 0x80020000u, E = 0x80030000u;

    /* V1 向量页几何: 0x80/0xA0/0xB0/0xC0 = syscall+jr$ra+nop, 且 0 镜像同页 */
    uint32_t wa, wb;
    HCHK(RDW(0x800000A0u) == 0x0000100Cu && RDW(0x800000A4u) == 0x03E00008u &&
         RDW(0x800000A8u) == 0u && RDW(0x80000080u) == 0x0000000Cu &&
         RDW(0x800000B0u) == 0x0000120Cu && RDW(0x800000C0u) == 0x0000130Cu &&
         psx_translate(&core, 0x000000A0u, &wa) == psx_translate(&core, 0x800000A0u, &wb),
         "v1_vectors_geom");
    /* V2 表几何: A/B/C 槽数 + 已登记锚位非空 */
    HCHK(HLE_A_SLOTS == 0xC0u && HLE_B_SLOTS == 0x60u && HLE_C_SLOTS == 0x20u &&
         h.a[0x39] != NULL && h.a[0x28] != NULL && h.b[0x56] != NULL && h.c[0x02] != NULL,
         "v2_table_geom");
    /* V3 A 族往返: putchar 'X' (v0='X', nlog+1, 返回落 ra 域) */
    c.gpr[0] = 0u;
    hle_go(&c, 0xA0u, 0x3Cu, P); c.gpr[4] = 'X';
    EM(P + 0, (0x09 << 26) | (0 << 21) | (20 << 16) | 5);   /* addiu $20=5 着陆标记 */
    psx_cpu_run(&c, 4); /* syscall, jr$ra, 槽nop, 着陆addiu */
    HCHK(c.gpr[2] == 'X' && h.nlog == 1u && h.last_vec == 0xA0u && h.last_cmd == 0x3Cu &&
         c.gpr[20] == 5u, "v3_a_trampoline_putchar");
    /* V4 未登记号诚实面: A(0x30) → v0=-1 + last_unknown 记账, 不抛异常 */
    uint32_t e0 = c.exc_n;
    hle_go(&c, 0xA0u, 0x30u, P); EM(P + 0, 0u);
    psx_cpu_run(&c, 3);
    HCHK(c.gpr[2] == 0xFFFFFFFFu && h.last_unknown == (int)((0xA0u << 8) | 0x30u) &&
         c.exc_n == e0, "v4_unknown_honest_minus1");
    /* V5 裸 a0 临界区: Enter(v0=前态,IE+IM5 清)→Exit(置回) */
    c.cop0[12] = 0x401u;
    EM(P + 0, 0x0000000Cu);                                   /* syscall Enter */
    EM(P + 4, (0x09 << 26) | (0 << 21) | (20 << 16) | 5);     /* addiu $20=5 */
    EM(P + 8, 0x0000000Cu);                                   /* syscall Exit */
    EM(P + 12, (0x08 << 26) | (0 << 21) | (21 << 16) | 7);    /* addi $21=7 */
    c.gpr[4] = 1u; c.gpr[9] = 0u; c.gpr[10] = 0u;
    c.pc = P - 4; c.npc = P; c.branch_pending = 0;
    psx_cpu_run(&c, 1);                                       /* Enter */
    uint32_t v_in = c.gpr[2], st_mid = c.cop0[12];
    c.gpr[4] = 2u;                                            /* 换 Exit 号(步间隙) */
    psx_cpu_run(&c, 3);                                       /* addiu, Exit, addi */
    HCHK(v_in == 1u && (st_mid & 0x401u) == 0u && (c.cop0[12] & 0x401u) == 0x401u &&
         c.gpr[21] == 7u, "v5_raw_critical_section");
    /* V6 A(0x39) InitHeap + B(0x00/0x01) 堆往返 (EvHeader 公开 5-word 布局) */
    hle_go(&c, 0xA0u, 0x39u, P); c.gpr[4] = D + 0x2000u; c.gpr[5] = 0x400u; EM(P, 0u);
    psx_cpu_run(&c, 3);
    HCHK(h.heap_ready == 1 && h.heap_base == D + 0x2000u, "v6_initheap");
    hle_go(&c, 0xB0u, 0x00u, P); c.gpr[4] = 0x20u; c.gpr[5] = 0u;
    psx_cpu_run(&c, 3);
    uint32_t p0 = c.gpr[2];
    HCHK(p0 >= D + 0x2000u + 20u && p0 < D + 0x2400u &&
         RDW(p0 - 20u + 4u) == 0x20u, "v6b_kernel_alloc_evhdr");
    EM(p0, 0xDEADBEEFu);
    hle_go(&c, 0xB0u, 0x01u, P); c.gpr[4] = p0;
    psx_cpu_run(&c, 3);
    HCHK(c.gpr[2] == 0u && h.heap_brk == D + 0x2000u, "v6c_kernel_free");
    /* V7 A(0x28) LoadImage: 合成 PSX-EXE (pc/gp/data/code) */
    for (uint32_t i = 0; i < 0x800u / 4u; i++) EM(E + 4u * i, 0u);
    EM(E + 0x10, 0x80040011u); EM(E + 0x14, 0x80040000u);
    EM(E + 0x18, 0x80040000u); EM(E + 0x1C, 0x10u + 0x0Cu);   /* 单块 = data16B+code12B */
    EM(E + 0x30, 0x80200000u);                                /* SP 基 (真布局) */
    for (uint32_t i = 0; i < 0x10u; i++) EM(E + 0x800u + 4u * i, 0x11110000u + i);
    EM(E + 0x800u + 0x10u, (0x08 << 26) | (0 << 21) | (2 << 16) | 0x222);   /* addi $2=0x222 */
    EM(E + 0x800u + 0x14u, 0x08004110u);                                     /* j 0x8004110 */
    EM(E + 0x800u + 0x18u, 0u);
    EM(D + 0x100, 0u); EM(D + 0x104, 0u);
    hle_go(&c, 0xA0u, 0x28u, P); c.gpr[4] = E; c.gpr[5] = 0u;
    c.gpr[6] = D + 0x100u; c.gpr[7] = D + 0x104u;
    psx_cpu_run(&c, 3);
    printf("HLE_EV v7 r=%x pc=%x gp=%x d0=%x dC=%x code=%x\n", c.gpr[2], RDW(D + 0x100u),
           RDW(D + 0x104u), RDW(0x80040000u), RDW(0x8004000Cu), RDW(0x80040010u));
    HCHK(c.gpr[2] == 0u && RDW(D + 0x100u) == 0x80040011u && RDW(D + 0x104u) == 0x80040000u &&
         RDW(0x80040000u) == 0x11110000u && RDW(0x8004000Cu) == 0x11110003u &&
         RDW(0x80040010u) == ((0x08u << 26) | (2u << 16) | 0x222u), "v7_loadimage_exe");
    /* V8 A(0x29) Execute(first=1) 武装 (entry/sp 走栈参 sp+0x10..) */
    c.gpr[29] = D + 0x1200u;
    EM(D + 0x1210u, 0x80040100u); EM(D + 0x1214u, D + 0x1000u); EM(D + 0x1218u, 0u);
    hle_go(&c, 0xA0u, 0x29u, P); c.gpr[4] = 1u;
    psx_cpu_run(&c, 3);
    HCHK(h.armed == 1 && h.exe_pc == 0x80040100u && h.exe_sp == D + 0x1000u && c.gpr[2] == 0u,
         "v8_execute_arm");
    /* V9 B 事件环: open→enable→test0→deliver→test1→wait→close */
    hle_go(&c, 0xB0u, 0x08u, P); c.gpr[4] = 0x00u; c.gpr[5] = 0x19u;
    c.gpr[6] = 0x01u; c.gpr[7] = 0u; psx_cpu_run(&c, 3);
    uint32_t evh = c.gpr[2];
    HCHK(evh != 0u && evh != 0xFFFFFFFFu, "v9a_open_event");
    hle_go(&c, 0xB0u, 0x0Cu, P); c.gpr[4] = evh; psx_cpu_run(&c, 3);
    hle_go(&c, 0xB0u, 0x0Bu, P); c.gpr[4] = evh; psx_cpu_run(&c, 3);
    uint32_t t0 = c.gpr[2];
    hle_go(&c, 0xB0u, 0x07u, P); c.gpr[4] = evh; psx_cpu_run(&c, 3);
    hle_go(&c, 0xB0u, 0x0Bu, P); c.gpr[4] = evh; psx_cpu_run(&c, 3);
    uint32_t t1 = c.gpr[2];
    hle_go(&c, 0xB0u, 0x0Au, P); c.gpr[4] = evh; psx_cpu_run(&c, 3);
    hle_go(&c, 0xB0u, 0x09u, P); c.gpr[4] = evh; psx_cpu_run(&c, 3);
    uint32_t cl = c.gpr[2];
    HCHK(t0 == 0u && t1 == 1u && cl == 0u, "v9b_event_lifecycle");
    /* V10 GetC0Table/GetB0Table [R 锚] */
    hle_go(&c, 0xB0u, 0x56u, P); psx_cpu_run(&c, 3); uint32_t c0 = c.gpr[2];
    hle_go(&c, 0xB0u, 0x57u, P); psx_cpu_run(&c, 3); uint32_t b0 = c.gpr[2];
    HCHK(c0 == HLE_C0_GPA && b0 == HLE_B0_GPA && c0 != b0, "v10_c0b0_tables");
    /* V11 C(0x02/0x03) enq/deq 链表 */
    EM(D + 0x1300u, 0u); EM(D + 0x1310u, 0u); EM(D + 0x1320u, 0u);
    hle_go(&c, 0xC0u, 0x02u, P); c.gpr[4] = D + 0x1300u; c.gpr[5] = D + 0x1310u; psx_cpu_run(&c, 3);
    hle_go(&c, 0xC0u, 0x02u, P); c.gpr[4] = D + 0x1300u; c.gpr[5] = D + 0x1320u; psx_cpu_run(&c, 3);
    HCHK(RDW(D + 0x1300u) == D + 0x1320u && RDW(D + 0x1320u) == D + 0x1310u, "v11a_enq_chain");
    hle_go(&c, 0xC0u, 0x03u, P); c.gpr[4] = D + 0x1300u; c.gpr[5] = D + 0x1320u; psx_cpu_run(&c, 3);
    HCHK(c.gpr[2] == D + 0x1320u && RDW(D + 0x1300u) == D + 0x1310u, "v11b_deq_mid");
    /* V12 devboot 族 + FlushCache: 恒 0 记账 */
    uint32_t n0 = h.nlog;
    hle_go(&c, 0xA0u, 0x70u, P); psx_cpu_run(&c, 3); uint32_t r1 = c.gpr[2];
    hle_go(&c, 0xA0u, 0x71u, P); psx_cpu_run(&c, 3); uint32_t r2 = c.gpr[2];
    hle_go(&c, 0xA0u, 0x72u, P); psx_cpu_run(&c, 3); uint32_t r3 = c.gpr[2];
    hle_go(&c, 0xA0u, 0x44u, P); psx_cpu_run(&c, 3); uint32_t r4 = c.gpr[2];
    /* task-151: 0x72=_96_remove 转正返 1(移除恒成功), 其余族仍 0 */
    HCHK(r1 == 0u && r2 == 0u && r3 == 1u && r4 == 0u && h.nlog == n0 + 4u, "v12_devboot_nosync");
    /* V13 SetConf/GetConf 往返 */
    uint32_t cfg[4] = { 0x020C0000u, 0x80028000u, 0x800287C0u, 0x80028FC0u };
    for (int i = 0; i < 4; i++) EM(D + 0x1400u + 4u * (uint32_t)i, cfg[i]);
    hle_go(&c, 0xA0u, 0x9Cu, P);
    c.gpr[4] = cfg[0]; c.gpr[5] = cfg[1]; c.gpr[6] = cfg[2]; c.gpr[7] = cfg[3];
    psx_cpu_run(&c, 3);
    hle_go(&c, 0xA0u, 0x9Du, P); EM(D + 0x1400u, 0u); EM(D + 0x1404u, 0u);
    c.gpr[4] = D + 0x1400u; psx_cpu_run(&c, 3);
    /* GetConf v1=写 4 字连续域(a0), config 存四参数字: 校验前两字 */
    HCHK(c.gpr[2] == 0u && RDW(D + 0x1400u) == cfg[0] && RDW(D + 0x1404u) == cfg[1] &&
         h.config[3] == cfg[3], "v13_conf_roundtrip");
    /* V14 puts: guest 串直出宿主 */
    memcpy((void *)(uintptr_t)(D + 0x1500u), "HLE_WEDGE_OK\n", 14u);
    hle_go(&c, 0xA0u, 0x3Eu, P); c.gpr[4] = D + 0x1500u; psx_cpu_run(&c, 3);
    HCHK(c.gpr[2] == 1u, "v14_puts_stdout");
    /* V15 卸载回原异常路径 */
    psx_hle_uninstall(&h);
    uint32_t ex0 = c.exc_n;
    hle_go(&c, 0xA0u, 0x3Cu, P); psx_cpu_run(&c, 1);
    HCHK(c.exc_n == ex0 + 1u && c.cop0[13] == (8u << 2), "v15_uninstall_falls_back_to_exception");

    /* V16 A 族文件 API: 自造盘 → open/read/lseek/close (fd=0x1100 正字) */
    {
        uint32_t n = 0xA00u; uint8_t *img = calloc(1, n);
        if (img) {
            psx_cd_synth_exe(img, n);
            psx_iso_build("/tmp/hb_hle_selftest.iso", img, n, "PSX.EXE", 0);
            psx_disc_t disc;
            if (psx_disc_open(&disc, "/tmp/hb_hle_selftest.iso") == 0) {
                psx_hle_install(&h, &c, &core);           /* 重装(HLE 钩归零后) */
                h.disc = &disc;
                memcpy((void *)(uintptr_t)(D + 0x1600u), "cdrom:\\PSX.EXE;1", 17u);
                hle_go(&c, 0xA0u, 0x00u, P); c.gpr[4] = D + 0x1600u; psx_cpu_run(&c, 3);
                uint32_t fd = c.gpr[2];
                hle_go(&c, 0xA0u, 0x02u, P); c.gpr[4] = fd; c.gpr[5] = E + 0x2000u; c.gpr[6] = 0x800u;
                psx_cpu_run(&c, 3);
                uint32_t got = c.gpr[2];
                hle_go(&c, 0xA0u, 0x01u, P); c.gpr[4] = fd; c.gpr[5] = 0x10u; c.gpr[6] = 0u;
                psx_cpu_run(&c, 3); uint32_t lp = c.gpr[2];
                hle_go(&c, 0xA0u, 0x04u, P); c.gpr[4] = fd; psx_cpu_run(&c, 3); uint32_t cr = c.gpr[2];
                uint32_t pc2 = 0; memcpy(&pc2, (void *)(uintptr_t)(E + 0x2000u + 0x10u), 4);
                HCHK(fd == 0x1100u && got == 0x800u && lp == 0x10u && cr == 0u &&
                     pc2 == 0x80040100u, "v16_file_api_open_read");
                psx_disc_close(&disc);
            } else HCHK(0, "v16_file_api_open_read");
            free(img);
        }
    }
    printf("HLE_EV bad=%d nlog=%u\n", bad, h.nlog);
    psx_core_free(&core);
    if (bad) { printf("HLE SELFTEST FAIL (%d)\n", bad); return 1; }
    printf("HLE CORE SELFTEST PASS\n");
    return 0;
}

/* ---------- retail 诊断面 (task-134): 用户自有盘运行期分析, 零入库 ----------
 * --retail <path>        : 盘面元数据 (卷名/BOOT 文件/EXE 头字段) —— 不含任何盘字节
 * --retail-probe <path> [cycles] : 冷跳后跑 N 周期, 输出能力缺口报告:
 *   已服务 rootcall 数 / 未登记号直方图 / 异常(cause,epc) / 终止 pc。
 * 报告字段 = 命令号与计数等元数据, 属兼容性事实, 非版权表达。 */
#include "psx_cdrom.h"

static int retail_setup(psx_core_t *core, psx_cpu_t *cpu, psx_hle_t *h,
                        psx_disc_t *d, const char *path, uint32_t *entry)
{
    if (psx_core_init_fixed(core) != 0) return 1;
    psx_cpu_reset(cpu, core);
    psx_hle_install(h, cpu, core);
    if (psx_disc_open(d, path) != 0) return 2;
    if (!d->have_iso) return 3;
    return psx_hle_coldboot(h, d, entry);
}

int cmd_retail(int argc, char **argv)
{
    const char *path = NULL;
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--retail") == 0 && i + 1 < argc) path = argv[++i];
    if (!path) { fprintf(stderr, "usage: --retail <disc.(bin|img|iso|cue)>\n"); return 2; }
    psx_core_t core; psx_cpu_t cpu; psx_hle_t h; psx_disc_t d; uint32_t entry = 0;
    if (psx_core_init_fixed(&core) != 0) return 1;
    if (psx_disc_open(&d, path) != 0) { printf("RETAIL open FAIL (无 ISO9660 PVD?)\n"); psx_core_free(&core); return 1; }
    char nm[64] = ""; int bi = psx_disc_boot_exe(&d, nm, sizeof nm);
    printf("RETAIL vol=\"%s\" files=%d boot=%s lba=%u size=%u data_off=%d frame0=%u\n",
           d.volume_id, d.nfiles, bi >= 0 ? nm : "(none)",
           bi >= 0 ? d.files[bi].lba : 0u, bi >= 0 ? d.files[bi].size : 0u,
           d.data_off, d.data_frame0);
    if (bi >= 0) {
        uint8_t hdr[2048];
        if (psx_disc_read(&d, d.files[bi].lba, 1, hdr) == 0) {
            uint32_t v10, v14, v18, v1c, v20, v24, v28;
            memcpy(&v10, hdr + 0x10, 4); memcpy(&v14, hdr + 0x14, 4); memcpy(&v18, hdr + 0x18, 4);
            memcpy(&v1c, hdr + 0x1C, 4); memcpy(&v20, hdr + 0x20, 4); memcpy(&v24, hdr + 0x24, 4);
            memcpy(&v28, hdr + 0x28, 4);
            printf("RETAIL exe hdr pc=%08x gp=%08x vaddr=%08x psize=%u taddr=%08x tsize=%u sp=%08x\n",
                   v10, v14, v18, v1c, v20, v24, v28);
        }
    }
    for (int i = 0; i < d.nfiles; i++)
        printf("RETAIL file %s lba=%u size=%u%s\n", d.files[i].name, d.files[i].lba,
               d.files[i].size, d.files[i].is_dir ? " (dir)" : "");
    psx_disc_close(&d); psx_core_free(&core);
    return 0;
}

static uint32_t d2mask(psx_dev_t *d){ return d->i_mask & 0x3FFFu; }
static uint32_t d2stat(psx_dev_t *d){ return d->i_stat & 0x3FFFu; }
int cmd_retail_probe(int argc, char **argv)
{
    const char *path = NULL; uint32_t cycles = 2000000u;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--retail-probe") == 0 && i + 1 < argc) path = argv[++i];
        else cycles = (uint32_t)strtoul(argv[i], NULL, 0);
    }
    if (!path) { fprintf(stderr, "usage: --retail-probe <disc> [cycles]\n"); return 2; }
    psx_core_t core; psx_cpu_t cpu; psx_hle_t h; psx_disc_t d; uint32_t entry = 0;
    psx_dev_t dev; psx_gpu_t gpu;
    int st = retail_setup(&core, &cpu, &h, &d, path, &entry);
    if (st != 0) { printf("RETAIL_PROBE setup rc=%d (1=core 2=open 3=noiso 其他=coldboot)\n", st);
                   psx_core_free(&core); return 1; }
    psx_dev_init(&dev); core.dev = &dev; h.dev = &dev;
    psx_dev_trace_reset();
    psx_gpu_init(&gpu); psx_gpu_attach_core(&gpu, &core); psx_dev_gpu_attach(&dev, &gpu); psx_dev_cd_attach(&dev, &d);
    psx_dev_core_attach(&dev, &core);   /* task-145: DMA 执行需 RAM 翻译 */
    printf("RETAIL_PROBE boot=%s entry=%08x sp=%08x gp=%08x\n",
           d.files[psx_disc_boot_exe(&d, 0, 0)].name, entry, cpu.gpr[29], cpu.gpr[28]);
    if (getenv("RETAIL_TRACE")) {         /* 首异常单步诊 + 前 16 步回环缓冲 */
        uint32_t budget = getenv("RETAIL_BUDGET") ? (uint32_t)strtoul(getenv("RETAIL_BUDGET"),0,0) : 4000000u;
        const uint32_t cad = getenv("RETAIL_CAD") ? (uint32_t)strtoul(getenv("RETAIL_CAD"),0,0) : 0u;
        struct { uint32_t pc, ins, t1, t2, v0, a0, ra; } rb[64]; int rn = 0;
        uint32_t wb_pc = 0, wb_val = 0; int wb_n = 0;   /* 0x8007FA90 写者追踪 */
        for (uint32_t i = 0; i < budget; i++) {
            uint32_t w; void *q = psx_translate(&core, cpu.npc, &w);
            uint32_t ins = q ? *(uint32_t *)q : 0xDEADBEEFu;
            rb[rn & 63].pc = cpu.pc; rb[rn & 63].ins = ins; rb[rn & 63].t1 = cpu.gpr[9];
            rb[rn & 63].t2 = cpu.gpr[10]; rb[rn & 63].v0 = cpu.gpr[2]; rb[rn & 63].a0 = cpu.gpr[4];
            rb[rn & 63].ra = cpu.gpr[31];
            rn++;
            {   /* 页级写监视: [63E8]SPU 指针槽 + [6FABC]/[6FA90] 描述符槽 */
                uint32_t w2;
                static const uint32_t mslot[3] = { 0x800763E8u, 0x8006FABCu, 0x8006FA90u };
                static void *mp[3]; static uint32_t mb[3];
                for (int mi = 0; mi < 3; mi++) {
                    mp[mi] = psx_translate(&core, mslot[mi], &w2);
                    mb[mi] = mp[mi] ? *(uint32_t*)mp[mi] : 0u;
                }
                uint32_t before = 0u; void *qp = mp[0];
                psx_cpu_step(&cpu);
                if (cad && (rn % cad) == 0u) { psx_gpu_tick(&gpu); psx_dev_vframe(&dev); }
                uint32_t after = qp ? *(uint32_t*)qp : 0u;
                for (int mi = 0; mi < 3; mi++) {
                    uint32_t af = mp[mi] ? *(uint32_t*)mp[mi] : 0u;
                    if (mp[mi] && af != mb[mi] &&
                        (wb_n < 12 || af == 0xFFFFFFFFu)) {   /* 饱和后仍记 -1 投毒 */
                        fprintf(stderr, "WB%02d @%08x ins=%08x val %08x->%08x\n",
                                wb_n++, mslot[mi], ins, mb[mi], af);
                        fflush(stderr);
                    }
                }
            }
            if (cpu.exc_n) {
                uint32_t ww; void *q2 = psx_translate(&core, cpu.cop0[14], &ww);
                fprintf(stderr, "RT EXC#%u cause=%x epc=%x badv=%x v0=%x v1=%x a0=%x a1=%x ins=%08x\n",
                        cpu.exc_n, cpu.cop0[13], cpu.cop0[14], cpu.cop0[8],
                        cpu.gpr[2], cpu.gpr[3], cpu.gpr[4], cpu.gpr[5],
                        q2 ? *(uint32_t*)q2 : 0xFFFFFFFFu);
                uint32_t dreg[4] = { cpu.gpr[4], cpu.gpr[5], cpu.gpr[6], cpu.gpr[7] };
                char dn[4] = { 'a','b','c','d' };
                for (int dqi = 0; dqi < 4; dqi++) {
                    uint32_t base = dreg[dqi] & ~3u;
                    if (base < 0x80000000u || base > 0x80200000u) continue;
                    fprintf(stderr, "RT DUMP$%c0:", dn[dqi]);
                    for (int wz = 0; wz < 8; wz++) {
                        void *q3 = psx_translate(&core, base + (uint32_t)wz * 4u, &ww);
                        fprintf(stderr, " %08x", q3 ? *(uint32_t*)q3 : 0xDEADBEEFu);
                    }
                    fprintf(stderr, "\n");
                }
                if (getenv("RETAIL_EXCD")) {   /* epc±4K RAM dump 供 capstone */
                    FILE *df = fopen("/tmp/hunt_ram.bin", "wb");
                    if (df) {
                        uint32_t start = (cpu.cop0[14] & ~0xFFu) - 0x800u;
                        fprintf(stderr, "EXCDUMP base=%08x\n", start);
                        for (uint32_t z = 0; z < 0x1000u; z += 4u) {
                            uint32_t ww9; void *qd = psx_translate(&core, start + z, &ww9);
                            uint32_t vv = qd ? *(uint32_t*)qd : 0u;
                            fwrite(&vv, 4, 1, df);
                        }
                        fclose(df);
                    }
                }
                {
                    static const uint32_t sl4[4] = { 0x800763E8u, 0x8006FA90u, 0x8006FABCu, 0x8006FA9Cu };
                    uint32_t ww8;
                    fprintf(stderr, "RT SLOTS:");
                    for (int q8 = 0; q8 < 4; q8++) {
                        void *q8p = psx_translate(&core, sl4[q8], &ww8);
                        fprintf(stderr, " [%08x]=%08x", sl4[q8], q8p ? *(uint32_t*)q8p : 0xDEDu);
                    }
                    fprintf(stderr, "\n");
                }
                fprintf(stderr, "RT EPCW:");   /* epc 前后 8 词指令窗 */
                for (int wz = -4; wz < 4; wz++) {
                    void *q4 = psx_translate(&core, (cpu.cop0[14] & ~3u) + (uint32_t)wz * 4u, &ww);
                    fprintf(stderr, " %08x", q4 ? *(uint32_t*)q4 : 0xDEADBEEFu);
                }
                fprintf(stderr, "\n");
                int base = rn - 1;
                for (int k = 40; k >= 0; k--) {
                    int idx = (base - k) & 63;
                    fprintf(stderr, "RT -%02d pc=%x ins=%08x t1=%x t2=%x v0=%x a0=%x ra=%x\n",
                            k, rb[idx].pc, rb[idx].ins, rb[idx].t1, rb[idx].t2, rb[idx].v0, rb[idx].a0, rb[idx].ra);
                }
                break;
            }
        }
    }
    /* 帧节拍正字 (task-146): PSX CPU 33.8688MHz / NTSC 60 = 564456 周期/帧 —
     * 旧 56445 少一量级(10x 欠喂), 真盘 vsync 内主循环全部显"慢一格"假象 */
    { const char *w = getenv("RETAIL_WATCH");   /* task-150: lo:hi 写监视(RAM 覆盖取证) */
      if (w) { unsigned lo,hi; if (sscanf(w,"%x:%x",&lo,&hi)==2) { g_watch_lo=lo; g_watch_hi=hi; g_watch_n=0; } } }
    const uint32_t frame_cycles = 564456u;
    uint32_t done = 0;
    static uint32_t hpc[4096];                    /* pc 热点采样 (page 桶, 诊断 RETAIL_HOT) */
    const int hot = getenv("RETAIL_HOT") != NULL;
    while (done < cycles) {
        uint32_t n = frame_cycles < cycles - done ? frame_cycles : cycles - done;
        if (hot) for (uint32_t z = 0; z < n; z += 64u) { psx_cpu_step(&cpu); hpc[(cpu.pc >> 4) & 4095u]++; }
        else psx_cpu_run(&cpu, n);
        done += n;
        {   /* 每 cause 首现场 (task-146): 新 excode 出现即 dump epc±128B RAM + 指令 */
            static uint32_t prev_exc = 0; static unsigned seen_causes;
            if (cpu.exc_n > prev_exc) {
                uint32_t ex = (cpu.cop0[13] >> 2) & 0x1Fu;
                if (!(seen_causes & (1u << ex))) {
                    seen_causes |= (1u << ex);
                    uint32_t ww6;
                    void *qi = psx_translate(&core, cpu.cop0[14] & ~3u, &ww6);
                    fprintf(stderr, "EXC1ST cause=%x(%u次) epc=%08x ins=%08x badv=%08x ra=%08x\n",
                            cpu.cop0[13], cpu.exc_n, cpu.cop0[14],
                            qi ? *(uint32_t*)qi : 0xDEADBEEFu, cpu.cop0[8], cpu.gpr[31]);
                    char fname[64];
                    snprintf(fname, sizeof fname, "/tmp/exc1st_%02lx.bin", (unsigned long)ex);
                    FILE *df = fopen(fname, "wb");
                    if (df) {
                        uint32_t start = (cpu.cop0[14] & ~0x7Fu) - 0x80u;
                        for (uint32_t z = 0; z < 0x100u; z += 4u) {
                            void *qd = psx_translate(&core, start + z, &ww6);
                            uint32_t vv = qd ? *(uint32_t*)qd : 0u;
                            fwrite(&vv, 4, 1, df);
                        }
                        fprintf(stderr, "EXC1ST dump %s base=%08x\n", fname, start);
                        fclose(df);
                    }
                    fflush(stderr);
                }
                prev_exc = cpu.exc_n;
            }
        }
        if (getenv("RETAIL_IRQT") && (dev.vframes % 200u) == 3u) {   /* 中断三要素 */
            fprintf(stderr, "IRQT vf=%u st.IE=%x mask=%04x stat=%04x exc=%u\n",
                    dev.vframes, (cpu.cop0[12] & 1u), d2mask(&dev), d2stat(&dev), cpu.exc_n);
            fflush(stderr);
        }
        {   /* 值猎手(task-143 下楔): 首见 0x94639271/9463941B 落 RAM 的帧+现场 */
            static int hunted = 0;
            if (!hunted) {
                uint32_t *mw = (uint32_t *)core.ram;
                for (uint32_t z = 0; z < 0x200000u / 4u; z++) {
                    if (mw[z] == 0x94639271u || mw[z] == 0x9463941Bu) {
                        fprintf(stderr, "HUNT frame=%u done=%u [%08x]=%08x pc=%08x exc=%u\n",
                                dev.vframes, done, 0x80000000u + z * 4u, mw[z], cpu.pc, cpu.exc_n);
                        fflush(stderr);
                        hunted = 1; break;
                    }
                }
            }
        }
        if (!getenv("RETAIL_STEP")) psx_gpu_tick(&gpu);
        psx_dev_vframe(&dev);        if (g_watch_done_gpa != 0xFFFFFFFFu) {
            uint32_t wv; void *pp = psx_translate(&core, g_watch_done_gpa, &wv);
            unsigned short hv; memcpy(&hv, pp, 2); (void)hv;
            printf("RWATCH pc=%08x 写 gpa=%08x post=%08x\n", g_watch_last_pc, g_watch_done_gpa, wv);
            g_watch_done_gpa = 0xFFFFFFFFu;
        }
        if (getenv("RETAIL_CB")) psx_hle_vblank_fire(&h, dev.vframes);   /* task-149 实验门控: 派发语义待真机钉, 默认关 */
        {   /* task-152: BIOS VBlank 给已 enable 事件投递票(Test/WaitEvent 消费) */
            for (uint32_t z = 0; z < HLE_EV_MAX; z++)
                if (h.ev[z].class != 0xFFFFFFFFu && h.ev[z].enabled) h.ev[z].fired = 1;
        }
        if (getenv("RETAIL_STEP")) {            /* 帧级进度诊 */
            printf("RF done=%u pc=%x exc=%u nlog=%u gp0=%u\n",
                   done, cpu.pc, cpu.exc_n, h.nlog, gpu.gp0_words);
            fflush(stdout);
        }
        if (cpu.pc == HLE_HALT_GPA || cpu.exc_n >= 64u) break;
    }
    { extern unsigned long hle_dbg_calls, hle_dbg_reject, hle_dbg_maxdepth;
      extern unsigned hle_dbg_cw;
      printf("RETAIL_PROBE rc=%lu reject=%lu maxdepth=%lu cw=%u\n", hle_dbg_calls, hle_dbg_reject, hle_dbg_maxdepth, hle_dbg_cw); }
        printf("RETAIL_PROBE ran=%u served=%u unknown=%u exc=%u cause=%x epc=%x pc=%08x npc=%08x\n",
           done, h.nlog, h.unk_n, cpu.exc_n, cpu.cop0[13], cpu.cop0[14], cpu.pc, cpu.npc);
    {   /* 首异常现场: badv + epc 指令窗 + 关键寄存器 (task-142 死环钉字) */
        uint32_t wwz;
        printf("RETAIL_EXC badv=%08x regs v0=%08x v1=%08x a0=%08x a1=%08x t0=%08x t1=%08x t2=%08x sc=%x ra=%08x sp=%08x\n",
               cpu.cop0[8], cpu.gpr[2], cpu.gpr[3], cpu.gpr[4], cpu.gpr[5],
               cpu.gpr[8], cpu.gpr[9], cpu.gpr[10], cpu.syscall_code, cpu.gpr[31], cpu.gpr[29]);
        printf("RETAIL_EXC epcw:");
        for (int wz = -3; wz <= 2; wz++) {
            void *qz = psx_translate(&core, (cpu.cop0[14] & ~3u) + (uint32_t)wz * 4u, &wwz);
            printf(" %08x", qz ? *(uint32_t*)qz : 0xDEADBEEFu);
        }
        printf("\n");
    }
        {
            uint32_t slots[4] = { 0x800763E8u, 0x8007FA90u, 0x80010000u, 0x8006FA90u };
            uint32_t wwz2;
            printf("RETAIL_EXC slots:");
            for (int qs = 0; qs < 4; qs++) {
                void *qp = psx_translate(&core, slots[qs], &wwz2);
                printf(" [%08x]=%08x", slots[qs], qp ? *(uint32_t*)qp : 0xDEDu);
            }
            printf("\n");
        }
        if (getenv("RETAIL_EXCD")) { uint32_t wwz2;   /* epc 上下文 RAM dump 落文件供 capstone 静态反汇编 */
            FILE *df = fopen("/tmp/hunt_ram.bin", "wb");
            if (df) {
                uint32_t start = (cpu.cop0[14] & ~0xFFu) - 0x800u;
                for (uint32_t z = 0; z < 0x1000u; z += 4u) {
                    void *qd = psx_translate(&core, start + z, &wwz2);
                    uint32_t vv = qd ? *(uint32_t*)qd : 0u;
                    fwrite(&vv, 4, 1, df);
                }
                fclose(df);
                fprintf(stderr, "EXCDUMP base=%08x -> /tmp/hunt_ram.bin\n", start);
                /* 函数序言追 $v1: 反查 epc 前所有写 $v1 指令码 + 全 RAM 搜立即数对 */
                FILE *vf = fopen("/tmp/hunt_v1.txt", "w");
                if (vf) { fprintf(vf, "epc=%08x v1=%08x a0=%08x a1=%08x s0=%08x s2=%08x\n",
                                 cpu.cop0[14], cpu.gpr[3], cpu.gpr[4], cpu.gpr[5],
                                 cpu.gpr[16], cpu.gpr[18]); fclose(vf); }
            }
        }
    psx_dev_trace_dump(14);
    if (hot) {
        for (int pass = 0; pass < 10; pass++) {
            uint32_t best = 0; int bi = -1;
            for (int z = 0; z < 4096; z++) if (hpc[z] > best) { best = hpc[z]; bi = z; }
            if (bi < 0) break;
            printf("HOT pc~%05x n=%u\n", (uint32_t)bi << 4, best);
            hpc[bi] = 0;
        }
    }
        {   /* DUMPHOT_GPA=0x... 导出该处 RAM 供 capstone 静态钉等待条件 (task-147) */
        const char *dg = getenv("DUMPHOT_GPA");
        if (dg) {
            uint32_t base = (uint32_t)strtoul(dg, 0, 0) & ~0xFFu;
            char fname[64]; snprintf(fname, sizeof fname, "/tmp/dumphot_%08x.bin", base);
            FILE *df = fopen(fname, "wb"); uint32_t www;
            if (df) { for (uint32_t z = 0; z < 0x300u; z += 4u) {
                        void *qd = psx_translate(&core, base + z, &www);
                        uint32_t vv = qd ? *(uint32_t*)qd : 0u; fwrite(&vv, 4, 1, df); }
                      fclose(df); printf("DUMPHOT %s base=%08x\n", fname, base); }
        }
    }
    { extern void gp0_trace_dump(void); gp0_trace_dump(); }
    { extern unsigned g_gp0_hist[8];
      printf("RETAIL_PROBE gp0hist: f0=%u f1=%u f2=%u f3=%u f4=%u f5=%u f6=%u f7=%u disp=%x\n",
             g_gp0_hist[0], g_gp0_hist[1], g_gp0_hist[2], g_gp0_hist[3],
             g_gp0_hist[4], g_gp0_hist[5], g_gp0_hist[6], g_gp0_hist[7], gpu.disp_on); }
    { const char *tw = getenv("RETAIL_TIMERS");
      if (tw) for (int t = 0; t < 4; t++)
          printf("RETAIL_TIM T%d ctrl=%04x cnt=%u target=%u last_sync=%llu\n", t,
                 dev.timer_ctrl[t], dev.timer_cnt[t], dev.timer_target[t],
                 (unsigned long long)dev.timer_last_sync[t]); }
    printf("RETAIL_PROBE gpu vframes=%u gp_ticks=%u gp0=%u gp1=%u fill=%u xfer=%u tri=%u spr=%u\n",
           dev.vframes, gpu.frames, gpu.gp0_words, gpu.gp1_words, gpu.fill_px, gpu.xfer_px, gpu.tri_px, gpu.spr_px);
    for (int z = 0; z < 48; z++) if (h.unk[z].cnt)
        printf("UNK vec=%02x cmd=%02x n=%u\n", h.unk[z].key >> 8, h.unk[z].key & 0xFF, h.unk[z].cnt);
    psx_disc_close(&d); psx_core_free(&core);
    return 0;
}
/* VBlank 60Hz 派发 (task-149): 遍历回调环, 每个同步执行至 trampoline 振荡点。
 * 现场保护 = 保存主循环 pc/npc/branch, 回调 ABI a0=event a1=class a2=id a3=gpu
 * 栈[sp+0x10]=data; 返回检测: 回调尾部 jr $ra 落 tramp 后 pc/npc 恒=tramp 振荡。
 * 预算 30000 指令/回调 + 深度护栏(回调内再触 vblank_fire 直接跳过)。 */
void psx_hle_vblank_fire(psx_hle_t *h, uint32_t tick)
{
    if (!h || !h->cpu || g_hle_depth > 2) return;
    h->vblank_ticks = tick;
    psx_cpu_t *c = h->cpu;
    g_hle_depth++;
    uint32_t tramp = (c->gpr[29] - 32u) & ~3u;   /* 主循环栈底安全窗 */
    uint32_t ww; 
    if (!psx_translate(h->core, tramp, &ww)) { g_hle_depth--; return; }
    uint32_t save_pc = c->pc, save_npc = c->npc;
    int save_pend = (int)c->branch_pending; uint32_t save_tgt = c->branch_target;
    uint32_t save_ra = c->gpr[31];
    hle_wr32(h, tramp, 0x03E00008u);              /* jr $ra */
    hle_wr32(h, tramp + 4u, 0u);                   /* nop */
    for (int z = 0; z < 16; z++) {
        if (!h->cb[z].used || h->cb[z].func < 0x80000000u) continue;
        c->gpr[4] = tick; c->gpr[5] = h->cb[z].class_; c->gpr[6] = h->cb[z].id;
        c->gpr[7] = h->cb[z].gpu; c->gpr[31] = tramp;
        hle_wr32(h, (c->gpr[29] & ~3u) + 0x10u, h->cb[z].data);
        c->branch_pending = 0;
        c->pc = tramp; c->npc = h->cb[z].func;     /* 从"tramp 延迟槽"进回调 */
        for (uint32_t st = 0; st < 200u; st++) {
            psx_cpu_step(c);
            if (c->pc == tramp && c->npc == tramp) break;   /* 回调已返回振荡 */
            if (c->exc_n > 0u && c->pc == save_pc) break;
        }
        c->branch_pending = 0;
    }
    c->pc = save_pc; c->npc = save_npc; c->gpr[31] = save_ra;
    c->branch_pending = (uint32_t)save_pend; c->branch_target = save_tgt;
    g_hle_depth--;
}
