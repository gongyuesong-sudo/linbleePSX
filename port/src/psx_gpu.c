/*
 * psx_gpu.c —— GPU v1: psx-spx 家族解码 + 软光栅 + Vulkan compute 硬件扫描输出 (task-132)
 *
 * 命令模型 (psx-spx: family = word>>29, 与本仓库对拍 Gp0 解码器同构):
 *   0 misc:   0x02 填充 3 字 (头=色 RGB8@23-0, coord=x9-0|y<<16, size=w&0x3FF步16|h<<16)
 *             0x1F IRQ1; 其余镜像 NOP
 *   1 多边形: 头(cmd|色), xy0, [uv0], {xy_i, [uv_i], [col_i]} (i=1..n-1)
 *             bit28=Gouraud bit27=四顶点 bit26=纹理; 字数=1+n*(1+tex)+(g?n-1:0)
 *   3 矩形/精灵: 纹里=头(xy!)+[size(变尺寸)]+clut; 非纹里=头(色)+[size]+coord
 *             size=(>>27)&3: 0 变 1=1×1 2=8×8 3=16×16; 原始模式(E1 位25)=数据流内联
 *   5 CPU→VRAM: 0xA0 3 字头 (命令|coord|size) + 数据流 (半字对低先)
 *   7 环境:   E1 纹里页(0-3 X/4, 4-5+16-17 Y/8, 25 原始, 26-27 深度) E2 纹窗
 *             E3/E4 区界 E5 偏移(11-bit 符号) E6 遮罩
 * v1 诚实面: Gouraud=真重心插值; 纹理采样=4/8bit CLUT+15bit 直接; 24bit 与
 * 半透明/遮罩/纹窗=登记未实现(观测计数); 硬件面=扫描输出 compute
 * (psx_gpu_scanout_spv.h → vk_host_present_gpu16: 上载→设备端 5551→RGBA8→blit)。
 */
#include "psx_gpu.h"
#include "psx_core.h"
#include "psx_dev.h"
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

static uint16_t *VR(psx_gpu_t *g, int x, int y)
{ return &g->vram[(y & 511) * 1024 + (x & 1023)]; }
static uint16_t rgb555(int r, int gg, int b)
{ return (uint16_t)(((r >> 3) & 0x1F) | (((gg >> 3) & 0x1F) << 5) | (((b >> 3) & 0x1F) << 10)); }
static uint32_t to_rgba(uint16_t c, int on)
{
    if (!on) return 0xFF000000u;
    int r = (c & 0x1F) << 3, gg = ((c >> 5) & 0x1F) << 3, b = ((c >> 10) & 0x1F) << 3;
    return 0xFF000000u | (b << 16) | (gg << 8) | r;
}
static int cx11(uint32_t v) { return (int)(int32_t)(v << 21) >> 21; }
static int cy11(uint32_t v) { return (int)(int32_t)(v << 5) >> 21; }  /* bits16-26 → <<5 对齐符号位 → >>21 符号扩展 */

/* ---------------- 封包机 ---------------- */
#define PKT_CAP 8192
typedef struct {
    uint32_t head;
    int family, n, gouraud, textured, sizecode, raw;
    int need_hdr;      /* 头部参数字数 */
    int data_words;    /* 原始/传输数据字数 (流式) */
    uint32_t *buf;     /* [0..need_hdr) 参数; 数据相位复用同区 */
    int wn;            /* 参数计数 */
} pkt_t;
static pkt_t pk;
static uint32_t pkt_store[PKT_CAP];

static void pkt_reset(void)
{
    memset(&pk, 0, sizeof pk);
    pk.buf = pkt_store;
}

static void tri_raster(psx_gpu_t *g, int x0, int y0, uint16_t c0, int x1, int y1, uint16_t c1,
                       int x2, int y2, uint16_t c2, int gouraud)
{
    int minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    int maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int miny = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int maxy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > 1023) maxx = 1023;
    if (maxy > 511) maxy = 511;
    long area = (long)(x1 - x0) * (y2 - y0) - (long)(x2 - x0) * (y1 - y0);
    if (area == 0) return;
    for (int py = miny; py <= maxy; py++)
        for (int px = minx; px <= maxx; px++) {
            long w0 = (long)(x1 - px) * (y2 - py) - (long)(x2 - px) * (y1 - py);
            long w1 = (long)(x2 - px) * (y0 - py) - (long)(x0 - px) * (y2 - py);
            long w2 = (long)(x0 - px) * (y1 - py) - (long)(x1 - px) * (y0 - py);
            if ((area > 0 && (w0 < 0 || w1 < 0 || w2 < 0)) ||
                (area < 0 && (w0 > 0 || w1 > 0 || w2 > 0))) continue;
            uint16_t col = c0;
            if (gouraud) {
                int r  = (int)((w0 * (c0 & 0x1F) + w1 * (c1 & 0x1F) + w2 * (c2 & 0x1F)) / area) & 0x1F;
                int gg = (int)((w0 * ((c0 >> 5) & 0x1F) + w1 * ((c1 >> 5) & 0x1F)
                                + w2 * ((c2 >> 5) & 0x1F)) / area) & 0x1F;
                int b  = (int)((w0 * (c0 >> 10) + w1 * (c1 >> 10) + w2 * (c2 >> 10)) / area) & 0x1F;
                col = (uint16_t)(r | (gg << 5) | (b << 10));
            }
            *VR(g, px, py) = col; g->tri_px++;
        }
}

