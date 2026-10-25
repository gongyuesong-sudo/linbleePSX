/*
 * resources.c —— 任务004+: 原 .rsrc 资源引擎 (图标 DIB / 对话框模板) + UI 合成
 *
 * 解析对象 (tools/dump_resources.py 从原 EXE 逐字节提取):
 *   type14_idN.bin  GRPICONDIR 组目录 (条目14B, nID 在 +12)
 *   type3_idN.bin   RT_ICON 裸 DIB (BITMAPINFOHEADER, biHeight=2*h 含 AND 掩码,
 *                   bpp 1/4/8/32, 调色板索引 + 1bpp AND mask, 行 bottom-up)
 *   type5_idN.bin   DLGTEMPLATEEX 风格对话框模板 (16B 控件头, UTF-16 文本,
 *                   class 原子 FFFF:0x0080=BUTTON 0x0082=EDIT)
 *
 * 入口:
 *   cmd_render_ui  离屏合成 → PPM (+单帧 present)
 *   cmd_window     常驻交互窗口 (~30fps, ESC/关窗退出) —— 窗口模式
 *
 * FULL-FEATURE: 展示的即完整功能集 (无时间限制/音效/记忆卡按完整版开放),
 * 原 demo 的禁用横幅不复存在。
 */
#include "bleem_native.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define UI_W 960
#define UI_H 540

typedef struct { uint32_t *px; int w, h; } rgba_t; /* uint32_t BGRA, alpha=bits24-31 */

