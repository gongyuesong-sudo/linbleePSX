# 113 — 中断/定时器/BIOS 向量几何 + 真盘 GPU 流接入 (task-135, SESSION-141)

## 本轮战果(真盘=北斗神拳 SLPS_029.93, 全程只读零入库)
1. **Timer M2 全语义**(psx-spx 公开位义): enable 门控 / target 比较模式(达 target 归零+IRQ) /
   0xFFFF wrap 模式 / IRQ 受 ctrl bit6 门 / **读返回瞬时值**(M1 曾返推进后值=错)。
   战果: 游戏 Timer3 死等轮询 **490 万次 → 4 次**。
2. **irq_check 掩码正字**: 0x1FFF→**0x3FFF**(旧值漏 T3 bit13, 定时器中断永远进不了 CPU)。
3. **GPU 端口地址系统性纠错**(no$psx 正字, 旧自造面 0x1800/0x1804 自洽掩盖):
   - GP0 写 = **0x1F801810**、GP1 写 = **0x1F801814**
   - GPUSTAT 读 = **0x1F801814**(bit24 ready-cmd 条件位置 / bit26,28 就绪恒 1)
   - GPUREAD(0x1800/0x1808/0x1810 读) = **恒 0, 绝不 pop 命令 FIFO**(旧 pop 模型把游戏
     GP0 命令流整个吞掉——真盘 gp0 流 0→**7714 词**接入的直接原因)
   - ACK 读 0x1F80181C = 0
4. **syscall code 位段正字**: MIPS SYSCALL 立即数 = **bits21-6**(PSYQ `syscall 0x40`=0x0000100C)。
   旧 `insn & 0x3F` 抓到的是操作码(恒 0xC)=全盘错位。
5. **BIOS 向量几何**: syscall code 0x20-0x3F → **C(0x07) InstallHandler** 向量表(dev.sysvec[])
   优先分派, ra=延迟槽(handler jr $ra 天然续跑); 空向量=nop。**真盘钉字**: 游戏 InitGeom
   改写 RAM 页(A0/B0/C0 写 `syscall 0x40/0x48/0x4C`, 0x80 写裸 syscall)→ 页上 rootcall
   落空 = 0x80 自噬死环 → 加页守卫(pc<0x100 别名面)。
6. **rootcall 兜底**: code 0x40/0x44/0x48/0x4C = A/B/C 族选择器($t2 无效时按 code 推族)。
7. **新登记**: B(0x19) HookEntryInt [S](挂 entry 存值回旧)、B(0x35) 未定名假设面(低锚,
   回留驻缓冲指针——游戏把返回值直接解引用实证; PSXRecomp 测试名 RootCounterClearPolicy)、
   A(0x49) GPU_cw v0=回显入参(假设面, 见下洞)。
8. **retail 帧节拍**: 56445 周期/帧 + 每帧 VBlank(I_STAT bit0)注入 + gpu tick。

## 观测收敛
rc=7723 reject=0 maxdepth=0 cw=7713 **UNK=0** gp0=7714 词命令流接入; 八面 114 断言全绿;
窗口端到端 hw=1 125 帧无回归。

## 已知洞(下一楔, 诚实记账)
- **卡点**: 游戏字节填充循环以 v0(=0xFFFFFFFF 哨兵)解引用(`sb/sw $zero,0($v0)`)→
  根因 = **A(0x49) 返回缓冲的数据结构** ≠ 我们的命令回显/零区: 游戏从缓冲读到的计数域
  下溢(初值 2 的来源待钉)。需 no$psx A0 主表 + 多盘交叉定 GPU_cw 正语义(跨会话语义战役)。
- GPUREAD v1 恒 0(无 GpuRead 响应建模); 精确 timer 分频(1/3,1/4,256/外部时钟+H-VBlank 计数)
  待做(现=读步进 ADV 近似); RETAIL_HOT 采样步长仍粗。
- 门未写(卡点未过零)——task-135 保持 in_progress, 不满足 gate 现场 rc=0 不标完成。

## 观测/诊断面(留用)
- `--retail <盘>`: 盘面元数据; `--retail-probe <盘> [cycles]`: 帧节拍跑+缺口报告
  (rc/reject/maxdepth/cw 计数+未登记直方图+GPU 计数)。
