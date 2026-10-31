# 19 — root2 线程入口与内核 RAM 镜像装载面判异（H-13a，SESSION-32）

## 1. 停机真身：jal 0x32E24 = root2 线程入口（ROM 直跳指令，非指针表）
分发器区 6 处 `jal 0x32E24`（24-bit 目标 0xCB89）：
`0xBFC1E7B0, 0xBFC1E8E8, 0xBFC1EA00, 0xBFC1EBA4, 0xBFC20394, 0xBFC206E8`。
- MIPS jal 目标 = (PC&0xF0000000)|idx<<2：从 KSEG1 ROM 执行 → **0xB0032E24**；
  PSX RAM 片选只看低位（2MB 镜像），0xB0032E24/0x80032E24/0x32E24 = **同一 RAM 页**（phys 解码已对）。
- 真机该入口由 **root2 程序**占据：nocash 规范 RAM 头 `0x80000150 root2ptr`、
  `0x154 root2 event`、`0x158 root1 stack`；本链停机 = RAM 0x32E24 起零海滑行
  （BLK 尾 `b1524→b1ca4` 线性 0x80 步 = NOP 海，`0xB1D24` 只是海尾垃圾指令，不是调用表）。

## 2. 装载面证据
- `FUN_bfc00420` = 唯一发现的复制循环：ROM 0xBFC10000 → RAM 0x500，长 0x8BF0（尾 `jr 0x500` 入内核）；
  覆盖异常处理器 RAM 0xC80、分发器 5C4/5E0/600、DCB 6EE0、FCB 8648 —— **不覆盖 0x32E24**。
- 复制块内 `0xBFC1418C/0xBFC14190 = 0x3CE00/0x32E00` 相邻值对 = 线程栈/入口表
  （root2 栈 0x32E00 边界与入口 0x32E24 同页）⇒ 内核镜像自带 root2 布局常量。
- root2 程序体 = **C2 数据区镜像**（boot 动画期经 WriteGPU/DMA 装入 RAM 0x3xxxx 起），
  ROM GPU 包 926 个（0xAxxxxxxx 头），动画装载函数在 `FUN_bfc02ed8` 族（model_probe 门）。
- ROM+0x130 = BIOS 版本 ASCII 串（" 3000/10/31"）；RAM 0x130（0xFFFE0130 窗）早期=handler 入口、
  后期内核 table-of-tables —— 两阶段复用，勿混淆。

## 3. H_ROOT2 实验（机制定案，非最终修复）
- 预置 RAM 0x32E24 stub（`addiu v0,1; jr ra`）+ RAM 0x150 root2 头 → 停机点从 `b00b1d24`
  推进到 `b00b3718`（零海更深）：证明 **JIT/异常语义正确，缺的是 C2 装载数据面**
  （root2 程序体 + 0xBxxxx 内核扩展区，单一 stub 不够）。
- 定案：warm 链全通 = 需仿真 C2 装载（DMA/WriteGPU 泵送 ROM 资源→RAM 0x3xxxx/0xBxxxx）
  或 HLE 直装内核完整 RAM 镜像 —— 即 PsxEmu 式 boot 仿真层，属设备/装载面工程（task-030+）。

## 4. 复验
- `bash tools/validate_host22.sh`：文档事实断言 + `H_ROOT2` 停机深度 > 基线（机制生效证据）。
