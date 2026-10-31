# 35 — H-21b CDROM/C2SEC 状态面取证（task-048 attempt1，2026-10-31 SESSION-51）

## 1. C2SEC 来源定论（修正 docs/34 假设）
- **SCPH1001 ROM 自带 C2SEC 镜像**：C1 拷贝器(RAM 0x50000，`jal 0x16a34` 例程，
  src=ROM+0x18000 len=0x67FF0 → RAM 0x30000) 与参考态逐词比对 **653/106492 差
  (0.6%)** —— 差集 = C2 init 完成后的状态词，含 `[0x89E2C]=0x160004`
  (主相 0x16=22, 辅相=4)。不存在"盘载 C2SEC"缺口。
- 交付物：`c2_state_patch.bin`(653 词状态差集) + `golden_c2_slice_low_500_900.bin`
  (低区派发表片, 装出真机 `[0x674]=0x1508` 等 C2 内核相对句柄) —— 仅存
  .analysis，不入 git。

## 2. C2 根状态协议面破译
- `[0x89E2C]` 全镜像仅 **2 个静态写者**：`0x316C8`(`jr ra; sw a0,0x9e2c`
  尾写 setter，ROM 侧调用者 bfc777b0) 与 `0x31D50`(条件迁移)。
- 状态处理头 `0x32a90`：`lw t6,[0x9e2c]; bne t6,3 → 远腿`；
  **`sw ra,sp,0x14` 是 bne 延迟槽——两腿都存 ra**(修正"未存 ra"猜疑)。
- ROM(C1) 有 32 处 `jal 0x32a90` 调用者；崩点 = 远腿深段尾返回簇
  `0x362b0 beq 0x363b8; 0x363b8 lw ra,0x14(sp); jr ra`，其帧 sp+0x14
  **无人写过 = 金标准栈残留 4** → pc=4。

## 3. 相1 实验（H_C2STATE=3 强写）不生效
拷贝段(425K 迭代)内已完成崩链；覆盖发生在 0x600 派发位但 0x32a90 首访
发生在拷贝腿已锁定路径后——RACATCH oldra 现场与对照完全一致。
说明崩相**不由该状态词单点决定**，由拷贝期 C2 主线程未被真正唤醒决定
（C2main 的事件循环需要 CDROM ack/TIMERS 中断节拍才能推进到 setter）。

## 4. attempt1 判定与交班
墙未破（量化：链 65536/边界 ra=4 不变）。但根因收敛为单一类：
**C2 主线程节拍（CDROM 命令 ack 环 + TIMERS/vblank 中断流）缺失** ——
正是 task-049 的定义域。新增证据锚：
- `H_WATCH 80089e20..38`：链上 4 次写全为拷贝副产物(ROM 垃圾)，无协议写。
- `H_WATCH 801FFDE0..801FE00`：sp+0x14 零写入（残留 4 实锤）。
attempt2/3 方向：把 CDHLE(docs/29) 命令环接入 C2 拷后主循环唤醒 +
TIMERS 0x1F801100 通道节拍注入 IRQ2，替代金标准态消费。
