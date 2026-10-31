# 102 — BIOS 战役楔8b-①：CDROM 寄存器面落地 + 等待点三判（task-121，SESSION-132）

## 1. 交付一：psx_dev CDROM 驱动器最小面（实现，dev 面 10 断言）
- 寄存器正字（no$psx 对表）：`0x1F801820-23` CDREAD 四字节数据出口
  （读消费清零）；`0x1820` 写 = 命令口（同时 ack CDINT）；`0x1824` CDSTAT
  （bit7=出口有货四字节或、恒 0x18=drive ready 系）；`0x1825` CDSEL 回读 0；
  `0x1826/27` 写弃观测面。
- 命令 state machine 第一档：任意命令 → 无盘标准四参
  `[0x11,0x14,0xC0,0x42]`（no$psx GetStat 无盘响应）+ CDINT（I_STAT bit2）
  置位；ACK/Data 两档合一（诚实登记，trace 驱动再细分）。
- `psx_dev_init` 上电复位 ACK：I_STAT bit2 预置（真机 = 驱动板上电
  ~百 ms 自发 RESET ACK）。
- 断言 `cdrom_poweron_ack_bit2` + `cdrom_getstat_nodisc_F1`（新建独立
  dev 块验证 init，避开主 dev 被前置用例改写——首轮即踩此坑入账）。

## 2. 交付二：等待点三判（推翻本楔开工前提的两条硬证据）
1. **BIOS idle 态全局关中断**：主序 `0x6894 sw I_MASK=0 + 0x6898 sw I_STAT=0`
   （watch 实抓 step 80959/80960）——**CDINT0 即使置位也不被接受**；
   docs/101"等 CDINT0 复位 ACK"假说**证伪**。
2. **195 步周期本体判读**（TR 2696400-2696596 全窗）：`0x19B0-0x1A34`
   = **sw $0 全字清零循环，以 128B 步距扫 [0x80030000, 0x80200000)**
   ——每周期把 1.9MB 系区清一遍 + 事件队列 drain（jalr 0x8003xxxx）
   ——**这是无盘黑屏的"每帧缓冲清零扫描"= 画面输出的 CPU 侧预备流**；
   GPU 消费端（0x1800 命令口/GPU DMA 上送）未实现 = 画面永远画不出。
   （该循环 pc 窗恰在 CMEXP 保护窗内——cm_skip 线性增长互证。）
3. **CDROM 查询在 idle 态零发生**（BIOS 不读 0x1820）：CDROM 面是
   **带盘路径的前置资产**，不是 idle 推进钥匙——验收链 a) 修正为
   "命令面存在断言（dev 10 断言）"而非"BIOS 主动读"。

## 3. 楔8b-② 定稿（呈现面正主）
GPU 消费端三件：①0x1F801800 GP0 命令口解析（dev 已有 gp0_n 记录面
雏形——D8 断言在账）②0x1F801810/1814 STATUS/ORDER 真值扩展 ③帧缓冲
[0x80030000]扫描→VRAM/显示输出的消费模型（或 GPU DMA 通道）。
验收 = GPU 端口/GPU DMA 写流出现 + 帧节拍可观测（黑屏画面=合法终态）。

## 4. 方法论入账
- 第三版等待点判定的两条推翻性证据都来自**写值 watch**（I_MASK=0）与
  **周期全窗 trace**——"等待 X"类假说必须先证伪"BIOS 是否启用 X 通道"
  （中断使能位/寄存器读取者），已入铁律候选。