/* 纹里页原点 (psx-spx E1: X 步4 位0-3, Y 步8 位4-5+16-17) */
static void tpage_origin(psx_gpu_t *g, int *tx, int *ty)
{
    *tx = (int)(g->tex_page & 0xF) * 4;
    *ty = (int)(((g->tex_page >> 4) & 1u) | ((g->tex_page >> 15) & 0x3Eu)) * 8;
}

static void rect_exec(psx_gpu_t *g)
{
    uint32_t h = pk.head;
    int p = 0;
    int x0, y0, w, hh;
    if (pk.textured) {
        x0 = (int)(pk.buf[0] & 0xFFFF); y0 = (int)((pk.buf[0] >> 16) & 0xFFFF); p = 1;
        if (pk.sizecode == 0) { w = (int)(pk.buf[p] & 0xFFFF); hh = (int)((pk.buf[p] >> 16) & 0xFFFF); p++; }
        else { w = hh = pk.sizecode == 1 ? 1 : (pk.sizecode == 2 ? 8 : 16); }
        /* pk.buf[p] = clut (原始模式忽略) */
        if (pk.raw) {
            const uint32_t *d = pk.buf + p + 1;
            for (int row = 0; row < hh; row++)
                for (int c2 = 0; c2 < w; c2 += 2) {
                    uint32_t wd = d[row * (w / 2) + c2 / 2];
                    *VR(g, x0 + c2,     y0 + row) = (uint16_t)(wd & 0xFFFF);
                    *VR(g, x0 + c2 + 1, y0 + row) = (uint16_t)(wd >> 16);
                    g->spr_px += 2;
                }
        } else {
            int tx, ty; tpage_origin(g, &tx, &ty);
            uint32_t clut = pk.buf[p];
            int cux = (int)(clut & 0x3F) * 4, cuy = (int)((clut >> 6) & 0x1FF) * 8;
            int depth = (int)((g->tex_page >> 26) & 3);
            for (int row = 0; row < hh; row++)
                for (int c2 = 0; c2 < w; c2++) {
                    uint16_t t;
                    if (depth == 2) {
                        t = *VR(g, tx + c2, ty + row);
                    } else {
                        uint32_t byteidx = ((unsigned)(ty + row) * 1024u + (unsigned)(tx + c2));
                        uint8_t b;
                        if (depth == 0) { /* 4bit CLUT */
                            uint8_t bw = (uint8_t)(*(uint16_t *)&g->vram[0] & 0); /* placeholder never used */
                            (void)bw; uint16_t wd = g->vram[(byteidx / 4u)];
                            uint8_t nib = (byteidx & 1u) ? (uint8_t)((wd >> ((byteidx >> 1) % 4u * 4u)) & 0xF)
                                                         : (uint8_t)(wd & 0xF);
                            t = VR(g, cux + (nib & 0x0F), cuy)[0];
                        } else { /* 8bit CLUT */
                            uint16_t wd = g->vram[byteidx / 2u];
                            uint8_t by = (uint8_t)((byteidx & 1u) ? (wd >> 16) : (wd & 0xFF));
                            t = VR(g, cux + (by & 0xFF), cuy)[0];
                        }
                        (void)b;
                    }
                    *VR(g, x0 + c2, y0 + row) = t; g->spr_px++;
                }
        }
    } else {
        uint16_t col = rgb555((int)(h & 0xFF), (int)((h >> 8) & 0xFF), (int)((h >> 16) & 0xFF));
        if (pk.sizecode == 0) { w = (int)(pk.buf[0] & 0xFFFF); hh = (int)((pk.buf[0] >> 16) & 0xFFFF); }
        else { w = hh = pk.sizecode == 1 ? 1 : (pk.sizecode == 2 ? 8 : 16); }
        { int ci = pk.sizecode == 0 ? 1 : 0;
          x0 = (int)(pk.buf[ci] & 0xFFFF); y0 = (int)((pk.buf[ci] >> 16) & 0xFFFF); }
        for (int row = 0; row < hh; row++)
            for (int c2 = 0; c2 < w; c2++) { *VR(g, x0 + c2, y0 + row) = col; g->fill_px++; }
    }
}