- RETAIL_TRACE=1 首异常 40 步回环; RETAIL_STEP=1 帧进度; RETAIL_HOT=1 pc 热点(慢)。

## SESSION-142 追加 (楔 4/5)
- **GPU_cw 高锚回正**: PSXRecompStudio 测试注释直引 **OpenBIOS openbios/gpu/gpu.c**
  ("GPU_sync, then GP0 write", Issue #701, ReturnValue=0)——清洁房最高来源档。
  v0=0 回正后 trace 实证游戏**已过 800456FC 旧卡点**(v0 泄漏假说作废: 那只是前一站症状)。
- **冷装载排除自证**: COLDIMG 现场对照 = img[0]=PS-X EXE 头 / img[0x800]=RAM[DVA] 逐词一致,
  **loader 无 bug**。本会话 python 复核曾用 2048-stride 直读 2352 盘误算"数据段缺失"= 假警报,
  教训入钉: 复核物理偏移必须 stride 2352 + data_off。
- **新死环定性**(真帧节拍): epc=800587D4 `sh $v0,0x1AA($v1)` badv=9463941B(奇)
  v1=0x94639271 —— andi 0xFFCF/ori 0x10/sh 序列 = **游戏自解包器(LZSS 族)**;
  源指针槽 [0x800763E8] 污染 = 下一个 rootcall 语义洞。无节拍 trace 模式已过此点(时序相关)。
- RETAIL_EXC 永久首异常现场(v0/v1/a0/a1/t0/t1/sp + epc±3 指令窗)入常规探针输出。

## SESSION-143 追加 (楔 5 未过, 定性推进)
- **真盘零文件 API**: HLEOPEN/HLEREAD 追踪实证北斗神拳**不调 A 族 open/read**——全部数据
  驻留冷载 EXE(950KB)+自解包。此前"第二级文件装载"假说作废。
- **解包器静态钉字**(capstone 反汇编 RAM 镜像): `lw $v1,[0x800763E8]`→**0x1F801C00=SPU 基**
  (no$psx SPU 窗 0x1C00-0x1DFF 正字); 半字循环 `lhu($s2)→sh 0x1DA8($spu)` = SPU 采样传输,
  轮询 [0x1DAA]/[0x1DAE] bit10=传输就绪。**SPU 传输面 v1 落地**(FIFO 吸收+状态恒就绪),
  served 7723→10983。
- **影子存值回读面**: 0x1000-0x10FF 未建模寄存器(含 DMA 全局口 0x10F0 实证写 3 次)存值可回读。
- **RAM 自覆盖发现**: epc=587D4 指令窗内容 = 文件 58FC8 区代码(同段代码被复制到不同地址,
  游戏运行期自我重定位)——EXE 静态 vs RAM 动态**地址不重合**, 静态反汇编必须对 RAM dump 做。
- **剩余洞**: 死环 store 的 v1=0x94639271 污染源未定(非 [63E8] 槽、非 b35_buf、非 HLE 写)。
  **值猎手已落已跑**(每帧全镜像扫 0x94639271/9463941B): **零命中 = 该值从未写入 RAM** =
  v1 是纯寄存器算术产物(lui/ori 或坏指针算术)。下楔=静态反汇编 587B8 函数序言找 v1 首赋,
  画数据流钉生产者(跨会话语义楔, HUNT 面保留在探针内)。

## SESSION-144 追加 (楔 6 — CPU 级大正字)
- **JAL ra 错位钉死**(本战役最深 bug): 旧 `ra=延迟槽地址` → `jr $ra` 返回**重放调用者
  延迟槽** → 子程序毁 v0 后槽位带毒重执行 → 零售盘 EXC 风暴 **7712→2**。真机 MIPS
  ra=延迟槽后一条(npc+4)。JALR/BGEZAL/BLTZAL/向量劫持同修 4 处。
  教训入钉: 自造夹具槽位全是幂等 SW, 长期掩盖; p4 断言曾把错误约定钉死, 已随正字更新。
- **冷启交棒态正字**: cop0[12] IE=1/EXL=0(RETAIL_IRQT 实证: 游戏 mask=0x000D 开 vsync、
  i_stat bit0 已置, 但 IE=0 = BIOS 交棒态缺失死等; 真机 BIOS eret 进 EXE 时 IE=1)。
- **B0 CARD 族 [S]**(PSn00bSDK stubs.json drivers.s 正名: InitCARD 0x4A/StartCARD 0x4B/
  StopCARD 0x4C/_card_write 0x4E/_card_read 0x4F/_new_card 0x50; 传输原语诚实 -1)。
- **0x80 通用 rootcall 入口**: t2=0x80 且 t1∈0x20-0x3F → sysvec handler 劫持(ra=槽后)。
- **静态反汇编工作流**: RETAIL_EXCD(epc±4K RAM dump)+capstone 钉出真槽=**0x8006FA90**
  (lui 0x8007-0x570, 此前差一页 0x8007FA90 系位段误读), [6FA90]运行时=1F801814=GP0 口
  (描述符合法指向 GPU); HOT 桶打印截位 bug 教训(00b750 实为 0x8005b750)。
- **现状**: 真盘 60M 周期=1063 帧全跑, unknown=0, exc=2 一次型(待追, a1=1F801110 Timer
  指针相关 store), **GPU 空闲(gp0=5)**=游戏停 in SPU 混音例程不上画 — 下楔=钉游戏主循环
  等什么([s0+0x32] 声部字段/0x1DAA 状态位读回语义) + 一次性 cause=20 现场。

## SESSION-145 追加 (楔 7)
- **DMA 通道执行引擎 v1**(no$psx 寄存器正字): MADR/BCR/CHCR 组, D2 GPU C→V 块传输送
  GP0, **触发完成清 CHCR bit28**(影子面旧行为=存 1 不清 → OTAG 等待方永久死等, 本楔修)。
- **b_setjmp/b_longjmp [S] + printf [S] + B0:0x3F puts 别名 [R]** → 交叉盘三国志2
  (SLPS_012.20) **UNK 归零**, pc 推进 80180f84→80183228。
- **真盘执行模型定性**(交叉盘+长跑): **游戏把主循环跑在 VSync 中断 handler 内**(PsyQ
  callback 模式) —— cause=100(IP8) 恰一次 = IE 修正生效铁证; EXL=1 期间阻断二次中断 =
  真机同型, 非 bug。北斗神拳 9463 帧 gp0 5→14 缓增 = 长启动解包, 非死锁。
- **下楔**: ①加速/对齐启动路径(Timer 分频与 SPU 传输语义对 vsync 内循环的节流影响);
  ②一次性 cause=20 store 现场(北斗神拳); ③第三枚盘(NBA Live 2003 USA)交叉验证。

## SESSION-146 追加 (楔 8 — 双 CPU 级正字)
- **LWL/LWR/SWL/SWR 实现**(楔4 遗留注释债): 真盘 `8a630003 lwl` 落 default=
  保留指令风暴 4300 次; 旧死环 `ac400000 swl`(非 sw!)未实现一并破。MIPS 小端掩码合并:
  lwl rt=(rt&((1<<8a)-1))|(w&~m); lwr 反向; swl/swr=对齐字读改写。
- **帧节拍量级正字**: 56445 → **564456 周期/帧** (33.8688MHz/60)。旧值少一"0" =
  vsync handler 每帧只有 1/10 预算, 一切"每帧一步慢推进"皆此假象。
- **0x80 入口 12 位全命令** [no$psx]: bits8-11 模块(0=System 1/3=GPU 4=SPU 5=CDROM 6=SIO)
  bit7 表 bits0-6 ID; System 模块转 A/B 表服务。**"真盘零文件 API"结论作废** —
  北斗神拳 t1=0x00 = System-A-ID0 open 走全命令面。非 System 模块 UNK 直方图(下楔)。
- **战果**: 北斗神拳 1.1G 周期(1949 帧) exc=4300→2、UNK=0、pc 破 0x8006xxxx(解包过?);
  三国志2 200M 周期 UNK=0; 窗口 hw=1 无回归; 八面 0F。**GPU 仍空闲(gp0=14)** —
  下楔=钉 vsync handler 主状态机到 OTAG 提交之间缺的服务面(EXC1ST 面已备好)。

## SESSION-147 追加 (楔 9 — 三盘交叉: 出画前的真卡点是渲染主循环未达)
- **帧节拍 564456 修复后稳定复现验证**(三盘): 北斗神拳 2.2B 周期 exc 稳定 2、LWL 风暴
  归零确认(修复前 4300)。三盘 **GPU 全部空闲**(gp0=8-14, fill=0) = 出画卡点从"崩/风暴"
  **上移为"渲染主循环从未拿到帧预算"**。
- **北斗神拳真卡点(DUMPHOT+capstone 钉字)**: 主线程每帧全耗在 **PsyQ SPU PCM 音序器
  传输等待循环**(epc 区 0x80058b18/0x80058bfc): `lhu $v0,0x1a6($a1)` / `0x1aa($a1)`
  轮询 SPU 基(0x1F801C00)+0x1a6/0x1aa 传输完成态, `sltiu $v1,0xf01` 超时才跳。我方
  SPU 面不推进这些传输地址 → 每帧空转 3841×N 次耗尽预算, GP0 提交饿死。
  **下楔=SPU 传输地址/完成态推进模型**(SoundTransmit 语义: 写传输源→硬件搬→状态位达期望)。
- **NBA Live 2003 = 最活跃盘**(交叉验证出正向信号): served **18.2M** rootcall
  (ResetEntryInt 等解锁后), pc 最终跳 0x10(函数指针表 NULL 页)= 某初始化表未落;
  UNK 仅 A0:0x19/0x1B(3 次, 无公开登记=PSn00bSDK A 表止于 0x14, 诚实直答)。
  **这盘离渲染最近, 建议作为下会话主攻盘**。
- **三国志2**: 200M UNK=0 exc=1(IP8 正常) 但 served 仅 12 = 也卡早期。
- **战果**: 八面 0F; 三盘 exc 全部收敛(风暴类归零); 观测面齐(EXC1ST/DUMPHOT/HOT/SLOTS)。
- **诚实**: 零版权未破坏; **出画未达成, task-135 不标完成**(gate=fill>0 现场 rc=0 未满足)。

## SESSION-148 追加 (楔 10 — SPU 握手解锁, 主循环跑起, gp0 14->16.7M)
- **全 64KB MMIO 影子回读面**: 未建模寄存器读=最后写值(真机控制字 RMW 语义)。
  根因链: task-143 SPU 专案把 0x1DA8/0x1DAA/0x1DAE 写截胡进 spu_* 字段, 但读侧
  新落影子面 → 写读不一致 → 北斗神拳 SPU 混音握手循环(80058b94 实证:
  `lhu [0x1aa]; ori 0x30; sh [0x1aa]` 后 `lhu [0x1aa]&0x30` 死等)每帧超时 0xf01 耗尽预算。
  撤 SPU 专案 + 影子面扩到 64KB → **gp0 从 14 跃到 16.7M**(主循环真正跑起, 持续吐 OTZ 链)。
  教训入钉: 加通用回读面时, 早前的字段级专案必须同步撤除, 否则写读分叉=静默死等。
- **OTZ GPU 端 fetch + 0xFF terminator**: bits0-25=VRAM 下一命令地址(半字粒度),
  从 g->vram 沿链读命令字重派发(深度 512 + 环检测护栏)。gp0hist 证游戏 OTZ 巨量
  但下游 f1/f3(poly/rect)=0 = 链指向的命令区仍空(见下)。
- **CDROM DMA3->RAM 写回**(no$psx): ReadN 装填 cd_sect → 写 DMA3 CHCR(bit30&28)
  → MADR 落 cd_sect 扇区(超 2048=CDDA 零面)→ 清 CHCR bit28 → I_STAT bit2 CDINT。
  **未验**(游戏此路尚未触发)。
- **卡点再上移**: 主循环停在 80051340, 反汇编=等待 [0x80075054] 指向的 RAM 计数器
  与快照不等(80051b3c bne 轮询)+ 调用 B0 表(jr $t2=0xb0)。**下楔**: 钉 0x80075054
  计数器生产者(疑 = CDROM 读中断回写的读取完成标志, DMA3 面已就位待触发链)。
- **诚实**: **出画未达成, task-135 不标完成**。但失败模式已从"崩/风暴/SPU 饿死"
  收敛为"主循环等一个 RAM 完成标志"——离渲染仅一步。三盘 exc 全部 ≤2, 零版权未破坏。