/* ---------- 5x7 位图字体 (大写 + 数字 + 常用符号) ---------- */
static const uint8_t F[] = {
/*A*/0x0E,0x11,0x11,0x1F,0x11,0x11,0x11,/*B*/0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E,
/*C*/0x0E,0x11,0x10,0x10,0x10,0x11,0x0E,/*D*/0x1C,0x12,0x11,0x11,0x11,0x12,0x1C,
/*E*/0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F,/*F*/0x1F,0x10,0x10,0x1E,0x10,0x10,0x10,
/*G*/0x0E,0x11,0x10,0x17,0x11,0x11,0x0F,/*H*/0x11,0x11,0x11,0x1F,0x11,0x11,0x11,
/*I*/0x0E,0x04,0x04,0x04,0x04,0x04,0x0E,/*J*/0x07,0x02,0x02,0x02,0x02,0x12,0x0C,
/*K*/0x11,0x12,0x14,0x18,0x14,0x12,0x11,/*L*/0x10,0x10,0x10,0x10,0x10,0x10,0x1F,
/*M*/0x11,0x1B,0x15,0x15,0x11,0x11,0x11,/*N*/0x11,0x19,0x15,0x13,0x11,0x11,0x11,
/*O*/0x0E,0x11,0x11,0x11,0x11,0x11,0x0E,/*P*/0x1E,0x11,0x11,0x1E,0x10,0x10,0x10,
/*Q*/0x0E,0x11,0x11,0x11,0x15,0x12,0x0D,/*R*/0x1E,0x11,0x11,0x1E,0x14,0x12,0x11,
/*S*/0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E,/*T*/0x1F,0x04,0x04,0x04,0x04,0x04,0x04,
/*U*/0x11,0x11,0x11,0x11,0x11,0x11,0x0E,/*V*/0x11,0x11,0x11,0x11,0x11,0x0A,0x04,
/*W*/0x11,0x11,0x11,0x15,0x15,0x1B,0x11,/*X*/0x11,0x11,0x0A,0x04,0x0A,0x11,0x11,
/*Y*/0x11,0x11,0x0A,0x04,0x04,0x04,0x04,/*Z*/0x1F,0x01,0x02,0x04,0x08,0x10,0x1F,
/*sp*/0,0,0,0,0,0,0,                     /*.*/0,0,0,0,0,0x0C,0x0C,
/*!*/0x04,0x04,0x04,0x04,0x04,0,0x04,    /*:*/0,0x0C,0x0C,0,0x0C,0x0C,0,
/*-*/0,0,0,0x1F,0,0,0,                   /*/*/0x01,0x02,0x02,0x04,0x08,0x08,0x10,
/*(*/0x02,0x04,0x08,0x08,0x08,0x04,0x02,/*)*/0x08,0x04,0x02,0x02,0x02,0x04,0x08,
/*+*/0,0x04,0x04,0x1F,0x04,0x04,0,       /*=*/0,0,0x1F,0,0x1F,0,0,
/*,*/0,0,0,0,0x0C,0x04,0x08,             /*%*/0x19,0x19,0x02,0x04,0x08,0x13,0x13,
/*0*/0x0E,0x11,0x13,0x15,0x19,0x11,0x0E,/*1*/0x04,0x0C,0x04,0x04,0x04,0x04,0x0E,
/*2*/0x0E,0x11,0x01,0x06,0x08,0x10,0x1F,/*3*/0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E,
/*4*/0x02,0x06,0x0A,0x12,0x1F,0x02,0x02,/*5*/0x1F,0x10,0x10,0x1E,0x01,0x11,0x0E,
/*6*/0x06,0x08,0x10,0x1E,0x11,0x11,0x0E,/*7*/0x1F,0x01,0x02,0x04,0x08,0x08,0x08,
/*8*/0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E,/*9*/0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C,
};
static const uint8_t *glyph(char c)
{
    static const struct { char c; int off; } map[] = {
        {' ', 26*7}, {'.', 27*7}, {'!', 28*7}, {':', 29*7}, {'-', 30*7},
        {'/', 31*7}, {'(', 32*7}, {')', 33*7}, {'+', 34*7}, {'=', 35*7},
        {',', 36*7}, {'%', 37*7},
        {'0', 38*7}, {'1', 39*7}, {'2', 40*7}, {'3', 41*7}, {'4', 42*7},
        {'5', 43*7}, {'6', 44*7}, {'7', 45*7}, {'8', 46*7}, {'9', 47*7},
    };
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c >= 'A' && c <= 'Z') return F + (c - 'A') * 7;
    for (unsigned i = 0; i < sizeof(map)/sizeof(map[0]); i++)
        if (map[i].c == c) return F + map[i].off;
    return F + 26 * 7; /* 未知字符 = 空格 */
}
static uint32_t rgb(int r, int g, int b)
{ return 0xFF000000u | ((uint32_t)b<<16) | ((uint32_t)g<<8) | (uint32_t)r; }
static void rect(uint32_t *fb, int x, int y, int w, int h, uint32_t c)
{
    for (int j = y; j < y + h && j < UI_H; j++)
        for (int i = x; i < x + w && i < UI_W; i++)
            if (i >= 0 && j >= 0) fb[j*UI_W + i] = c;
}
static void draw_text(uint32_t *fb, int x, int y, const char *s, uint32_t col, int scale)
{
    for (; *s; s++) {
        if (*s == ' ') { x += 6 * scale; continue; }
        const uint8_t *g = glyph(*s);
        for (int r = 0; r < 7; r++)
            for (int b = 0; b < 5; b++)
                if (g[r] & (0x10 >> b))
                    for (int dy = 0; dy < scale; dy++)
                        for (int dx = 0; dx < scale; dx++) {
                            int px = x + b*scale + dx, py = y + r*scale + dy;
                            if (px >= 0 && px < UI_W && py >= 0 && py < UI_H) fb[py*UI_W + px] = col;
                        }
        x += 6 * scale;
    }
}

/* ---------- 资源装载 ---------- */
static uint8_t *slurp(const char *dir, const char *name, size_t *len)
{
    char p[512]; snprintf(p, sizeof(p), "%s/%s", dir, name);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)n);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    fclose(f); *len = (size_t)n; return b;
}

