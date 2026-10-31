# 85 — x86 战线楔10：native 金标准通道复验 PASS + boot32 回归坑——x86 战役具备收官条件（task-102，SESSION-111）

## 1. 解包链终验（native 通道复跑，三流位级全 PASS）
`tools/native_unpack/build_selftest.sh` 现场复跑：
```
stream 0xfacc  PASS: 2,979B 位级
stream 0x1015c PASS: 4,327B 位级
selftest PASS: 466,111B 位级 vs native golden
```
三流解压=宿主启动链的心脏，**真算法在 native 构建上逐字节复现金标准**——
docs/76-79 启动链(交权/块表/解包)的**终验闭环**。

## 2. boot32 回归坑（.analysis 无版本控制之祸，落账）
现 `/home/gill/bleem/.analysis/boot2.c` 重跑：`[boot] slot=1007ffb4 delta=0`
（注释金语义应 delta=0x400000）→ SEGV eip=0x400004（落回 MZ 头）。
**boot32 金时刻版本不可恢复**（.analysis 非 git；boot2.c 被后续实验改写）。
教训升级：**金标准 harness 源码本身必须入 git 或至少带 sha 基线**——
`build_selftest.sh`(入 rewrite git) 正是因此才是可信通道；一次性取证 harness 会腐烂。

## 3. 解包目的位图（docs/76/79/81 已有账，此处汇总定版）
| 块 | src(压缩) | dst(视图坐标) | 解压态 | 身份 |
|---|---|---|---|---|
| s1 | 0xfab7 | 0x8e8(+0x60 引导) | 2,979B | 薄皮模块(表 0xe) |
| s2 | 0x1014f | 0xe18 | 4,327B | 薄皮模块(表 0x9e) |
| s3 | 0x10a4e | 0x5a244+**bit0=call** | 466,111B | **宿主引擎本体**(init 0x10A4E 内含映射构造器) |
| 明文区 | (不压) | 0x55338-0x55DF4 | 2,748B | 二段引导器(位引擎, docs/06 零漂移) |
交权行 `add $0xfa00+call *esi` → base+0xFA00 stub(`lea edi,[esi+0xc];ret` 置模块表基)——三段式全链每环节均有独立证据(位级 PASS/指令锚/串链)。

## 4. 三路线边界总账（x86 战役收官条件）
- **静态**：docs/83 三判据穷尽——主循环 call*reg×37 动态面失明；除此之外启动链/块表/
  服务表 ABI/位引擎/映射构造器全破。
- **Wine**：docs/84 win16 七 API 屏蔽后进程存活，但 COM 稳态+ptrace_scope 双墙。
- **native 重放**：解包链位级终验 PASS（本楔）；主循环体依赖 DX/OLE 运行时，
  重放路线不覆盖（且 boot32 基线已腐烂，重建=新战役）。
主循环最后 20% 语义已由 A3 混合模型+服务表投影(docs/67-75/81)给出等价物。
**判定：x86 战线已达成"可理解、可验证"目标；继续推进=月级新战役(DX/OLE 全栈重放)，
收益/成本比极低——具备收官条件，待终判封线审计(楔11)。**
