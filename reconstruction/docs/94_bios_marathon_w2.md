# 94 — BIOS 战役楔2：勘正——"10M 步健康 idle"实为无盘空程序 nop 漂流；下一墙=CD-ROM 设备面（task-113，SESSION-124）

## 1. 勘正（docs/93 的"正常 idle 环"判定不成立）
`--bios-run` 升级分段直方图 + 落点 dump + 反汇编判读后，3M 步真相：
- **分段**：`rom=74,089(25%) ramlo=225,695(75%)` —— BIOS 走通了**真 ROM 初始化流**
  （`bfc00070→…→bfc07xxx` 系，GPU 端口 `a000b068` 探测、rootcall stub `rfe` 返回、
  栈 `801ffe00` 建立——楔1 的 rfe/RAM 折叠两修真值价值**仍然成立**）；
- 但此后 BIOS 因 **CD-ROM 端口全 0 → 判无盘 → `A(0)Boot` 跳空程序(0x0)** →
  在 RAM 0x240-0xFFFF 的**全零 nop 海**漂流 3M 步。"零异常"≠"活着"——
  这是无盘 PSX 的真实语义落点（真机黄屏等待即此阶段），但对本战役，
  **下一道墙由此钉死：CD-ROM 设备面**。
- 附带发现：step28 `jal 0xBFC00150` 落 nop——BIOS 极早期有一拍走 RAM 镜像
  等待序（金标准 `RAM 0x150=0x6ee0` 即此值域）；未影响后续 ROM 主流。

## 2. 战役方法论升级（本楔固化的反指标）
**"零异常长跑"单独不构成健康证明**——必须配 ①分段 pc 直方图（ROM/RAM 低段/
RAM 主段/设备段）②落点指令流 dump+判读 ③"跳转→目标零区"侦测（注意 jr+
nop 槽假阳性，须测 target 而非 slot）。已并入 `--bios-run` 工具面
（`BIOSRUN_PCHIST/BIOSRUN_DUMP` env + 分段/LOST 常开）。

## 3. 楔3 路线（钉死，勿再漂移）
BIOS 进入"有盘引导"需要 **CD-ROM 控制器寄存器组**（PSX 地图：
0x1F801C00-0x1F801C3F——读数据/命令/状态/seek 四端口系；本引擎设备窗
alloc 0x9400 已覆盖该域，当前全部走 psx_dev 默认面(读 0/写弃)）。
**但端口清单以 BIOS 真码访问序列为准，不凭教科书**——楔3 第一步=
MMIO trace 钩子跑 BIOS 轨迹，提取其实际轮询/命令端口序列，按序列补
CDROM 应答语义（无盘应答 vs 挂 ISO 只读应答两态；ISO 字节不入 git
约束仍生效）。SPU(0x1F801500)/DMA 控制块(0x1F8010B0)预计同批按需补。
GTE 真机位对拍、lwl/swr 等同旧候选一起排在 CDROM 之后。

## 4. 回归与工具
四面 selftest 31/14/8/7 双态零回归；`--bios-run` 输出新增
`BISO_SEG`/`BISO_LOST` 面；`BIOSRUN_DUMP` 可导出 RAM 低段运行态供反汇编。