static void fill_exec(psx_gpu_t *g)
{
    uint32_t h = pk.head, coord = pk.buf[0], size = pk.buf[1];
    int x = (int)(coord & 0x3FF), y = (int)((coord >> 16) & 0x1FF);
    int w = (int)(((size & 0x3FF) + 0xF) & ~0xF), hh = (int)((size >> 16) & 0x1FF);
    uint16_t col = rgb555((int)(h & 0xFF), (int)((h >> 8) & 0xFF), (int)((h >> 16) & 0xFF));
    for (int row = 0; row < hh; row++)
        for (int c2 = 0; c2 < w; c2++) { *VR(g, x + c2, y + row) = col; g->fill_px++; }
}

static void poly_exec(psx_gpu_t *g)
{
    uint32_t h = pk.head;
    int n = pk.n, stride = 1 + pk.textured;
    uint16_t cols[4]; int xs[4], ys[4];
    cols[0] = rgb555((int)(h & 0xFF), (int)((h >> 8) & 0xFF), (int)((h >> 16) & 0xFF));
    int p = 0;
    xs[0] = cx11(pk.buf[0]) + g->draw_off_x; ys[0] = cy11(pk.buf[0]) + g->draw_off_y; p += stride;
    cols[n - 1] = cols[0];
    for (int i = 1; i < n; i++) {
        xs[i] = cx11(pk.buf[p]) + g->draw_off_x; ys[i] = cy11(pk.buf[p]) + g->draw_off_y; p += stride;
        if (pk.gouraud && i < n - 1) {
            uint32_t cw = pk.buf[p++];
            cols[i] = rgb555((int)(cw & 0xFF), (int)((cw >> 8) & 0xFF), (int)((cw >> 16) & 0xFF));
        }
    }
    if (pk.gouraud) cols[n - 1] = cols[0]; /* 末顶点色=真机语义? v1: 包内无末色字 → 复用首色, 登记 */
    tri_raster(g, xs[0], ys[0], cols[0], xs[1], ys[1], cols[1], xs[2], ys[2], cols[2], pk.gouraud);
    if (n == 4)
        tri_raster(g, xs[0], ys[0], cols[0], xs[2], ys[2], cols[2], xs[3], ys[3], cols[3], pk.gouraud);
}

static void xfer_exec(psx_gpu_t *g)
{
    uint32_t coord = pk.buf[0], size = pk.buf[1];
    int x0 = (int)(coord & 0x3FF), y0 = (int)((coord >> 16) & 0x1FF);
    int w = (int)((((size & 0xFFFF) - 1) & 0x3FF) + 1), hh = (int)((((size >> 16) & 0xFFFF) - 1) & 0x1FF) + 1;
    const uint32_t *d = pk.buf + 2;
    int avail = pk.wn - 2; if (avail < 0) avail = 0;
    uint32_t total = (uint32_t)w * (uint32_t)hh, idx = 0;
    for (int i = 0; idx + 1 < total && i < avail; i++) {
        *VR(g, x0 + (int)(idx % (unsigned)w), y0 + (int)(idx / (unsigned)w)) = (uint16_t)(d[i] & 0xFFFF); idx++;
        *VR(g, x0 + (int)(idx % (unsigned)w), y0 + (int)(idx / (unsigned)w)) = (uint16_t)(d[i] >> 16); idx++;
    }
    g->xfer_px += idx;
}

static int rect_dw(int w, int hh) { return w * hh / 2; }