/* RT_ICON DIB 解码 → RGBA; 支持 1/4/8/32bpp + 调色板 + AND 掩码 */
static int decode_icon(const uint8_t *d, size_t n, rgba_t *out)
{
    if (n < 40) return -1;
    int32_t w, h2; uint16_t planes, bpp;
    memcpy(&w, d + 4, 4); memcpy(&h2, d + 8, 4);
    memcpy(&planes, d + 12, 2); memcpy(&bpp, d + 14, 2);
    (void)planes;
    int h = h2 / 2;                       /* biHeight 含 AND 掩码 */
    if (w <= 0 || h <= 0 || w > 256 || h > 256) return -1;
    const uint8_t *pal = d + 40;
    size_t pal_bytes = (bpp <= 8) ? (size_t)(1u << bpp) * 4 : 0;
    const uint8_t *img = pal + pal_bytes;
    size_t xor_row = ((size_t)w * bpp + 31) / 32 * 4;
    const uint8_t *andm = img + xor_row * (size_t)h;
    size_t and_row = ((size_t)w + 31) / 32 * 4;
    if ((size_t)(andm - d) + and_row * (size_t)h > n) return -1;
    out->w = w; out->h = h; out->px = malloc((size_t)w * h * 4);
    if (!out->px) return -1;
    for (int y = 0; y < h; y++) {
        const uint8_t *row = img + xor_row * (size_t)(h - 1 - y);   /* bottom-up */
        const uint8_t *arow = andm + and_row * (size_t)(h - 1 - y);
        for (int x = 0; x < w; x++) {
            uint32_t c = 0;
            if (bpp == 32) { memcpy(&c, row + x*4, 4); c |= 0xFF000000u; }
            else {
                unsigned idx = 0;
                if (bpp == 8) idx = row[x];
                else if (bpp == 4) idx = (x & 1) ? (row[x>>1] & 0xF) : (row[x>>1] >> 4);
                else if (bpp == 1) idx = (row[x>>3] >> (7 - (x & 7))) & 1;
                if ((size_t)idx * 4 + 3 < pal_bytes) {
                    uint32_t v; memcpy(&v, pal + idx*4, 4); c = v | 0xFF000000u; /* BGRA */
                }
            }
            int mask_bit = (arow[x>>3] >> (7 - (x & 7))) & 1;       /* AND: 1=透明 */
            out->px[y*w + x] = mask_bit ? 0x00000000u : c;
        }
    }
    return 0;
}

/* 组目录 → 选定成员的 type3 id (nID 在 14B 条目 +12; 优先第2条目) */
static int group_first_icon(const uint8_t *g, size_t n)
{
    if (n < 6 + 14) return -1;
    uint16_t count; memcpy(&count, g + 4, 2);
    if (count == 0) return -1;
    if ((size_t)(6 + count * 14) > n) count = (uint16_t)((n - 6) / 14);
    uint16_t pick = (count >= 2) ? 1 : 0;
    uint16_t id; memcpy(&id, g + 6 + pick * 14 + 12, 2);
    return id;
}

/* 对话框模板: UTF-16 串提取 + 控件类原子计数 */
static void parse_dialog(const uint8_t *d, size_t n, char *title, size_t tl,
                         int *buttons, int *edits)
{
    *buttons = *edits = 0; if (tl) title[0] = 0;
    for (size_t i = 0; i + 1 < n; i += 2) {
        uint16_t u; memcpy(&u, d + i, 2);
        if (u == 0xFFFF && i + 3 < n) {
            uint16_t cls; memcpy(&cls, d + i + 2, 2);
            if (cls == 0x0080) (*buttons)++;
            if (cls == 0x0082) (*edits)++;
        }
        size_t j = i, len = 0;
        while (j + 1 < n) { uint16_t c; memcpy(&c, d + j, 2);
            if (c >= 0x20 && c < 0x7F) { len++; j += 2; } else break; }
        if (len >= 3 && tl && !title[0]) {
            size_t k = 0;
            for (size_t q = i; q < j && k + 1 < tl; q += 2) {
                uint16_t c; memcpy(&c, d + q, 2); title[k++] = (char)c;
            }
            title[k] = 0;
        }
        if (len) i = j - 2;
    }
}

