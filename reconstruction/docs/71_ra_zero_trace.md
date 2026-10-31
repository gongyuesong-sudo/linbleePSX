# 71 — A3 楔5：ra 归零全链捕获——僵尸环根因 = 借来的死栈帧（task-087，SESSION-96）

## 1. `H_SVCT` 首跳 trace（决定性 24 跳序列）
开桥主运行，低页命中全量 trace：
```
SVCT1 pc=0xB0 ra=8005A314 t9=0   ← rootcall/向量 stub 页, 第 1 次服务, thunk 合法返回
SVCT2 pc=0xB0 ra=8005A338 t9=0   ← 第 2 次服务, 合法返回
SVCT3 pc=0x00 ra=00000000 t9=80079D00 v0=8005A844 sp=800DFE90→FEE0  ← 转折跳
SVCT4+ 0→80→100 三角环(ra=0 恒定)
```
**SVCT3 定性**：sp 从 `0xFE90` 切到 `0xFEE0`（楔4 栈窗）后，服务表路径
`t9=80079D00`（中断服务表）函数序言从 **0xFEE0 栈帧弹 saved-ra = 0**
（楔4 dump 实证该窗 ra 槽真值 0）→ `jr ra` → nop sled → 僵尸三角。

## 2. 根因判定（战役级）
**MAINRUN 的起点不是真实现场**：CPU 初值用 fwd 默认态，而非 golden 快照的 CPU 寄存器
（ra/sp/pc/gpr 全 32 项在 `disc.state` CPU 段，sha ee2cc76a）。借来的 C2 主线程栈帧
是**终态死帧**（saved-ra 槽=0 = snapshot 时该协程未持有活返回链）。
→ 楔2"跑穿"与楔5"合法两跳"都建立在错误起点上；**修正起点 = 楔6**。

## 3. 旁证与排除
- `H_POKE`（启动期 RAM 写钩，能力沉淀）：0x89D44-54 状态词毒值/恢复双实验
  **零影响** → 排除"状态词未初始化致 0xFFFF 索引"假说（t9=0xFFFF 是未注册服务号
  哨兵本身，来自 0xE10 thunk→0xB0 stub 的正常派发）。
- 服务表函数 [1] `0x8005A844` = `0x800EFF50 + a0*4` 索引比较族（与控制台/中断状态
  表 0x800EFFEE0 族同构）——纯查询函数，无副作用，**语义核可低成本 HLE**。
- `RAM[0xB0]` stub 页仅 3 非零词（rootcall stub 基础页, docs/41 一脉）。

## 4. 楔6 定靶（真起点）
从 golden state CPU 段读 32 寄存器 + PC + HI/LO，`FWDMAIN` 起步装载（`H_CPUGOLD`）：
预期 SVCT1-2 形态改变、SVCT3 死帧跳消失或后移——**首次以真实现场姿态主运行**。
次级备胎（若 state CPU 段不可解析）：从 htrace CPU 采样管线（docs/24）取。