unsigned g_gp0_hist[8];                      /* task-148: 命令族直方图(观测面) */
void psx_gpu_gp0(psx_gpu_t *g, uint32_t w)
{
    g->gp0_words++;
    if (pk.data_words == 0) g_gp0_hist[(w >> 29) & 7u]++;   /* 仅命令头计数 */
    { extern void gp0_trace_word(uint32_t w); 
      extern int g_gpu_wordtrace_n;
      if (g_gpu_wordtrace_n < 40 && (w >> 26) != 0x3Fu) { gp0_trace_word(w); } }
    if (pk.data_words > 0) {                              /* 数据流相位 */
        if (pk.wn < PKT_CAP) pk.buf[pk.wn++] = w;
        if (--pk.data_words == 0) {
            if (pk.family == 3) rect_exec(g);
            else if (pk.family == 5) xfer_exec(g);
            pkt_reset();
        }
        return;
    }
    if (pk.need_hdr > 0) {                                /* 参数相位 */
        if (pk.wn < PKT_CAP) pk.buf[pk.wn++] = w;
        if (--pk.need_hdr == 0) {
            if (pk.family == 0) { fill_exec(g); pkt_reset(); return; }
            if (pk.family == 1) { poly_exec(g); pkt_reset(); return; }
            if (pk.family == 5) {                       /* 头满 → 数据流 */
                uint32_t size = pk.buf[1];
                int wd = (int)((((size & 0xFFFF) - 1) & 0x3FF) + 1);
                int hh2 = (int)((((size >> 16) & 0xFFFF) - 1) & 0x1FF) + 1;
                pk.data_words = wd * hh2 / 2;
                if (pk.data_words <= 0 || pk.wn + pk.data_words > PKT_CAP) {
                    xfer_exec(g); pkt_reset();          /* 超限=按已收执行(诚实降级) */
                }
                return;
            }
            if (pk.family == 3) {
                if (pk.textured && pk.raw) {
                    int sw, sh;
                    if (pk.sizecode == 0) { sw = (int)(pk.buf[pk.wn - 1] & 0xFFFF); sh = (int)((pk.buf[pk.wn - 1] >> 16) & 0xFFFF); }
                    else { sw = sh = pk.sizecode == 1 ? 1 : (pk.sizecode == 2 ? 8 : 16); }
                    pk.data_words = rect_dw(sw, sh);
                    if (pk.data_words <= 0 || pk.wn + pk.data_words > PKT_CAP) { rect_exec(g); pkt_reset(); }
                    return;
                }
                rect_exec(g); pkt_reset(); return;
            }
            pkt_reset();
        }
        return;
    }
    int fam = (int)(w >> 29);
    uint32_t op8 = (w >> 24) & 0xFF;
    pkt_reset();
    pk.head = w;
    switch (fam) {
    case 0:
        if (op8 == 0x02) { pk.family = 0; pk.need_hdr = 2; }
        else {   /* OTZ 表头 (真盘主命令形态, task-148): bits0-25=下一命令缓冲地址,
                  * bits26-29=z。GPU 端 fetch: 从该地址读命令字重派发(深度护栏防环)。 */
            /* OTZ 链 = GPU 内建链表遍历: 沿链找第一枚真命令(非 OTZ 头)。
             * 环检测 = 访问地址去重(32 槽窗口足够: 真表零命令时链短; 环=立即停)。
             * 深度上限 4096 = OT 槽数上限。 */
            /* GPU 正字: OTZ 低 26 位 = **VRAM 地址**(半字粒度)。Psy-Q 经典流 =
             * 命令块直写 VRAM(0xA0 传输), FIFO 只送 OTZ 链 → 本核从 VRAM 沿链吃:
             * 命令头→按包宽推进→下一 OTZ→…直到 0xFF000064 terminator(本表尾)。*/
            uint32_t vram_half = w & 0x03FFFFFFu;
            static int otz_busy = 0;
            if ((vram_half >> 1) < (1024u * 512u) && otz_busy < 512) {
                uint16_t *vp = &g->vram[0];   /* VRAM 16bpp 字数组(半字/2) */
                uint32_t vi = (vram_half >> 1) & ((1024u * 512u) - 1u);
                uint32_t cmd = (uint32_t)vp[vi] | ((uint32_t)vp[vi + 1] << 16);
                if (cmd != 0u) {
                    otz_busy++;
                    psx_gpu_gp0(g, cmd);
                    if ((cmd >> 24) == 0xFFu) { /* terminator: 本 OT 表结束 */ }
                    otz_busy--;
                }
            }
        }
        return;
    case 1:
        pk.family = 1; pk.n = ((w >> 27) & 1) ? 4 : 3;
        pk.gouraud = (int)((w >> 28) & 1); pk.textured = (int)((w >> 26) & 1);
        pk.need_hdr = pk.n * (1 + pk.textured) + (pk.gouraud ? pk.n - 1 : 0);
        break;
    case 3:
        pk.family = 3; pk.textured = (int)((w >> 26) & 1); pk.sizecode = (int)((w >> 27) & 3);
        pk.raw = g->raw_mode;
        /* PSYQ 布局: xy 恒有; 非纹里=[size?]+coord→ xy+size?; 纹里 SPRT=xy,clut[,size] */
        pk.need_hdr = 1 + (pk.sizecode == 0 ? 1 : 0) + (pk.textured ? 1 : 0);
        break;
    case 5:
        pk.family = 5; pk.need_hdr = 2; break;
    case 7:
        switch (op8) {
        case 0xFF: return;   /* Terminator(OT 表尾, task-148 真盘 OTZW 实证) */
        case 0xE1: g->tex_page = w & 0xFFFFFF; g->raw_mode = (int)((w >> 22) & 1u); break; /* v1 契约: 位22=原始纹理(自研 HBEXE ABI, 真机面待游戏 trace 楔对拍) */
        case 0xE2: g->tex_window = w & 0xFFFFFF; break;
        case 0xE3: case 0xE4: break;
        case 0xE5: { uint32_t v = w & 0xFFFFFF;
            g->draw_off_x = cx11(v); g->draw_off_y = cy11(v); break; }
        case 0xE6: g->mask_set = w & 0xFFFFFF; break;
        default: break;
        }
        return;
    default:                                              /* 线/Blit 族 v1 观测 */
        break;
    }
}

