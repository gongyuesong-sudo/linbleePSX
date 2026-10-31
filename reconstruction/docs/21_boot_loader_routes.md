# 21 — C2/root2 装载面判异与路线分叉（H-13b，SESSION-33）

## 1. 实证（本轮）
- warm 链 RAM 0x150 头（root2ptr/event/stack）**全程只被写 0**（H_WATCH=150:170 STR 日志，
  写者 = bfc00250/bfc002a0 清零环）；分发器 StartRootThread 读到 0 → `jal 0x32E24` 常量形态
  进零海（docs/19 复核一致）。
- ROM 0x0 复位 6-dword = `lui v0,0x8003 / ori v0,0x243F / lui at,0x1F80 / sw at,0x1010 / j 0x150 / ...`
  = root0 内核初始化；**不执行任何 C2→RAM 复制**。
- SCPH1001.BIN = 512KB 全窗（0xBFC00000-0xBFC7FFFF），**不存在 0x82E24 偏移源**；
  "掩膜 ROM 硬件直拷 RAM 0x30000" 假设**证伪**——真机装载必走 DMA/资源区机制（ROM 数据区 0x49000+）。
- warm 链 IO 足迹（4199 块）：仅 GPU 控制寄存器 0x1F801008-0x1F801020 + RAM size 0x1060 +
  0x1D80/0x1D84/0x2040；**无 DMA(0x1080+)、无 GPU DATA(0x1000) 写** = 链死于装载开始前，
  不是装载机制模拟不全。

## 2. 外部参照（PsxEmu，源码已存 .analysis/hle_bios_reference.cpp，2128 行）
- PsxEmu HLE = **纯平行内核**：自己汇编 ROM trap 桩 + C++ 实现全部内核函数，**从不执行 Sony 码**；
  boot=instant（无动画）；kernel RAM 图与 SCPH-1001 逐地址一致（向量/100h/200h/5C4/C80h/DCB/FCB/ExCB/EvCB/PCB/TCB）。
- 其 `boot_runner bios/SCPH1001.BIN --dis 80000C80:200` 可跑真 BIOS 并导 RAM 反汇编
  = **潜在真机 RAM 终态金标准提取器**（需编译其 Core）。

## 3. 路线分叉（task-030 attempt2+ 决策表）
| 路 | 内容 | 代价 | 证据依赖 |
|---|---|---|---|
| a | 补真 boot 面：DMA 引擎+GPU OTZ+VRAM+F6 读回，跑完 Sony boot 全流程 | 最大（半台 PSX） | 无需外部金标准，但周期长 |
| b | HLE instant boot：按 PsxEmu RAM 图自建 kernel 终态+设备注册表，跳过 Sony boot | 中 | 需真机 RAM dump 对齐（boot_runner 或物理机）|
| c | 收束：docs/09-21 归档，游戏运行= DuckStation | 零 | 已具备 |

**当前判定**：warm 链机制面（异常/分发/BIOS 码执行）已全通；剩余 = boot 数据面。
建议 attempt2 走 **b**（HLE 终态预放），金标准 = 自研 dump（DuckStation GUI 手动跑 boot_runner 等价物）
或后续移植 PsxEmu 图；a 作为远期。
