/*
 * psx_core.h —— PSX 薄引擎骨架 (task-106 楔1)
 *
 * 施工图 = 本仓库逆向定案件:
 *   docs/82 —— s3 头部 PSX 内存映射构造器 (状态块槽位 + 0x328400/0x9400/
 *              -0xBFC00000/-0x1F800000/+0x1F800400 平移常数)
 *   docs/81 —— 0x4022a4 服务分派表 ABI ("表基址+索引 call", 0x55459 型)
 *
 * 楔1 范围 = 状态块构造 + 地址翻译算术 + 分派表 roundtrip + selftest。
 * 不含: CPU/GPU/CD 仿真、MAP_FIXED 直置原址、BIOS 镜像装载 (后续楔)。
 */
#ifndef PSX_CORE_H
#define PSX_CORE_H

#include <stdint.h>

/* 状态块槽位 (docs/82 构造器直写域, 偏移与原件一致) */
#define PSXSB_GPU_BASE    0x24u /* = scratch + 0x400 (GPU 端口族 0x1F800400 基) */
#define PSXSB_SCRATCH     0x28u /* MMIO 寄存器堆基 (scratch pool)          */
#define PSXSB_MMIO_DELTA  0x2Cu /* = scratch - 0x1F800000 (MMIO 翻译常数)   */
#define PSXSB_RAM         0x44u /* 主 RAM 基 (alloc 0x328400)               */
#define PSXSB_ROM_DELTA   0x48u /* = RAM - 0xBFC00000 (BIOS 窗平移常数)     */
#define PSXSB_SIZE        0x70u /* 状态块槽区 (0x00-0x6C 全落账)            */

#define PSX_GPA_RAM_BASE   0x80000000u
#define PSX_GPA_ROM_BASE   0xBFC00000u
#define PSX_GPA_MMIO_BASE  0x1F800000u
#define PSX_RAM_ALLOCSZ    0x328400u /* docs/82 构造器 imm (≈3.2MB)          */
#define PSX_MMIO_ALLOCSZ   0x9400u   /* 0x9400 池 (七连 0xC00 子窗)          */
#define PSX_MMIO_SUBWIN    0xC00u

/* 翻译窗口 id */
enum { PSX_WIN_RAM = 0, PSX_WIN_ROM = 1, PSX_WIN_MMIO = 2, PSX_WIN_BAD = 3 };

#define PSX_SVC_SLOTS 16

typedef struct {
    uint8_t  *ram;                 /* PSX_RAM_ALLOCSZ  */
    uint8_t  *mmio;                /* PSX_MMIO_ALLOCSZ */
    uint32_t  sb[PSXSB_SIZE / 4];  /* 状态块 (槽值=原件平移语义) */
    void     *svc[PSX_SVC_SLOTS];  /* docs/81 服务分派表 */
    uint32_t  svc_nlog;            /* 分派调用日志计数 */
    uint32_t  svc_last;            /* 最近一次 svc id  */
} psx_core_t;

int   psx_core_init(psx_core_t *c);
void  psx_core_free(psx_core_t *c);
/* guest 物理地址 → 宿主指针; *win 输出窗口; 越界返回 NULL + PSX_WIN_BAD */
void *psx_translate(const psx_core_t *c, uint32_t gpa, uint32_t *win);
/* docs/81 型索引分派: call(svc[idx]) 带两参, 记日志 */
uint32_t psx_svc_call(psx_core_t *c, uint32_t idx, uint32_t a, uint32_t b);
/* main.c 子命令入口 (--psx-selftest): 0=PASS */
int   cmd_psx_selftest(int argc, char **argv);

#endif /* PSX_CORE_H */