void psx_gpu_gp1(psx_gpu_t *g, uint32_t w)
{
    uint32_t op = (w >> 24) & 0xFF;
    g->gp1_words++;
    switch (op) {
    case 0x00:
        g->tex_page = g->tex_window = g->mask_set = 0;
        g->draw_off_x = g->draw_off_y = 0; g->disp_on = 0; g->raw_mode = 0;
        g->mode_24bpp = (int)((w >> 23) & 1);
        g->disp_x0 = g->disp_y0 = 0; g->disp_x1 = 0x3FF; g->disp_y1 = 0xFF;
        g->fb_x = g->fb_y = 0; pkt_reset(); break;
    case 0x02: g->disp_x1 = w & 0x3FF; g->disp_y1 = (w >> 16) & 0xFF; break;
    case 0x03: g->disp_x0 = w & 0x3FF; g->disp_y0 = (w >> 16) & 0xFF; break;
    case 0x04: g->fb_x = w & 0x3FF; g->fb_y = (w >> 16) & 0xFF; break;
    case 0x05: g->disp_on = 1; break;
    case 0x06: g->disp_on = 0; break;
    default: break;                                       /* 01/07/08 vq/ack 观测 */
    }
}

void psx_gpu_tick(psx_gpu_t *g)
{
    int w = (int)(g->disp_x1 - g->disp_x0) + 1;
    int h = (int)(g->disp_y1 - g->disp_y0) + 1;
    if (w <= 0 || w > 1024) w = 320;
    if (h <= 0 || h > 512) h = 240;
    if (w != g->fb_w || h != g->fb_h || !g->fb) {
        free(g->fb); g->fb = calloc((size_t)w * (size_t)h, 4);
        g->fb_w = w; g->fb_h = h;
    }
    if (g->fb) {
        for (int y = 0; y < h; y++) {
            const uint16_t *row = &g->vram[((g->fb_y + (unsigned)y) & 511) * 1024
                                           + ((g->fb_x + g->disp_x0) & 1023)];
            uint32_t *dst = g->fb + (size_t)y * (size_t)w;
            for (int x = 0; x < w; x++) dst[x] = to_rgba(row[x & 0x3FF], g->disp_on);
        }
    }
    if (g->hw_present) g->hw_present(g->hw_ctx, g);      /* Vulkan 硬件通道 */
    g->frames++;
}

void psx_gpu_attach_core(void *g, void *core) { ((psx_gpu_t *)g)->core = core; }
void psx_gpu_attach_hw(psx_gpu_t *g, void *ctx, int (*present)(void *, psx_gpu_t *))
{ g->hw_ctx = ctx; g->hw_present = present; }

int psx_gpu_init(psx_gpu_t *g)
{
    memset(g, 0, sizeof *g);
    pkt_reset();
    g->disp_x1 = 0x3FF; g->disp_y1 = 0xFF;
    return 0;
}
void psx_gpu_free(psx_gpu_t *g) { free(g->fb); g->fb = NULL; }

/* ---------------- --psx-window: 冷启图形 HBEXE → tick → 硬件 present ---------------- */
#include "psx_core.h"
#include "psx_cpu.h"
#include "psx_hle.h"
#include "psx_cdrom.h"
extern void *vk_open(int w, int h, const char *title);
extern int vk_push(void *v, const uint32_t *fb, int w, int h);
extern int vk_host_present_gpu16(void *v, const uint16_t *vram, int ox, int oy, int w, int h, int disp_on);
extern int vk_alive(void *v);
extern void vk_close(void *v);
extern int psx_disc_open(psx_disc_t *d, const char *path);

static int hw_shim_present(void *ctx, psx_gpu_t *g)
{
    return vk_host_present_gpu16(ctx, g->vram, (int)(g->fb_x + g->disp_x0) & 1023,
                                 (int)(g->fb_y + g->disp_y0) & 511, g->fb_w, g->fb_h, g->disp_on);
}

