# 91 — PE 战线楔4：设备寄存器堆 + CPU×设备联动 + rootcall 契约链（task-110，SESSION-121）

## 1. 本楔交付
`port/src/psx_dev.{h,c}` + CPU ld/st 语义钩 + `--dev-selftest`（8 断言双态 PASS）：
- **设备真值面直接平移本仓库 H 战役已定案资产**（docs/57/62/63 的语义表，
  不是教科书泛型）：I_STAT **写 AND 清**、EVTGEN 每 64 次 I_STAT 读翻转置
  vblank 位、GPU STATUS 读含 0xB0…（GPUOK 真值）+ send-ready 位、
  Timer 四通道**读推进 0x400 且 16 位回绕**（docs/63 测速实证常数）、
  C2 私有窗 0x1044 写 + 0x104A **双相位 bit0/bit1 握手状态机**（docs/62）。
- **CPU×设备联动**：`psx_translate` 命中 MMIO 窗且 `core->dev` 在位 →
  guest sw/lw 走 `psx_dev_read/write`（lb 符号扩/对齐检查/地址错异常全部
  保持楔3 语义）；RAM/BIOS 窗仍裸指针零开销。
- **GPU GP0 FIFO 深 4**（push 满丢弃/pop 顺序）+ GP1 暂存。
- **rootcall 契约链（docs/41 地址面）**：guest 程序 `0x600 dispatch 块 →
  0x674 表 → handler` 全链由解释核执行，handler 内 `sw→I_MASK` 实证
  **CPU×RAM×设备三面联动**；返回经 jr $31+延迟槽回主程序。

## 2. 现场测试缺陷（4 枚，全记录）
1. GPUOK 断言自相矛盾：`0xB0000000` 高字节与"bit24=1"不能同时用
   0xFF000000 掩码断言——send-ready 恰在 byte2，改分段掩码。
2. `sw $2,0x1074($0)` 忘 MMIO 基址——落到 RAM 别名 0x1074（正说明
   fixed 态翻译严格性），必 `lui $3,0x1F80` 做基。
3. rootcall 步数差一拍（返回槽执行完 npc==target 非 target+4）。
4. （楔3 遗留记录在 docs/90 §2。）

## 3. 已定案边界
Timer 读推进=**M1 测速模型**（docs/63 实证常数，非点时钟自由跑）；
1044/104A 为最小握手状态机，**字节级对拍真机 = 楔5**；
GTE(cop2) 未接（syscall 面 rootcall 用 guest 表实现，未做异常内自动派发）；
中断触发→BIOS handler 自动进入链（I_STAT&I_MASK→vector 0x80000000）= 楔5。

## 4. 回归
`--psx-selftest` 31 + `--cpu-selftest` 14 双态零回归（ld/st 重构等价性由
楔3 全套断言实证）；host102 七判含 host101/host100 门抽验 + final_check。
