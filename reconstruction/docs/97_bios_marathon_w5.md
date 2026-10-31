# 97 — BIOS 战役楔5：REGIMM 真 bug 修 + cache 维护写弃实验 = BIOS 无盘 idle 主循环会聚（task-116，SESSION-127）

## 1. 本楔两记真交付
### (1) REGIMM 真 bug（引擎第 5 类字段级错误，BIOS 实战暴露）
- 现场：修复 A(24) 墙后 BIOS 推进到 step 86162，`bltz $4,+N`（ROM 0x30C8）
  被本引擎判 reserved→异常。根因：**旧实现把操作数放 rt、子操作放 rs——
  恰好与 MIPS 正字相反**（正字：`bltz rs,off`，子操作编码在 rt 字段：
  0=bltz 1=bgez 16/17=bltzal/bgezal 20/21=reserved）。
- **自测覆盖盲区实证**：cpu-selftest 从未测过任何 REGIMM 编码——旧实现
  "全绿"运行了五张楔。修后补 P7b 两断言（标准编码 bltz 落点+bltzal ra 挂），
  cpu 面 14→16。教训入铁律：**分支族指令必须按编码正字测，不测=没实现**。

### (2) cache 维护写弃实验（BIOSRUN_CMEXP，实验开关默认 OFF）
- 楔4 悬案终判：BIOS 的 [0,0xF80) **16B 步距写 0 循环 = D-cache 维护特征**
  （256 线×16B=4KB index 空间全扫；PSX D-cache index=地址低 12 位）。
  真机该写不改 RAM 语义（至少不毁向量表）；无盘冷 boot 下 golden RAM
  [0xB0]=lui/addiu/beq stub 存活 = 铁证。
- 实现 = pc∈[0xBFC01940,0xBFC01A40]（该 handler 码区）且目标 RAM off<0x1000
  的 store → **写落 blackhole**。
- **实现坑（新教训）：`mem_rw` 只交还指针，写在调用方——patch 写弃必须
  `*host=&blackhole` 重定向，"return 不写"完全无效**（首轮 cm_skip 计数
  正常但 RAM 照毁 = 假生效）。
- 手算第 3 坑入账：`0xBFC0193C | 0xA0000000 = 0xBFC0193C`（kseg0 已含
  kseg1 位 29——BIOS tramp 的 or 对 ROM 基地址是幂等 no-op，jr 回槽自入
  = 合法设计）——gdb/心算都骗了我，**or_probe 微程序才是判官**。

## 2. 战果（数量级推进）
| 态 | 200k 步 | 20M 步 |
|---|---|---|
| 旧（默认） | rom=74k → nop 漂流 JUMPZERO×2 | —（同一漂移） |
| CMEXP | 无 JUMPZERO | **rom=2.73M + ramlo=16.8M + other=443k(设备窗活跃)** exc=88736 |
- 异常全部 = ROM 0xD964 `syscall`（cause=20 EXL 置位，195 步周期 = 事件
  循环节拍）→ rfe 回环，**线性缩放 = 稳定循环非崩溃**。
- 终点像 v0/ra/sp 全部落在 BIOS 栈/码区 = **无盘 idle 事件主循环会聚**
  （真机无盘终态 = 黑菜单/CD 播放器等待，本引擎达到"活循环"等价面）。
- **楔3 负结果勘正**：CMEXP 态下 BIOS 到达 CDROM SetLocation 序列区
  （docs/95"不触 CDROM"结论仅在漂移态成立）——勘正以索引注记形式入账。

## 3. 边界与楔6 方向
- CMEXP 保持**实验开关**（默认 OFF=历史面零回归）；转正条件=论证 general
  cache 语义（写-through/write-back 分型）或至少"维护循环 pc 窗"的更多 BIOS
  样本验证——**楔6-a** 候选：syscall 195 周期链语义精判（handler 走到哪、
  GPU 应答 a000b068 后 CDROM 命令是否发出）→ 决定 CDROM 应答补否。
- **楔6-b** 候选：CMEXP 转正 + D-cache 简化模型（index 空间读写分离）。
- 门链第 5 次精确串复发（host106 B cpu=14 串）已阈值化——铁律再验证。
