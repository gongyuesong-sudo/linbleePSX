# 45 — H-24 终局评估：设备契约栈规模定量与关门判据（task-059 attempt3，SESSION-68）

## 1. 契约面定量（本次普查，validator F 项复算）
C2 全区（golden 切片 0x30000..0x8FFFF）根call桩族：
- vec=0xA0：21 桩 / 21 不同 cmd；vec=0xB0：26 桩 / 26 cmd；vec=0xC0：3 桩 / 3 cmd。
- **引擎↔BIOS 命令契约全集 = 44 个 cmd、50 个桩点、3 个向量族**（docs/41 的 26 窗是冷链实际到达面，全集 44）。

## 2. 达 Execute! 所需完整面（历轮终判合并）
| 层 | 内容 | 证据锚 |
|---|---|---|
| L1 命令契约 | 44 cmd 语义+帧 ABI | docs/41/43 |
| L2 线程调度 | C2 CreateThread/SwitchThread、≥8 独立栈窗、epilogue ra 合法化 | docs/36 §6、43 |
| L3 设备真值 | GPU 命令环/STAT/GPUREAD、DMA 8 通道、CDROM 机构状态机 | docs/34/38、44 §4 |
| L4 过程依赖 | 引擎对象/栈字段由真执行 device open/init 序列生成，静态注入不可达 | docs/44 §4 四路终证 |

关键事实：**BIOS 侧 handler 不缺**（ROM 全实现且冷链可执行）；缺的是 **BLEEM 引擎侧驱动层**
对 L3 的初始化真值——即要跑游戏需自研引擎兼容层，而非补 BIOS。

## 3. 成本与风险
- 36 会话 H 系列已证伪 11 个单点假设（注入/节拍/状态词/回灌/窗压全 census 零变化）。
- L2+L3+L4 合计量级为**重写级**（多周），且每步唯一 oracle 是 census 增量，回退风险高。

## 4. 终判与建议
- **H-24 关门**（task-057✓ 058✓ 059 3/3）：知识提取目标已达成（契约全集/帧 ABI/线程栈窗/过程依赖定性全部定量落档）。
- 游戏运行路线维持 **DuckStation**（AGENTS 既定）。
- 若将来立项 H-25（引擎兼容层），本档 §2 表为任务分解基线；需用户显式批准，成本预期以周计。

## 5. 资产清单（本轮可复核）
- 机制面：H_TFR(线程回灌)/H_C1PATCH(码+栈窗注入)/H_DEVGPU(GPU HLE 雏形)/H_EVW(状态字生产者)/H_FRAMES(帧捕获)。
- 文档面：docs/36→45 H 系列完整排除链；validator host41→49 锚矩阵。
