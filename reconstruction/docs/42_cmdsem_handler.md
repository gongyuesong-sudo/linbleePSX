# 42 — H-23b attempt3(终)：cmd 语义 handler 已运转但墙不动（task-052，2026-10-31）

## 1 交付机制（全部 env 门控，默认关=基线逐位不变）
- `H_CMDSEM`：mailbox（RAM `0x97F80..0x97F8C`，宿主 cmd_mb[8]）+ RAM `0x94000` MIPS trampoline
  （`lui k0,0x8009; ori k0,0x7F80; sw a0/a1/a2/t1; lw v0,0x10(k0); jr ra`）+ `jh_sw/jh_lw` 钩 +
  `cmdsem_service()`：**NewRootCmd(0x00)=RAM 0xA0000 CB 竞技场**、open/close/read/write(fd 计数)、
  printf(0x12→2)、**InitializeMemory(0x20)=真 memset 实现**、0x120/0xC0/0x1C。
- 派发面：懒填槽由 blanket-ack(0x95000) 升级为 trampoline（仅当槽为 0/越界，不覆盖已装真值）。
- 踩坑记录：mailbox 初放 `0x802A0200` 被 phys() 2MB 回绕折叠到 RAM 0xA0200（scratch 页不在
  RAM 视图内）——迁 0x97F80 后通路即通。

## 2 运行实证与判读
- `CMDSEM` 日志 4 事件：`cmd=0x20 InitMem`、`cmd=0x120 Mode2 ×3`，ra=bfc07014（C1 包装器 A(19) 路）。
- **参数面新事实**：a0-a2 全为 ASCII 残值（`"Initialize memory"` 串碎片）——该族服务参数**不走
  a0-a2 寄存器约定，而在调用者栈帧**；mail 捕获的是残存器。真实语义服务需读帧（每 cmd 帧布局
  各异）→ 语义面从"16 cmd 表"膨胀为"16 cmd × 帧 ABI 逆向"。
- 墙界完全不变（EMITFAIL@0x1014 / ra=4 / UNIQ 462<474 因 12 个 ack 块被 trampoline 块替换）：
  被拦的 4 个 cmd 位于停机点**之后**的因果流，对 halt 无杠杆。

## 3 终判（task-052 3/3 永久 failed）
H-23b 的最小集假设**否证**：可运行的按-cmd handler（含真 InitMem/NewRootCmd）不推进墙界。
根判收敛（与 docs/38 三排除合取）：达 `Execute!` 需要的是**完整 kernel+C2 双向 ABI**
（寄存器+栈帧+CB 生命周期+线程调度），任何截断式最小子集均为惰性。该工程量=重写 BIOS 宿主
契约层，超出逆向知识工程目标。**H-23 家族关门**；task-053 依 [DEPENDENCY] 传播终态。
可续能资产：`H_CMDSEM` 服务框架（帧读取扩展点=cmdsem_service 内按 cmd 取 `psx_state.r[29]+off`）、
docs/41 语义表、26 调用窗。游戏运行维持 DuckStation 路线。
