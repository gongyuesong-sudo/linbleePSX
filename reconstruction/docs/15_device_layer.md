# 15 — 最小设备层探索（H-12a，SESSION-27）

## 已落地（host/mips_jit.c，回归 host7/14/16 全绿）
1. **设备寄存器组 v1**：`dev_regs[16K]`，0x1F800000-0x1F80FFFF 全窗 trap（lw/sw/lwl/lwr 四路）。
   - 策略：写全收；GPU/IRQ 域(0x1000-0x10FF)读寄存器值(默认 0=ready)；其余读 0xFFFFFFFF(=功能全开)。
   - `H_JITDEV=<file>` 设备访问日志；`EMITFAIL pc/op` 停机取证。
2. **全链取证**：`H_JITBLOCKS=1` → BIOS 链全部块 PC 序列（256 环）。

## warm 链 150 块全画像（复位向量起）
```
0x150→0x1D0→0x250→0x278→0x2F8 → [0x2A0↔0x320]×34   ; RAM 0-2KB 清零+默认向量(sw $0 循环, CPU 拷贝非 DMA)
→ 0x328 cop1(mtc0 SR=0 关中断, 我们 NOP-shadow) → 0x330 HW 配置读(0xA0000000 窗, phys 正确)
→ A(0)@6ec4 → 3× [0x18E0,0xDA0,0xDC0,0xE2C,0x18D0,0x1910]  ; 设备/lib 三段 init
→ rootcall cmd=1 → A(19)@2240(libetc) → cmd=2 → cmd=1
→ 0x3CF0..0x3D58 printf/stdout 路径 → jalr $t3 目标=0x68 → EMITFAIL(RAM[0x68]=0x000000ff 哨兵)
```
- 停机本质：**printf("...") → 设备链表遍历 → devsw 槽 = 0xff 哨兵("无设备")** → 跳 0x68 执行垃圾。
- RAM 0x68/0x6C/0x70 = PSX 默认 stdin/stdout/stderr device-switch 指针槽。
- 预放 RAM[0x68..70]=0xBFC0DC70 **无效**——0x2A0 复位循环在其后清零；安装者应在循环之后的某段 BIOS 代码（未走到，被更早的 open-dev 路径截断，鸡生蛋）。
- **devsw 表定位**：ROM+0xDC70 = 5 连指针组 `BFC04F48/58/68/78/88`（Init/Open/Close/Read/Write 或 +Control）。

## 设备写观测（链上真实发生的）
WR 1F801020=00031125 / 101C=00070777 / 1018=00020843 / 1014=200931E1 / 1010=0013243F / 100C=00003022 /
1008=0013243F / 1004=1F802000（= IRQ 基+TIM 模式设置，docs/13 的 0x150 init 段）；
WR 1F802040=0F00(GPU_CTR)；WR 1F801060=0B88(TIM0 mode)；1F801D80/84 read-write 轮询×4（0xB88x 设备区）。

## attempt3 建议路线（下一手）
用 DuckStation（~/Applications/duckstation-x64.AppImage，debug 符号）引导同 ROM，
读停机/首帧时刻 RAM 0x00-0x1FF 金标准与链各点 RAM 做 diff → 直接点名"我们的链少装了哪张表"；
比盲追 ROM 安装者快一个数量级。若 diff 显示 devsw 安装发生在 CD-ROM 读后（libfs 依赖）→
判定 warm 链物理上需要 CD 设备面 → task-024 验收转 C 归档定案。
