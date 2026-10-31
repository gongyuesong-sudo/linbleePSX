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

typedef struct {
    uint32_t gpr[32];
    uint32_t hi, lo;
    uint32_t pc, npc;
    uint32_t cop0[16];   /* 12=status 13=cause 14=epc 8=badvaddr */
    uint32_t cycles;
    uint32_t exc_n;      /* 异常进入计数 */
    int      branch_pending;
    uint32_t branch_target;
    const psx_core_t *core;
} psx_cpu_t;

void psx_cpu_reset(psx_cpu_t *c, const psx_core_t *core); /* pc=0xBFC00000 复位向量 */
int  psx_cpu_step(psx_cpu_t *c);      /* 执行一条(含延迟槽簿记); 0=ok */
uint32_t psx_cpu_run(psx_cpu_t *c, uint32_t nsteps); /* 跑 n 步, 返回实步数 */
/* main.c 子命令入口 (--cpu-selftest): 0=PASS */
int  cmd_cpu_selftest(int argc, char **argv);

#endif /* PSX_CPU_H */
