/*
 * psx_cpu.h —— PSX 薄引擎楔3: MIPS R3000A 整数核 (task-109)
 *
 * 语义基线 = rewrite/host/mips_jit.c 时代已定案坑位:
 *   - 延迟槽必执行 (branch_pending 两段式)
 *   - gpr[0] 恒 0 (写弃)
 *   - REGIMM 含 bltzal/bgezal 链接
 *   - 小端 load/store 对齐强制 (非对齐=地址错异常 tcode4/5)
 * 范围: 整数指令全集 + 最小 cop0(mfc0/mtc0/eret, status/cause/epc/badv) +
 *       syscall/break/地址错/保留 四类异常。GTE(cop1)/RC 计数/LWC 半字对
 *       留楔4 (触发保留异常 excode10)。
 * 内存一律经 psx_translate (fixed 态=原址恒等)。
 */
#ifndef PSX_CPU_H
#define PSX_CPU_H

#include "psx_core.h"
#include <stdint.h>

typedef struct psx_cpu_s {
    uint32_t gpr[32];
    uint32_t hi, lo;
    uint32_t pc, npc;
    uint32_t cop0[16];   /* 12=status(IE=bit0 EXL=bit1 BEV=bit22 真值) 13=cause 14=epc 8=badv */
    uint32_t gte[32];    /* 楔5 GTE 寄存器堆(位布局=引擎自定义面, 真机对拍留后) */
    uint32_t cycles;
    uint32_t exc_n;
    uint32_t syscall_code;   /* task-136: 最近 syscall 指令 code 位段 */      /* 异常进入计数 */
    int      branch_pending;
    uint32_t branch_target;
    const psx_core_t *core;
    /* 清洁房楔2 (task-129): HLE rootcall 钩 —— 返回 1=已服务(当普通指令续跑),
     * 0/NULL=走原 syscall 异常路径 (BIOS 装载态保持既有语义) */
    int (*on_rootcall)(struct psx_cpu_s *c);
    void *hle;           /* psx_hle_t* 不透明柄 */
} psx_cpu_t;

extern int psx_cm_enabled;   /* 楔7b: cache 维护写弃开关(默认 1) */
void psx_cpu_reset(psx_cpu_t *c, const psx_core_t *core); /* pc=0xBFC00000 复位向量 */
int  psx_cpu_step(psx_cpu_t *c);      /* 执行一条(含延迟槽簿记); 0=ok */
uint32_t psx_cpu_run(psx_cpu_t *c, uint32_t nsteps); /* 跑 n 步, 返回实步数 */
/* 楔6d: 金标准引导表装载——把 golden RAM 0x400-0x1800 区间的
 * "非零且指向 BIOS ROM 窗"的指针灌入本引擎 RAM 同址(BLEEM 宿主职责的
 * port 对等; golden 字节不入库, 派生自运行期读取)。返回灌入字数。 */
int  psx_load_golden_vectors(psx_core_t *c, const uint8_t *golden_ram);
/* main.c 子命令入口 (--cpu-selftest): 0=PASS */
int  cmd_cpu_selftest(int argc, char **argv);

#endif /* PSX_CPU_H */
