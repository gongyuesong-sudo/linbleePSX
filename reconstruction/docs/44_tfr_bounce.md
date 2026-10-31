# 44 — H-24b 线程回灌 H_TFR（task-058，SESSION-66）

## 机制
`H_TFR=1`：JIT 动态 jr/jalr（pend_mode=4 终止形）前插 host-call `jh_tfr(tgt,jpc)`；
当目标**不可取指**且未达 `H_TFR_CAP` 时：`ra=jpc+8`、`sp=0x801FFE00`（分发栈重置）、
`EPC=0x80030000`、`Cause.ExcCode=AdEL(5)`、`pc=BIOS 异常分发器`（[0x130] 表→0x1E988 回退，
H_C2HOME 下 h==0 忠实回退与 EXN 同语义）。

## 实证（/tmp/h66v3.log 复现锚）
1. TFR#1 `bad=ffffffff jpc=bfc01a80 sp=8025d7e8` —— 首次捕获活线程上下文的坏返回并回灌成功。
2. 链首次进入 **BIOS 异常分发器真执行** `bfc1e988/e9a0/ea00`（此前 H_BIOSEXN 只走 syscall 面）。
3. 分发器 rfe→EPC 后链进入 **真 C2 kernel 代码 `b0032e24`**（golden RAM 同址=真代码，docs/21 锚点）——H 系列首次到达该面。
4. 随后 LOOP-DETECT 在 `0x32e24`（`lui v0,0x8008; lw v0,0x9f68; jr ra` = 状态词 getter）：
   调用方 C2main 事件循环对 rfe 回灌 `0x80030000` 的固定节拍形成等事件自旋，65536 块饱和。
5. v1 教训：裸回灌 `pc=0x80030000` 进向量页数据漫步（0xa0/0x120 步长 0x80）至 host SEGV；必须走分发器路线。

## 终判（attempt1）
H_TFR 把墙从“boot 栈残值死路”推进为“分发器→真 C2 kernel→等事件自旋”。剩余钥匙 = C2main 事件循环
等待的具体事件词/回调完成（[0x89f68]=0x1b004b 等 golden 值是 init 中期语境，非终态哨兵），
属 task-059 契约实现面：需要按 getter 语义供事件而非改 EPC 节拍。

## §2 SESSION-66 取证补充（task-059 attempt1）
- `0x5a8d0` 现场 = **C2 直发第二桩族**：`addiu t2,zero,0xA0; jr t2; addiu t1,cmd`，cmd∈{0x72,0x39,0x2f}，
  纯 `jr` 非 jal；静态 `jal->0x5a8d0/0x32e24` 均 0 命中（经 0xA0 向量面派发，非直接 jal）。
- `LOOP-DETECT @0x32e24` 非 C 码环：getter 本身无环；真身 = **TFR 回灌 rfe→0x80030000 固定 EPC
  的每异常一跳活锁**，65536=H_MAXB 块上限饱和。回灌语义需带"进展判定"：连续 N 次 bounce 无新 UNIQ
  即改 EPC 为 C2main 循环体出口或供被等事件。

## §3 task-059 attempt1 终判（SESSION-66）
- `0x80089F68` 定样为**引擎状态字**：C2 内 9 个直接访问点，含推进路径 `v0=word+1; sw`（0x347ac）与
  多处清零/条件写——真机由**设备事件回调**置值；冷链无生产者 ⇒ getter 恒 0 ⇒ caller 轮询环 ⇒
  LOOP-DETECT @0x32e24。H_SPINADV 覆写 jh_lw **无效**（getter 块走 JIT 内联访存，不经 host jh_lw）。
- bounce 后进 C2main 的顺序执行最终仍落**同一既有墙**（数据词当码执行）或等字环——H_TFR 把"死路"
  变"环"，但未变"进展"。
- **终判**：达到 Execute! 的必需前置 = `0x9F68/0x9F70/0x9F78` 引擎状态字的**事件生产者**
  （GPU/vblank 回调族 + CD 事件族，docs/41 cmd 语义 {0x0C,0x1C} 相邻面），属契约实现核心，
  与 H-21c 已证伪的"裸 IRQ 电平注入"不同：需要的是**回调写词**而非 IP2 电平。

## NEXT（attempt2 方案）
1. 找真机写者：金标 RAM `0x9F68` 区域回溯（BIOS rootcall cmd 0x0C/0x1C handler 静态反汇编, ROM 0x2D3xx 族）。
2. H_EVW：宿主按 vblank 节拍对 `0x9F68` 供给推进序列 {0,1,2,0}（写 RAM 镜像后 jit_flush 不适用——数据词非码块, 直写即可被内联 lw 读到）。
3. 判据：LOOP-DETECT 消失且 UNIQ census 增量 > 0（新块面）。

## 已废方案
- ~~同 jpc 连击≤3 停回灌~~（环不在 TFR 计数上, 在 caller 轮询）。
rfe 后 EPC 固定回灌 0x80030000 造成节拍环 → 改 EPC 为 C2main 循环体后分支入口
（先用 H_JITUNIQ 频次找 loop head 与其出边），或拦 `0x32e24` getter 记录调用方 pc 集，
确定被等事件词后按 docs/41 cmd 语义供给。