static void draw_icon(uint32_t *fb, const rgba_t *ic, int x, int y, int scale)
{
    for (int j = 0; j < ic->h; j++)
        for (int i = 0; i < ic->w; i++) {
            uint32_t c = ic->px[j*ic->w + i];
            if (!(c >> 24)) continue;     /* AND 掩码透明 */
            for (int dy = 0; dy < scale; dy++)
                for (int dx = 0; dx < scale; dx++) {
                    int px = x + i*scale + dx, py = y + j*scale + dy;
                    if (px >= 0 && px < UI_W && py >= 0 && py < UI_H) fb[py*UI_W + px] = c;
                }
        }
}

/* ---------- UI 合成 (frame 供页脚动画) ---------- */
static void ui_compose(uint32_t *fb, unsigned frame, const char *dir)
{
    for (int y = 0; y < UI_H; y++)
        for (int x = 0; x < UI_W; x++)
            fb[y*UI_W + x] = 0xFF000000u | (uint32_t)(0x20 + y/24) << 16 |
                             (uint32_t)(0x2C + y/20) << 8 | (uint32_t)(0x40 + y/14);
    rect(fb, 0, 0, UI_W, 56, rgb(16, 22, 38));
    draw_text(fb, 16, 18, "BLEEM! NATIVE PORT", rgb(255, 255, 255), 3);
    draw_text(fb, 620, 22, "X11/WAYLAND + VULKAN", rgb(120, 200, 255), 2);

    static const char *grps[] = {"type14_id1.bin","type14_id2.bin","type14_id11.bin",
        "type14_id12.bin","type14_id21.bin","type14_id31.bin","type14_id41.bin","type14_id30012.bin"};
    int ix = 24;
    for (unsigned i = 0; i < sizeof(grps)/sizeof(grps[0]) && ix < UI_W - 100; i++) {
        size_t gn; uint8_t *g = slurp(dir, grps[i], &gn);
        if (!g) continue;
        int id = group_first_icon(g, gn);
        char nm[64]; snprintf(nm, sizeof(nm), "type3_id%d.bin", id);
        size_t cn; uint8_t *c = slurp(dir, nm, &cn);
        free(g);
        if (!c) continue;
        rgba_t ic;
        if (decode_icon(c, cn, &ic) == 0) { draw_icon(fb, &ic, ix, 84, 3); free(ic.px); ix += ic.w * 3 + 18; }
        free(c);
    }

    static const int dlg_ids[] = {4001, 11001, 15001, 30004, 30008, 30009};
    for (int k = 0; k < 6; k++) {
        char nm[64]; snprintf(nm, sizeof(nm), "type5_id%d.bin", dlg_ids[k]);
        size_t n; uint8_t *d = slurp(dir, nm, &n);
        if (!d) continue;
        char title[128]; int btns = 0, edits = 0;
        parse_dialog(d, n, title, sizeof(title), &btns, &edits);
        free(d);
        int cx = 24 + (k % 3) * 312, cy = 220 + (k / 3) * 150;
        rect(fb, cx, cy, 296, 132, rgb(28, 34, 54));
        rect(fb, cx, cy, 296, 24, rgb(48, 90, 160));
        for (char *t = title; *t; t++) if (*t >= 'a' && *t <= 'z') *t -= 32;
        draw_text(fb, cx + 6, cy + 6, title, rgb(255, 255, 255), 2);
        char meta[48]; snprintf(meta, sizeof(meta), "%d BTNS %d EDITS", btns, edits);
        draw_text(fb, cx + 6, cy + 40, meta, rgb(160, 190, 230), 2);
        for (int b = 0; b < btns && b < 4; b++)
            rect(fb, cx + 10 + b * 68, cy + 96, 56, 20, rgb(70, 120, 200));
    }

    rect(fb, 0, UI_H - 40, UI_W, 40, rgb(10, 14, 26));
    draw_text(fb, 16, UI_H - 28,
              "FULL FEATURES: SOUND, MEMCARD, COLOR FMV - NO TIME LIMIT - NO DEBUGGER CHECKS",
              rgb(120, 255, 160), 2);
    if (frame & 0x10) rect(fb, UI_W - 44, UI_H - 30, 16, 16, rgb(255, 200, 60)); /* 心跳块 */
    char fnum[24]; snprintf(fnum, sizeof(fnum), "FR %06u", frame % 1000000u);
    draw_text(fb, UI_W - 160, UI_H - 28, fnum, rgb(160, 190, 230), 2);
}

