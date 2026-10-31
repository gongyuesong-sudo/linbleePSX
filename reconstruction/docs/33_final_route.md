# 33 — H 系列终局评估与路线决定（task-046，2026-10-31 SESSION-49）

## 1. 七道墙（全部实证，非推测）
| # | 任务 | 墙 | 证据 |
|---|------|----|------|
| 1 | task-024 attempt1-3 | BIOS-JIT 调用面推不动引擎深段：深段不调用 BIOS MIPS 函数 | deep=4/max=004002cf 不变；docs/17 |
| 2 | task-027 | syscall 异常分发器真执行后引擎 deep 仍不变 | docs/18 §5, validate_host20 FAIL-terminal |
| 3 | task-031 | 真机终态 RAM 金标准消费后深段仍同墙（img x86 模板 ABI/设备面） | docs/23 |
| 4 | task-040 attempt1 | 冷启动自动运行止于 rootcall NewRootCmd 派发面（C2 从盘装载，C1 覆盖不到 root CB） | docs/31, validate_host34 |
| 5 | task-043 attempt1 | C2 根槽可装（链 18021→22772, handler 146 次）但 NewRootCmd 需结构 CB，单字句柄被拒循环重派 | docs/32 §3 |
| 6 | task-044 attempt1 | 终态 C2 切片消费（链 →65536, 首入 C2 callee）仍不达 Execute!：深段命中零槽 ra=4 | docs/32 §5, validate_host36 |
| 7 | task-045 attempt1 | 装载期快照假设证伪：t=5/9/16/32/60s 五时点 RAM 同态，五片重放同界 final=00000000；ra=4 源于 C2 从设备寄存器面读到与 Beetle 真值不符的时序量走错分支 | docs/32 §6 |

**终判**：PSX 冷跳链达 Execute! 的唯一剩余钥匙 = **完整设备驱动面**
（GPU/CDROM/DMA/时序寄存器真值，量级≈重写 Beetle core）。与项目原始目标
（demo v1.4 原生移植）正交——该目标已由 `port/` 达成（见 §3）。

## 2. 两案成本对比
**A. 归档收束（推荐）**
- 成本：文档/镜像标注半小时级；零代码风险。
- 保全成果：port 原生可运行（X11+Wayland+Vulkan）；三流解密金标准；
  BIOS 全像 RE（REA 79 符号/A(0..43)=BIOS libc/异常分发器/rootcall 桩表）；
  块级 MIPS-JIT（REGIMM/MULT/DIV/HI-LO 全，TICK 30M 块零失败回归）；
  金标准管线（SRZIP 采样/解析/重放）；homebrew PS-X EXE 工具链 +
  20036 块端到端；CDROM/C2 HLE 命令环闭环；rootcall/C2 装载面终判。
- 游戏内容运行：DuckStation（既定路线）。

**B. 继续（完整设备面仿真）← 已采纳**
- 成本：GPU 软/硬光栅、CDROM 全盘时序、DMA、Timings、OTC/中断语义——
  人月量级新子项目；且 C2 为 BIOS 版权码，全行为对拍只能闭卷猜测+金标准
  差分，调试回路极长。
- 收益：仅达"冷跳链 Execute!"里程碑（DuckStation 已免费提供同等能力）。

## 3. port 线状态实证（显示需求：X11+Wayland+Vulkan，用户要求）
- 能力横幅（`bleem_port --help` 现场输出）：`display: X11 + Wayland backends,
  Vulkan presentation`；`gpu_accel=vulkan:1`；demo 限制全解除。
- 现场复证（SESSION-49）：`--selftest --driver wayland` 在真 Wayland 合成器
  （wayland-0）**PASS (frames=3)**；x11 面因当前会话为 Wayland 型桌面
  （无 X 授权 cookie）不可达，历史窗口曾在 X 桌面 PASS（final_check 全绿）。
- 本判定不取消 port 线任何显示需求；X11 回归需在 X 桌面会话内复跑。

## 4. 决定（提请用户）
- [ ] A. 归档收束(用户已否决 2026-10-31)：linbleePSX README 标注最终状态，H 系列冻结，游戏运行走 DuckStation。
- [x] B. 继续设备面仿真（**已采纳** 2026-10-31）：H-21 = task-047(设备复位真值)/048(CDROM)/049(GPU/DMA+达 Execute!)。
- 推荐 **A**。执行归档需用户明示（镜像标注属对外动作）。
