# 92 — PE 战线楔5（收官）：中断自动进入链闭环 + GTE 最小面 + 真 BIOS 2000 步烟测（task-111，SESSION-122）

## 1. 本楔交付（薄引擎五楔完成）
- **中断自动进入链**：`irq_check` 置于 step 头（报告点=下条未执行指令，
  eret 后重执行它——正确硬件时序）；条件 `status.IE && !EXL &&
  I_STAT&I_MASK&0x1FFF`；cause.IP8=bit8、ExCode=0、向量 BEV 双址
  (0x80000000/0xBFC00000)。**EVTGEN→vblank→handler 清位→eret→续跑**
  全链由 guest 代码闭环实证（cnt=1/cause=0x100/reads=66）。
- **status 位真值修正**：楔3 把 EXL 误用 bit0（PSX 真值 **IE=bit0,
  EXL=bit1, BEV=bit22**）——楔3 selftest"自洽但非真值"教训固化；
  eret 改清 bit1，p5 断言同步。
- **GTE 最小面**：mfc2/mtc2/cfc2/ctc2 寄存器 ABI + SFTRGB + AVZ3；
  命令识别=rs0 且 rd==0&&sa==0（与 mfc2 $0,$0 的硬件性编码冲突归命令）。
  **位布局（IRGB/OTZ/控制字节方位）为本引擎定义面，真机对拍需 GTE
  寄存器金标准——留后续楔**（诚实边界，不冒充 r3001 全文）。
- **服务表全量**：svc 槽 16→**37**（docs/81 动态面计数全量）+ 36 号
  空槽守卫 + 字节偏移 ABI 到末槽。
- **真 BIOS 2000 步烟测**：SCPH1001 自复位跑 2000 步**无崩溃**，
  `cycles=2000 pc=0x80001e5c exc=1`——BIOS 自检段执行后经未实现面异常
  走 vec 落入 RAM 码继续漂移；port 首次长跑真 BIOS 的实证记录。

## 2. 现场测试缺陷（3 枚，全记录）
1. 轮询环 beq 回边的**延迟槽落在环外 nop**（P+36）——周期=4 非 3，
   步数预算 230→280；"分支回边 slot 位置"是合成测试的系统性考点。
2. mfc0 $10,$13 编码写成 0x400A7000（rd=14）——rd 域=13<<11=0x6800；
   cop0 rd 编码是本项目第二次踩（楔3 p5 首次），已进铁律。
3. ctc2 rd>7 写低字节 vs SFTRGB 读 gte[28] 高 8 位——控制字节方位
   自洽性在本引擎定义面内统一（rd>7=低 8 位；SFTRGB 的 sf code 读
   高 8 位为定义面约定，docs 声明对拍留后）。

## 3. 薄引擎五楔总账
| 楔 | 交付 | 门 | 断言 |
|---|---|---|---|
| 1 | 骨架(状态块/翻译/服务表) | host98 | 17 |
| 2 | fixed 原址三窗+BIOS 装载 | host100 | +14=31 |
| 3 | MIPS 解释核+真首指令 | host101 | 14 |
| 4 | 设备堆+联动+rootcall 契约 | host102 | 8 |
| 5 | 中断链+GTE 最小面+svc37+BIOS 烟测 | host103 | 7 |
合计 **60 断言 × 无ROM/带ROM 双态**；显示面 final_check 每楔回归绿。
后续面（若用户加楔）：GTE 真机位对拍（需金标准）、lwl/swr、RTPS 透视、
DMA 信道、多线程帧链、MAP_FIXED 与 port 段共存矩阵、CPU 速度面。
