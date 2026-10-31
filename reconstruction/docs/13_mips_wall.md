# 13 — H-9b MIPS 执行墙画像：SCPH1001 引导链仿真与 BIOS-JIT 最小指令集（task-020）

复现：`python3 tools/mips_wall.py /home/gill/bleem/psxbios/SCPH1001.BIN`（validator：`tools/validate_host13.sh`）。
模型：MIPS-I (R3000) 子集仿真，PSX 物理别名（ROM phys 0x1FC00000），延迟槽执行，jal 返回栈跟随。

## 一、引导链仿真结果

| 指标 | 值 |
|---|---|
| 仿真步数 | 60,000（预算内无死路） |
| 引导路径未知指令 | **0** |
| NOP（`sll $zero` 形态）占比 | 59,918/60,000 ≈ 99.86%（BIOS 热循环含大量 NOP 垫） |
| 有效指令族 | or/lui/ori/addiu/sw/lb/j/jr/bne/addi/cop1 |
| 稳态 | 进入 `0x00007b60` 递增扫描循环（RAM 清零族）——真实终止需 vsync/timer IRQ 注入 |

**JIT 激活策略推论**：整 ROM 静态解码含 ~19% 数据字（字体/串表），不可作翻译依据；
必须**块级按需翻译**（以 PC 触发、异常回退出块）——即真实 JIT 形态，与 docs/10/11 的引擎架构判定一致。

## 二、BIOS-JIT 最小指令集清单（H-10a 输入）

按合法性×引导链观测×ROM 编码面三级过滤：

1. **运算**（ROM 高频）：add/addu/sub/subu、and/or/xor/nor、slt/sltu、sll/srl/sra、
   addi/addiu、andi/ori/xori、lui
2. **访存**：lw/sw、lb/lbu、sb、sh、lhu；**lwl/lwr/swl/swr 必须以微操作字节序列展开**
   （ROM 各 ~400-760 次；R3000 无硬件非对齐——JIT 侧展开，异常路径不实现）
3. **控制流**：j/jal/jr/jalr、beq/bne/blez/bgtz（**延迟槽必须真实执行**——引导链 jal 4,185 处）
4. **协处理器（影子语义，不实现数值行为）**：COP0 mfc0/mtc0（status/cause 影子即可）、
   COP2 GTE 全族（lwc2/swc2 + 命令编码：JIT 期可按 NOP 影子处理，H-10b 后按游戏需求升级）、
   COP1（R3000 保留 → ROM 中 2,429 字为数据/字体位图，非执行体）
5. **异常面**：syscall/break 计数在案（301 字），H-10b 之前不实现 handler 链——引导链未触及

## 三、全 ROM 静态解码直方（证据存档）

有效编码族合计 ~81%；未知 24,799 字（19%）集中于字体位图与串表区（0x1FC00000+数据段），
sample 见 `mips_wall.py` scan 模式输出。此数字**不是** JIT 缺口——块级按需翻译天然规避。

## 四、对 H-10a/H-10b 的工程约束

- JIT 生成目标 = x86-32，运行期直接写入宿主映射的 PSX 窗（0xBFC00000 RW→EXEC 两段式，参照 host32 生产宿主）
- 寄存器映射建议：MIPS r0..r31 → 宿主侧 128B 数组（首版不钉物理寄存器；性能优化留后）
- 内存桥 = 2MB RAM 背板 + ROM 别名家（phys() 函数已给出，逐字照抄进 JIT）
- 终止/推进语义需要 IRQ 注入点（vsync 计数 → COP0 cause/ip 位）——列入 H-10b 验收