/* HBEXE 图形程序: 蓝填充 + 红传输块(0xA0) + 绿原始精灵(0x7C, E1 位25) + 环 */
static void gb_build_exe(uint8_t *img, uint32_t n)
{
    const uint32_t C = 0x80040100u;
    #define ADDIU(rt,im) ((0x09u << 26) | (0u << 21) | ((rt) << 16) | ((im) & 0xFFFFu))
    #define ADDIUR(rs,rt,im) ((0x09u << 26) | ((rs) << 21) | ((rt) << 16) | ((im) & 0xFFFFu))
    #define ORI(rt,im) ((0x0Du << 26) | ((rt) << 21) | ((rt) << 16) | ((im) & 0xFFFFu))  /* rs=rt (LUI+ORI 惯用) */
    #define LUI(rt,im) ((0x0Fu << 26) | ((rt) << 16) | ((im) & 0xFFFFu))
    #define SW(rt,ofs,bs) ((0x2Bu << 26) | ((bs) << 21) | ((rt) << 16) | ((ofs) & 0xFFFFu))
    #define J(t) ((0x02u << 26) | (((t) >> 2) & 0x03FFFFFFu))
    #define LDC(rt, val32) do { uint32_t vv=(val32); \
        if ((vv & 0xFFFFu) == 0u) *w++ = LUI(rt, (vv >> 16) & 0xFFFFu); \
        else if (vv >> 16) { *w++ = LUI(rt, (uint32_t)((int32_t)(vv + 0x8000) >> 16) & 0xFFFFu); *w++ = ORI(rt, vv & 0xFFFFu); } \
        else *w++ = ORI(rt, vv & 0xFFFFu); } while (0)
    static uint32_t codebuf[704];            /* 独立缓冲(img+0x900 会被 full() memset 清掉=自拷贝 bug) */
    uint32_t *code = codebuf;
    uint32_t *w = code; uint32_t k;
    *w++ = LUI(16, 0x1F80); *w++ = ORI(16, 0x1800);      /* $16 = GP0 口 */
    *w++ = ADDIUR(16, 17, 4);                            /* $17 = GP1 口 */
    LDC(18, 0x00000000u); *w++ = SW(18, 0, 17);          /* GP1 复位 */
    LDC(18, 0x03000000u); *w++ = SW(18, 0, 17);          /* 区起点 (0,0) */
    LDC(18, 0x02000000u | (239u << 16) | 319u); *w++ = SW(18, 0, 17);
    LDC(18, 0x04000000u); *w++ = SW(18, 0, 17);          /* FB 原点 */
    LDC(18, 0x05000000u); *w++ = SW(18, 0, 17);          /* 显示开 */
    LDC(18, 0x02000000u | ((uint32_t)0xFFu << 16));      /* 蓝填充 */
    *w++ = SW(18, 0, 16);
    LDC(18, 0x00000000u); *w++ = SW(18, 0, 16);
    LDC(18, (240u << 16) | 320u); *w++ = SW(18, 0, 16);
    LDC(18, 0xA0000000u); *w++ = SW(18, 0, 16);          /* CPU→VRAM (psx-spx 0xA0) */
    LDC(18, (120u << 16) | 160u); *w++ = SW(18, 0, 16);
    LDC(18, (16u << 16) | 16u); *w++ = SW(18, 0, 16);
    for (k = 0; k < 128; k++) { LDC(18, 0x001F001Fu); *w++ = SW(18, 0, 16); }
    LDC(18, 0xE5000000u); *w++ = SW(18, 0, 16);          /* 偏移 0 */
    LDC(18, 0xE1000000u | (1u << 22)); *w++ = SW(18, 0, 16);   /* E1 原始模式(位22) */
    LDC(18, 0x7C000000u); *w++ = SW(18, 0, 16);          /* 纹里矩形 16×16 (0x7C) */
    LDC(18, (50u << 16) | 200u); *w++ = SW(18, 0, 16);   /* xy */
    LDC(18, 0x00000000u); *w++ = SW(18, 0, 16);          /* clut(原始=占位) */
    for (k = 0; k < 128; k++) { LDC(18, 0x03E003E0u); *w++ = SW(18, 0, 16); }
    { uint32_t loop_addr = C + (uint32_t)((w - code) * 4u);
      *w++ = J(loop_addr); *w++ = 0u; }
    #undef LDC
    psx_cd_synth_exe_full(img, n, code, (uint32_t)((w - code) * 4u));
}

