/*
 * psx_gpu.h —— 清洁房战役楔5: GPU v1 软件渲染 (task-132)
 *
 * 零版权自研实现; 命令几何按公开文献 psx-spx 位定义:
 *   GP0(0x02) 填充: word=color(RGB8@23-0), p0=x(9-0)|y(25-16), p1=w(9-0 步16)|h(25-16)
 *   GP0(0x03) CPU→VRAM: p0=目标(x9-0|y<<16), p1=宽|高<<16, 数据=半字对低先经 GP0 口
 *   GP0(0x20/0x24) 平涂/Gouraud 三顶点; (0x28/0x30) 8/16 裸精灵(纹里页=x4-0,y6-5)
 *   GP0(0xE1..0xE6) 环境 24-bit 值在命令字
 *   GP1: 00 复位(含模式) 02/03 显示区界 04 FB 原点 05/06 显隐 07/08 ack
 * 帧节拍: gpu_tick()=VRAM→RGBA 扫描(显示区尺寸) → vk_host present 消费
 * (docs/102 三判的"扫描消费"语义在软件面自持, 不依赖 guest 0x80030000 镜像)。
 */
#ifndef PSX_GPU_H
#define PSX_GPU_H

#include <stdint.h>

typedef struct psx_gpu_s {
    uint16_t vram[1024 * 512];
    /* 环境 */
    uint32_t tex_page;      /* E1 */
    uint32_t tex_window;    /* E2 */
    int32_t  draw_off_x, draw_off_y;   /* E5 (11-bit 有符号) */
    uint32_t mask_set;      /* E6 */
    /* 显示 */
    uint32_t disp_x0, disp_y0, disp_x1, disp_y1;   /* GP1 03/02 */
    uint32_t fb_x, fb_y;                            /* GP1 04 */
    int      disp_on;
    int      mode_24bpp;                            /* GP1(00) bit23 */
    /* 传输态 */
    int      tx_active;
    uint32_t tx_x, tx_y, tx_w, tx_h, tx_cnt;
    int      tx_half_pending; uint16_t tx_hold;
    /* 观测面 */
    uint32_t gp0_words, gp1_words, fill_px, xfer_px, tri_px, spr_px;
    uint32_t frames;
    int      raw_mode;            /* E1 位25: 原始纹理(数据流内联) —— v1 契约登记 */
    void    *hw_ctx;              /* Vulkan 硬件扫描通道上下文 (vk_host*) */
    int    (*hw_present)(void *ctx, struct psx_gpu_s *g);
    /* 帧缓冲 */
    uint32_t *fb;           /* RGBA8888, fb_w*fb_h */
    int fb_w, fb_h;
} psx_gpu_t;

int  psx_gpu_init(psx_gpu_t *g);
void psx_gpu_free(psx_gpu_t *g);
void psx_gpu_gp0(psx_gpu_t *g, uint32_t word);   /* GP0 命令/数据口 (0x1F801800) */
void psx_gpu_gp1(psx_gpu_t *g, uint32_t word);   /* GP1 控制口 (0x1F801804) */
void psx_gpu_tick(psx_gpu_t *g);                 /* VRAM→fb 一帧 */
/* 硬件扫描通道附着 (present 回调在 tick 尾部消费 VRAM 16bpp) */
void psx_gpu_attach_hw(struct psx_gpu_s *g, void *ctx, int (*present)(void *, struct psx_gpu_s *));
/* 子命令入口: 0=PASS */
int  cmd_gpu_selftest(int argc, char **argv);
int  cmd_psx_window(int argc, char **argv);      /* --psx-window: 冷启 HBEXE 出画 */

#endif /* PSX_GPU_H */
