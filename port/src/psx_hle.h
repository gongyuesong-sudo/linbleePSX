/*
 * psx_hle.h —— 清洁房战役楔2: HLE 内核 v1 (task-129)
 *
 * 零 BIOS 纪律: 不装载、不执行、不复制 SCPH 任何字节。本内核 = 自研 C 实现,
 * 命令号 ABI 逐槽登记公开来源 (docs/109 §2):
 *   [S] = PSn00bSDK 公开桩表 (MPL, 数值为事实非表达)
 *   [R] = PSXRecompStudio 验证集 (A0:39/3C/3E/AB/AC, B0:4E/50/56/57)
 *   [D] = 本仓库 docs/47 ROM 静态锚 (A(40) 0x4000 分块拷贝形态)
 *   [P] = PCGUIX/PSYQ 公开文献通用表 (低锚, 标注之)
 * 未登记槽 = 未实现 (v0=-1 + last_unknown 记账), 绝不臆造号。
 *
 * 派发协议 = 真机 LIBSN 序列 (原样兼容, 因游戏侧代码烧在自己的 EXE 里):
 *   li $t1,cmd ; li $t2,0xA0|0xB0|0xC0 ; jr $t2 ; (延迟槽) ; 该地址处 syscall
 *   裸 syscall a0=1/2 = Enter/ExitCriticalSection [S _syscalls.s]
 * 参数移位 ABI: 内核 handler 视 a0..a2=实参0..2, 更多自栈 sp+0x10。
 * 返回: v0 = 结果, pc = 返回到 syscall 后一条 (HLE 无 eret)。
 */
#ifndef PSX_HLE_H
#define PSX_HLE_H

#include "psx_cpu.h"
#include <stdint.h>

#define HLE_A_SLOTS 0xC0u
#define HLE_B_SLOTS 0x60u   /* B 表止于 0x5D (PSXRecomp 注) */
#define HLE_C_SLOTS 0x20u   /* C 族公开号 0x00-0x1F */
#define HLE_EV_MAX  64u     /* OpenEvent 槽 */

struct psx_hle;
typedef uint32_t (*hle_a_fn)(struct psx_hle *h, uint32_t cmd);
typedef struct psx_hle {
    psx_cpu_t  *cpu;
    psx_core_t *core;
    /* 派发账 (selftest + 观测面) */
    uint32_t nlog;         /* 已服务 rootcall 总数 */
    uint32_t last_vec, last_cmd;
    int      last_unknown; /* 最近未实现号 (vec<<8|cmd), 无=-1 */
    /* 堆 (b_InitHeap 域) */
    uint32_t heap_base, heap_size, heap_brk;
    int      heap_ready;
    /* 事件环 v1 槽 */
    struct { uint32_t class, vector; int enabled, fired; } ev[HLE_EV_MAX];
    /* Execute(first) 武装态 (task-131 接线主循环) */
    uint32_t exe_pc, exe_gp, exe_sp; int armed;
    /* CONFIG 镜像 (SetConf/GetConf) */
    uint32_t config[4];
    hle_a_fn a[HLE_A_SLOTS];
    hle_a_fn b[HLE_B_SLOTS];
    hle_a_fn c[HLE_C_SLOTS];
} psx_hle_t;

/* 安装: 建表 + RAM 向量页 trampoline (0x80/0xA0/0xB0/0xC0 = syscall) + CPU 钩。 */
int  psx_hle_install(psx_hle_t *h, psx_cpu_t *cpu, psx_core_t *core);
void psx_hle_uninstall(psx_hle_t *h);
/* CPU on_rootcall 钩实现: 1=已服务 0=走原异常路径 */
int  psx_hle_rootcall(psx_cpu_t *cpu);
/* 内部堆 API (LoadImage 等复用; EvHeader=公开 PSX 堆头 5 word 布局) */
uint32_t hle_heap_alloc(psx_hle_t *h, uint32_t size);   /* 返回 EvHeader* (guest) */
int      hle_heap_free(psx_hle_t *h, uint32_t elem);
/* PSX-EXE 装载器 (LoadImage 内核面): hdr_gpa=PSX-EXE 头(含0x800镜像) dest=0 时用头内 vaddr */
int  hle_load_exe(psx_hle_t *h, uint32_t hdr_gpa, uint32_t dest,
                  uint32_t *pc_out, uint32_t *gp_out);
/* main.c 子命令入口 (--hle-selftest): 0=PASS */
int  cmd_hle_selftest(int argc, char **argv);

#endif /* PSX_HLE_H */