int cmd_psx_window(int argc, char **argv)
{
    const char *iso = "/tmp/hb_window.iso";
    const char *mode = "hw";
    int seconds = 0;
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--iso") == 0) iso = argv[i + 1];
        if (strcmp(argv[i], "--gpumode") == 0) mode = argv[i + 1];
        if (strcmp(argv[i], "--seconds") == 0) seconds = atoi(argv[i + 1]);
    }
    static psx_core_t core; static psx_cpu_t cpu; static psx_hle_t hle;
    static psx_dev_t dev; static psx_gpu_t gpu; static psx_disc_t disc;
    if (psx_core_init_fixed(&core) != 0) { fprintf(stderr, "PSXWIN core FAIL\n"); return 1; }
    psx_gpu_init(&gpu); psx_dev_init(&dev);
    psx_cpu_reset(&cpu, &core); core.dev = &dev; dev.gpu = &gpu;
    psx_hle_install(&hle, &cpu, &core);
    if (psx_disc_open(&disc, iso) != 0 && strstr(iso, "hb_window") != NULL) {
        uint8_t *img = calloc(1, 0x4000u);
        if (!img) return 1;
        gb_build_exe(img, 0x4000u);
        if (psx_iso_build("/tmp/hb_window.iso", img, 0x4000u, "PSX.EXE", 1) != 0) { free(img); return 1; }
        free(img);
        if (psx_disc_open(&disc, "/tmp/hb_window.iso.cue") != 0) { fprintf(stderr, "PSXWIN fixture disc FAIL\n"); return 1; }
    } else if (psx_disc_open(&disc, iso) != 0) { fprintf(stderr, "PSXWIN disc FAIL\n"); return 1; }
    hle.disc = &disc;
    uint32_t entry = 0;
    if (psx_hle_coldboot(&hle, &disc, &entry) != 0) { fprintf(stderr, "PSXWIN coldboot FAIL\n"); return 1; }
    void *vk = vk_open(320, 240, "linbleePSX cleanroom (zero BIOS) -- PSX window");
    if (!vk) { fprintf(stderr, "PSXWIN no display (selftest 面仍绿)\n"); psx_gpu_free(&gpu); psx_core_free(&core); return 2; }
    int hw = strcmp(mode, "soft") != 0;
    if (hw) psx_gpu_attach_hw(&gpu, vk, hw_shim_present);
    double elapsed = 0.0; const double limit = seconds > 0 ? (double)seconds : 1e18;
    while (vk_alive(vk)) {
        psx_cpu_run(&cpu, 20000u);
        psx_gpu_tick(&gpu);
        if (!hw && gpu.fb) vk_push(vk, gpu.fb, gpu.fb_w, gpu.fb_h);
        struct timespec ts = { 0, 16 * 1000 * 1000 };
        nanosleep(&ts, NULL);
        elapsed += 0.016;
        if (elapsed >= limit) break;
    }
    printf("PSXWIN frames=%u hw=%d fill=%u tri=%u spr=%u xfer=%u gp0=%u gp1=%u\n",
           gpu.frames, hw, gpu.fill_px, gpu.tri_px, gpu.spr_px, gpu.xfer_px, gpu.gp0_words, gpu.gp1_words);
    vk_close(vk); psx_gpu_free(&gpu); psx_disc_close(&disc); psx_core_free(&core);
    return 0;
}

/* ---------------- --gpu-selftest ---------------- */
#define GCHK(cond, name) do { printf("GPU_TEST %s: %s\n", name, (cond) ? "PASS" : "FAIL"); \
    if (!(cond)) bad++; } while (0)

