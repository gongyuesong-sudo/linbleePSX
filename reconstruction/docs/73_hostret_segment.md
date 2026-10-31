# 73 — A3 楔7：混合执行模型双证与首个 rc=0 完成段（task-089，SESSION-98）

## 1. 递减者观测（H_RAMDUMP 新能力）
真起点无 POKE 主运行 20M 块后 `RDUMP 00083c58=00000002`——**纹丝不动**。
结合 docs/72（golden=2 边界死锁、POKE=0 即过）：**0x8008_3C58 的递减者不在 guest 码流里**
——它是宿主/设备侧完成计数的镜像（guest 只轮询，递减=交还宿主执行的后果）。

## 2. `H_HOSTRET`：宿主镜像段语义化（本楔核心）
解剖定谳：`ra=0x044F4C02` 连 8MB 扩展 RAM 都不覆盖 = **BLEEM 混合执行模型的宿主
trampoline 地址**（guest 寄存器持有 host 侧返回点；MainRAM 段实测 2MB，scratch 紧随，
无 4MB+ 空间——排除"dump 截断"解释）。
新能力 `H_HOSTRET`：pc 落入 `0x044x_xxxx` 段 → 判"交还宿主"，**kind=0 段自然完成**。

## 3. 首个完成段（里程碑，含诚实边界）
`H_CPUGOLD + POKE(83c58=0) + HOSTRET`：**`MAINRUN rc=4 kind=0 v0=00000000 endpc=044f4c02`**
`HOSTRET pc=044f4c02 ra=044f4c02 blocks=4`——主运行历史上**第一个 rc=0 正常终止段**
（此前全部 rc=114/20002/僵尸环）。SEQTOP 显示 4 块走 `0x3D6FC 首墙环入口→过墙→rfe 链→宿主段`。
**边界声明**：段仅 4 块——golden 快照点本就处于"服务收尾待交还宿主"中段，
本段完成=语义闭环建立（BLEEM 真机此刻同样是交宿主），非长程推进；
无 HOSTRET 时同环境仍是楔6 的 `rc=393 EMITFAIL 0x04500ECA` 形态（假阳性对照，host80 C 逐字锚）。

## 4. 混合模型双证汇总（战役图景更新）
guest↔host ABI 两面全部现形：① 服务表窗（RAM 0x100 页，jr t9 进宿主）
② 宿主 trampoline 段（0x044xxxxx，rfe 回宿主）。
**BLEEM = guest 码 + 宿主码协作**：A3 长程主运行 = 分段回放（HOSTRET census 段序列）
+ 宿主模拟器（楔8 候选：以 golden 状态词/服务表反推宿主每段做了什么）。

## 5. 楔8 定靶
`FWDMAIN` 多段模式：HOSTRET 后按"宿主职责"最小实现（递减 0x83C58、重派发 guest 入口）
续跑下一段 → **段序列 census**（段数/每段块数/服务指纹）——首个"准连续"主运行视图。