/* ---------- 入口 ---------- */
extern int vk_host_show_frame(const uint32_t *fb, int w, int h);
void *vk_open(int w, int h, const char *title);
int   vk_push(void *v, const uint32_t *fb, int w, int h);
int   vk_alive(void *v);
void  vk_close(void *v);

int cmd_render_ui(int argc, char **argv)
{
    const char *out = "build/ui.ppm";
    const char *dir = "data";
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--render-ui") == 0 && i + 1 < argc) out = argv[++i];
    for (int i = 1; i < argc - 1; i++)
        if (strcmp(argv[i], "--data") == 0) dir = argv[i + 1];

    static uint32_t fb[UI_W * UI_H];
    ui_compose(fb, 0, dir);
    if (vk_host_show_frame(fb, UI_W, UI_H) != 0)
        fprintf(stderr, "WARN: window present skipped (no display?) — offscreen PPM still written\n");

    FILE *f = fopen(out, "wb");
    if (!f) { perror(out); return 1; }
    fprintf(f, "P6\n%d %d\n255\n", UI_W, UI_H);
    static uint8_t row[UI_W * 3];
    for (int y = 0; y < UI_H; y++) {
        for (int x = 0; x < UI_W; x++) {
            uint32_t c = fb[y*UI_W + x];   /* fb 行0=顶行, PPM 自顶向下 */
            row[x*3] = c & 0xFF; row[x*3+1] = (c >> 8) & 0xFF; row[x*3+2] = (c >> 16) & 0xFF;
        }
        fwrite(row, 1, sizeof(row), f);
    }
    fclose(f);
    printf("render-ui OK -> %s (%dx%d)\n", out, UI_W, UI_H);
    return 0;
}

/* 窗口模式: 常驻交互窗口 ~30fps; ESC 或关窗退出; --seconds N 可限时 */
int cmd_window(int argc, char **argv)
{
    const char *dir = "data";
    int seconds = 0;
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--data") == 0) dir = argv[i + 1];
        if (strcmp(argv[i], "--seconds") == 0) seconds = atoi(argv[i + 1]);
    }
    static uint32_t fb[UI_W * UI_H];
    ui_compose(fb, 0, dir);
    void *v = vk_open(UI_W, UI_H, "bleem! native port - ubuntu (full features, no demo limits)");
    if (!v) { fprintf(stderr, "window init failed (no display?)\n"); return 1; }
    unsigned frame = 0;
    const double limit = seconds > 0 ? (double)seconds : 1e18;
    double elapsed = 0.0;
    while (vk_alive(v)) {
        ui_compose(fb, frame, dir);
        if (vk_push(v, fb, UI_W, UI_H) != 0) break;
        struct timespec ts = { 0, 33 * 1000 * 1000 };   /* ~30fps */
        nanosleep(&ts, NULL);
        frame++; elapsed += 0.033;
        if (elapsed >= limit) break;
    }
    vk_close(v);
    printf("window closed after %u frames\n", frame);
    return 0;
}