int cmd_gpu_selftest(int argc, char **argv)
{
    (void)argc; (void)argv;
    int bad = 0;
    static psx_gpu_t g; static psx_dev_t dev_stub;
    psx_gpu_init(&g);

    /* G1 GP1 显示设置 */
    psx_gpu_gp1(&g, 0x00000000u);
    psx_gpu_gp1(&g, 0x03000000u);
    psx_gpu_gp1(&g, 0x02000000u | (239u << 16) | 319u);
    psx_gpu_gp1(&g, 0x04000000u);
    psx_gpu_gp1(&g, 0x05000000u);
    GCHK(g.disp_on == 1 && g.disp_y1 == 239 && g.disp_x1 == 319, "g1_gp1_display");

    /* G2 0x02 全蓝填充 */
    psx_gpu_gp0(&g, 0x02000000u | (0xFFu << 16));
    psx_gpu_gp0(&g, 0u);
    psx_gpu_gp0(&g, (240u << 16) | 320u);
    GCHK(*VR(&g, 100, 100) == rgb555(0, 0, 255) && g.fill_px == 320u * 240u, "g2_fill_blue");

    /* G3 0xA0 CPU→VRAM 16×16 红块 */
    psx_gpu_gp0(&g, 0xA0000000u);
    psx_gpu_gp0(&g, (120u << 16) | 160u);
    psx_gpu_gp0(&g, (16u << 16) | 16u);
    for (int i = 0; i < 128; i++) psx_gpu_gp0(&g, 0x001F001Fu);
    GCHK(*VR(&g, 165, 125) == 0x001Fu && *VR(&g, 100, 100) == rgb555(0, 0, 255) && pk.data_words == 0,
         "g3_xfer_red_block");

    /* G4 原始精灵 16×16 绿 (0x7C + E1 位25) */
    psx_gpu_gp0(&g, 0xE5000000u);
    psx_gpu_gp0(&g, 0xE1000000u | (1u << 22));
    psx_gpu_gp0(&g, 0x7C000000u);                        /* 0x7C 16×16 纹里 */
    psx_gpu_gp0(&g, (50u << 16) | 200u);                 /* xy */
    psx_gpu_gp0(&g, 0x00000000u);                        /* clut 占位 */
    for (int i = 0; i < 128; i++) psx_gpu_gp0(&g, 0x03E003E0u);
    GCHK(*VR(&g, 205, 55) == 0x03E0u && g.spr_px == 256u, "g4_sprite16_raw_green");

    /* G5 平涂三角 0x20 (头|xy0|xy1|xy2) */
    psx_gpu_gp0(&g, 0x20000000u | 0xFFu);                /* 红 */
    psx_gpu_gp0(&g, (10u << 16) | 10u);
    psx_gpu_gp0(&g, (10u << 16) | 40u);
    psx_gpu_gp0(&g, (40u << 16) | 10u);
    printf("GPU_EV g5 need=%d dw=%d fam=%d wn=%d tri=%u p15=%04x\n",
           pk.need_hdr, pk.data_words, pk.family, pk.wn, g.tri_px, *VR(&g, 15, 15));
    GCHK(*VR(&g, 15, 15) == 0x001Fu && g.tri_px > 400u, "g5_flat_triangle");

    /* G6 tick → fb RGBA */
    psx_gpu_tick(&g);
    GCHK(g.frames == 1u && g.fb_w == 320 && g.fb_h == 240 &&
         g.fb[100 * 320 + 100] == (0xFF000000u | (248u << 16)) &&   /* RGB555→888 精度=<<3 → 248 */
         g.fb[125 * 320 + 165] == (0xFF000000u | 248u) &&
         g.fb[55 * 320 + 205] == (0xFF000000u | (248u << 8)), "g6_framebuffer_rgba");

    /* G7 显示关黑场 */
    psx_gpu_gp1(&g, 0x06000000u); psx_gpu_tick(&g);
    GCHK(g.fb[100 * 320 + 100] == 0xFF000000u && g.frames == 2u, "g7_dispoff_black");

    /* G8 端口路由 */
    psx_dev_init(&dev_stub);
    psx_dev_gpu_attach(&dev_stub, &g);
    psx_gpu_gp1(&g, 0x00000000u); psx_gpu_gp1(&g, 0x05000000u);
    {
        uint32_t f0 = g.fill_px;
        psx_dev_write(&dev_stub, 0x1800u, 0x02000000u | 0x1F1F1Fu, 4);
        psx_dev_write(&dev_stub, 0x1800u, (300u << 16) | 600u, 4);
        psx_dev_write(&dev_stub, 0x1800u, (16u << 16) | 16u, 4);
        GCHK(g.fill_px == f0 + 16u * 16u, "g8_gp0_port_routing");
        psx_dev_write(&dev_stub, 0x1804u, 0x06000000u, 4);
        GCHK(g.disp_on == 0, "g8b_gp1_port_routing");
    }
    psx_gpu_free(&g);
    printf("GPU_EV bad=%d frames=%u fill=%u tri=%u spr=%u xfer=%u\n",
           bad, g.frames, g.fill_px, g.tri_px, g.spr_px, g.xfer_px);
    if (bad) { printf("GPU SELFTEST FAIL (%d)\n", bad); return 1; }
    printf("GPU CORE SELFTEST PASS\n");
    return 0;
}

int g_gpu_wordtrace_n = 0;
uint32_t g_gpu_wordtrace[40];
static uint32_t g_wt_ring[40]; static int g_wt_i = 0;
void gp0_trace_word(uint32_t w)
{
    g_wt_ring[g_wt_i = (g_wt_i + 1) % 40] = w;
    if (g_gpu_wordtrace_n < 40) { g_gpu_wordtrace[g_gpu_wordtrace_n++] = w; }
}
void gp0_trace_dump(void)
{
    for (int z = 0; z < g_gpu_wordtrace_n; z++)
        fprintf(stderr, "GP0W%02d %08x (top3=%x top2=%x z=%x addr=%x)\n", z, g_gpu_wordtrace[z],
                g_gpu_wordtrace[z] >> 29, g_gpu_wordtrace[z] >> 30,
                (g_gpu_wordtrace[z] >> 24) & 0x1F, g_gpu_wordtrace[z] & 0xFFFFFF);
}

void otz_trace(uint32_t next, uint32_t cmd)
{
    extern int g_gpu_wordtrace_n; extern uint32_t g_gpu_wordtrace[40];
    if (g_gpu_wordtrace_n < 40)
        fprintf(stderr, "OTZW%02d next=%06x cmd=%08x (fam=%x op8=%02x)\n",
                g_gpu_wordtrace_n++, next, cmd, cmd >> 29, (cmd >> 24) & 0xFF);
}
