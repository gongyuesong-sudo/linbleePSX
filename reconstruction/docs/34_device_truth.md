# 34 — H-21a 设备复位真值面与 ra=4 根因（task-047，2026-10-31 SESSION-50）

## 1. 设备读序普查（H_JITDEV + 冷跳链 65536 块）
- 读风暴：`RD 0x1F801DAC ×10008` = **DMA7(GPU-FIFO) CHCR 完成位轮询**——
  未建模寄存器默认 `0xFFFFFFFF` 使 trigger/busy 位永不清零。
- 其余访问面小：DMA7 DADR/BADR(1D80/1D84)、PCR(1D8C)、MADR(1DA8)、
  MWR(1DB0/1DB4)、GPU 2040、A-TERM(1D88/1D8C)。

## 2. H_DEVRST（env 门控，默认关）
`host/mips_jit.c` dev_read：`0x1F801080..0x10FF` DMA0-7 控制块硬件复位=0
（CHCR busy/trigger=0，写后按存值回读）。旧默认 0xFFFFFFFF 属建模错误。

## 3. ra=4 根因终判（RACATCH/H_RAWATCH 双探针）
迁移链：`bfc1f694 jalr→0x32a90`(C2 状态机入口) → `0x362b0 beq 0x363B8`
(尾返回块) → `lw ra,0x14(sp); jr ra` → **ra=4** → pc=4 崩。
- C2 `0x32a90` 首指令 `lw t6,[0x80089E2C]`；`bne t6,3` 真分支跳 `0x362b0`
  尾返回面——**我们的链命中的是 state≠3 分叉**，而 Beetle 终态
  `[0x89E2C]=0x15000C`（低半字=3）走的是另一条腿。
- 金标准栈 `sp+0x14=4` 与崩值一致：该槽在参考态也是残留值——尾返回腿
  在真机此阶段不被走。
- **定性：不是缺寄存器，是 C1↔C2 状态机时序分叉**——C1 推进状态依赖
  CDROM 命令 ack/DMA 完成真值。设备复位真值(H_DEVRST)只消 DMA 风暴，
  不改分叉。达 Execute! 的正路 = task-048 CDROM 命令 ack/时序真值 +
  task-049 DMA 完成/中断语义。

## 4. 判据复现
`bash tools/validate_host39.sh` — 断言：H_DEVRST 在场(0x1080..0x10FF 复位 0)、
设备普查日志可读(0x1F801DAC 风暴记录在案)、RACATCH 迁移探针在场、
docs/34 三结论落档、默认语义无回归(基线链 18021±)。
