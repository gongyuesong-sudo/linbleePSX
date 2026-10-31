# 29 — H-18a CDROM 驱动 HLE v1 + JIT 正确性三勘误（task-039, SESSION-42/43）

## 1. 静态判定：BIOS CDROM 寄存器面
- 全 ROM 直接扫描 `lui 0x1F80` + `lb/lh/lw/sw` 命中 CDROM 窗 `0x1800..0x1840`：**命中 0**。
- 结论：CDROM 寄存器访问全部由 C2 设备驱动（RAM 装载体）经 `jr $t9`/驱动表间接进行；
  ROM 静态面无直接寄存器面。→ CD HLE 必须以 **C2 体替换/命令环** 为切口。

## 2. C2 命令环 HLE v1（自研体，docs/19 root2 装载面的合法替身）
- 生成器 `tools/hlecd_build.py` → `host/hlecd_code.h`（31 词）。装载：
  - 体 @RAM `0x80040000`（物理 0x40000）；stub @RAM `0x32E24`（root2/C2 入口劫持）跳体。
  - 命令块 `0x802A0100` → **phys 2MB 回绕 = 物理 0xA0100（真机同义解码）**；
    应答块 `[k0+0x100]` = 物理 0xA0200。勘误：`+0x40` 落分发器进入时清零的首 scratch 块内，
    必须放同块高位（+0x100）。
- 命令语义（v1）：`0x1001`=READ：把 SECDATA(`0x80041000`←host 供数, `H_CDHLE_EXE` 直供
  第一方 EXE 头 2048B / `H_CDHLE_DISC` 读 2352 盘 LBA16) 512 字拷至 DST(`0x80050000`)，
  写 ack=0x1001、清命令、回 outer；其他命令=接受并清；命令=0 → `v0=1; jr ra`。
- 验证模式 `CD=1`（golden_cpu_probe）：GSTATE 金标准终态 + install + 直跳 C2 体，
  ROM break 哨兵 `0xBFC4C1B0` 干净停机。实测（validate_host31 C 段）：
  `cmd=00000000 ack2=00001001 dst0=582d5350("PS-X") v0=1 fails=0 blocks≈1030`。

### 生成码汇编勘误（本次踩全）
1. **MIPS-I 无 `seq`**：SPECIAL funct `0x2A`=slt、`0x2B`=sltu、`0x27`=nor、`0x23`=subu。
   自研汇编器把 `seq` 编成 funct42 → 实际 slt（有符号小于），回边条件恒真。
   修正=倒计数寄存器 + `beq s5,z0` 判零（或 slt/sltu/or 组合判等）。
2. **分支必须有延迟槽**：`beq/bne` 后补 `nop`；JIT `EMITFAIL branch-in-slot` 是正确拒绝。
   回边方向：完成路径必须 **fall-through 经过 ack/clear**，不能用 taken 跳过应答写入
   （`bne s5,z0 →L` 回边，`nt` 顺落 ack）。
3. **命令块地址受 2MB 回绕**：宿主 install 直写 `psx_ram+0x2A0100` 是幻影（无掩码越界写），
   JIT `phys()` 真实读点 = `0x0A0100`。安装/断言一律用物理回绕地址。

## 3. JIT 正确性三勘误（历史最大洞，影响全部旧结论）
1. **R-type 双寄存器 ALU 自操作 bug**：`add/sub/and/or/xor/nor/slt/sltu` 生成码
   `eax=[rs]; op eax,[rs]`（EDX 未切到 rt）= `rs OP rs`。修正 = 每个 mem-op 前
   `set_edx_r(rt)`。单测曾以机器码 `0x01223021`(注释 addu t1,t2；rt 字段实为 v0) 恰好
   与 bug 互相掩盖——已修正机器码并保留断言 0x2468。
2. **分支 flag 槽与 `ra` 同址**：flag 用 `ST+124` = `r[31]` 偏移！每个块内 beq/bne/blez/bgtz
   写 `[ST+124],AL` = **clobber ra 低字节**（停机 ra=…01 奇数、返回地址被 +0/+1 的根源）。
   修正 = `PsxState` 新增 `bflag` 成员（ST+140），flag 读写全迁移。
   **连带：旧 TICK“空闲唤醒闭环”、部分旧停机面是 ra-bug 伪像，需在新基线上重判（task-041）。**
3. **inc.h 漂移**：probe 用 `host/mips_jit_inc.h`（.c 剥离 `#ifdef JIT_MAIN` 版），历史靠
   手工 append 掩盖不同步。固化 `tools/gen_inc.py`：改 `mips_jit.c` 必跑。

## 4. 新基线下的真停机面（→ task-041 重锚）
- BIOS smoke（host14）：blocks=18021 → `pc=3c15bd80` emitfail（金标准外零区 sled 爬升）。
- 冷链 warm 链：halt `bfc00434`，v0=`a000b068`：轮询 phys 0xB068 等 RAM flag（DMA/中断写入方）
  = 真机依赖面，非翻译错误。
- 历史 validator host14/21/23/28 的停机面数字在 bug 语义下成立，新基线待重锚（task-041）；
  host19 EXN 链、host26 注入设备面在新基线仍 PASS。

## 5. 边界
- C2 体直调闭环 ≠ 冷启动自动运行端到端（调度器真发 READ → Execute!）= task-040/H-18b。
- CDROM 硬件寄存器 HLE（0x1F801800-3F）未做：v1 走命令环替身即可满足读首扇区语义。
