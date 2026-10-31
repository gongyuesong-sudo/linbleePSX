# 90 — PE 战线楔3：MIPS R3000A 解释核入 port——延迟槽/异常语义可跑可验（task-109，SESSION-120）

## 1. 本楔交付
`port/src/psx_cpu.{h,c}` + `bleem_port --cpu-selftest`（14 断言双态 PASS）：
- **整数指令面**：SPECIAL 全集（移位含 v 族/jr/jalr/movz/movn/mf*hi/lo/mult(u)/
  div(u)/算术/逻辑/slt）+ 主 op 立即数族 + 分支六族 + jal/jr 链 + COP0
  （mfc0/mtc0/eret，status/cause/epc/badv 四寄存器）。
- **访存**：lb/lbu/lh/lhu/lw/lwu/sb/sh/sw 全宽度经 `psx_translate`（fixed 态=
  原址恒等），对齐强制→地址错异常 tcode4/5+vaddr；cache 按 nop（解释核安全面）。
- **延迟槽**=两段式：分支置 pending → 下一条（槽）执行完 npc 直指 target。
- **异常**：syscall(8)/break(9)/地址错(4/5)/保留(10) 进 `enter_exception`
  （epc/cause/status.EXL/BEV 双向量 0x80000080|0xBFC00000）。
- **真 BIOS 取指**：BLEEM_BIOS 装载态下自 0xBFC00000 译码执行首指令
  `3c080013`（lui t0,0x13）→ gpr8=0x00130000，pc→0xBFC00004——
  **port 第一次执行 SCPH1001 真码**。

## 2. 合成套件当场揪出的 4 个 bug（自验证价值实证）
1. **核 bug·槽后双跳环**：延迟槽执行完"续挂 pending"会把 target 再当槽消费
   → 目标地址无限重执行；正解=槽执行完 `npc=target` 一次性指向。
2. **核 bug·lb 符号扩展**：`SIGN16(零扩字节)` 永不翻负；必须 `(int8_t)` 直扩。
3. 测试编码笔误：mfc0/mtc0 $14 的 rd 域=0x7000（写成 $12=status，
   handler 反复读 status 并把 epc 写成 5——EV 打印三步定位）。
4. 测试期望错：multu 7×(2³²−5)=0x6_FFFFFDD（lo=0xDD 非 0xE5）；
   div −5/7 余=−5（hi=0xFFFFFFFB 非 −1）；且 div 段覆写 hi/lo，
   mult 断言必须在下一步之前（时序敏感断言拆两段 run）。

## 3. 已定案边界（docs 内记录在案）
add/sub 不做溢出 trap（按 addu）；eret/syscall 中断当拍延迟槽簿记；
div0 取 hi=0/lo=0xFFFFFFFF 保守值；GTE(cop1/cop2/LWC2)、lwl/lwr/swl/swr、
cache 真语义、BIOS rootcall 协议面 = **楔4**（设备+GTE+服务表）。
本核=正确性优先的解释器；速度/JIT 化不在薄引擎路线。

## 4. 回归
`--psx-selftest` 31 断言（楔1/2）不受影响；host101 七判含 host100/host98
回归 + final_check + 金标准恒 + ROM 不入 git。
