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

## SESSION-149 追加 (楔 11 — 死环定性 = 60Hz 设备回调派发)
- **北斗神拳终卡点钉死**(DUMPHOT+capstone 80051280-80051370): 驱动主循环向设备邮箱
  写状态字节 7(`[0x80074FF8]` 指向的 RAM)后**轮询等低 3 位清零**——96.c 型设备状态机,
  清位者=BIOS 60Hz 事件环逐个调用的设备回调(AddCblk 注册链)。
- **AddCblk [S] + psx_hle_vblank_fire 已落地但门控**(RETAIL_CB 默认关):
  首实验: 派发确实推进设备状态(served 174→243、邮箱字段变化、游戏开始新动作)=
  **方向实锤**; 但松散"指针形态"入参识别致一次回调跳进数据区(800750C0=ASCII 0x39312d33
  "3-19...")=excode10 风暴。**派发 ABI 未定=不许猜**, 待 PSn00bSDK drivers.s/sys.c
  源精读钉参数序后默认开。
- **教训入钉**: 中断/回调派发类合成必须参数语义有源可依; 松散启发式跳飞比不派发更糟。
- **回归**: 门控后干净基线 = served 174 / gp0 16.7M / exc 2 / 1063 帧(=task-148 态);
  八面 0F; 窗口 hw=1。
- **下楔**: ①精读 PSn00bSDK AddCblk/70mA 设备链源(func 参数形态+event 号) 开 RETAIL_CB
  默认; ②若派发正确后游戏仍等, 追设备状态机下一字段(CDROM 读完成=DMA3 已就位)。

## SESSION-150 追加 (楔 12 — 中断派发三级分派转正 + HookEntryInt 证伪)
- **CALLTR 取证面**(每 (vec,cmd) 首见参数+pc) 一次跑出全 boot 服务画像:
  C0:0A=(3,0,800750C0) A0:49/72/44 B0:08/0A(DMA3!)/0B/0C/19/35/4A/4B/56/57/5B ——
  服务面全景首次可见。
- **R3000 中断向量正字**: 旧 irq_check 跳 `0x80000000`(nop 海)=EXL 永久卡死 +
  被 vector 页 `jr $ra` 毁伤 ra; 正字=**0x80000180**(BEV=0)。三级分派:
  游戏自装 dispatcher(读 [80000180]≠0 即转发) > HookEntryInt 词派发 > 诚实吞源防 livelock。
- **HookEntryInt 证伪钉字**: 本盘传 `0x800750F8`=数据旗标词地址(RAM 实测=可复用缓冲,
  文件字节恰为代码是巧合链) —— 跳调用风暴 excode10, **参数语义不许从单盘外观臆断**;
  降级为"中断到达 → [hookword]|=1"。Cblk/3-19 链派发(RETAIL_CB)仍关:
  a2=800750C0 派发时刻亦已被游戏复用为数据(RWATCH 抓到 80051e40 写旗标),
  跳入必炸已两次实证。**邮箱清除方 = 3-19 内核链语义公开钉字缺失, 战役级卡点**。
- **RETAIL_WATCH**(归一化 lo:hi 写监视, 归一化坑=gpa&0x1FFFFFFF 传 0x8xxxxxxx 静默失效)。
- **回归**: 八面 114P/0F(ENG e1 向量改正字后); 北斗神拳 pc 深入邮箱轮询体 80051350
  (原 80051340), exc 2→1; 三国志2 200M exc=0; 窗口 hw=1。
- **下楔候选**: ①3-19 内核(class,id,ccb)派发模型的公开源(OpenBIOS r3000 dispatcher
  / PsyQ LIBSN 反汇编文档)再攻邮箱; ②转攻三国志2 路线(零异常最深, 可能先到首帧);
  ③NBA Live rootcall 面横向补服务。

## SESSION-151 追加 (楔 13 — GPU 字节端口面破邮箱 + HookEntryInt=jmp_buf 终钉字)
- **设备真身=LIBSN 96.gp**: 设备指针槽 [0x80074FF4..5008] = 1F801800/1803/1020/1801/1802/1C00
  (GPU 数据端口/就绪字节/GPU STAT/DMA base)。主线程死环 = `sb 1,[1800]; sb 7,[1803];
  sb 7,[1802]`(OTG 输出) 后轮询 `[1803]&7==0` —— 影子页把 0x1803 变死字节。**GPU 端口
  字节别名+size 判别+就绪读回 0** 三面齐 → **北斗神拳 600M 首次离开邮箱区, pc 深入
  0x80060554→0x8003F3CC 新领地, exc=1**。
- **HookEntryInt 终钉字**: 入参 = **jmp_buf 保存帧基址**(帧首=被保存 ra 可含标记位,
  如 80051E11), 绝非函数 —— 跳帧首 = 三國志2 IP8 风暴 75K 回归实证, 已撤跳路。
  游戏侧 SetEntryjmpBuf/GetEntryjmpBuf/ReturnFromException = setjmp/longjmp 上下文帧
  (sw ra,gp,sp,fp,s0-s7 布局已反汇编钉字 @80052324/80052360)。
- **真 BIOS 中断模型(楔 14 目标)**: IP8 到达 → setjmp 存主线程帧到 jmp_buf →
  跑 C0:0A 注册的设备回调链 → B0:17 ReturnFromException = 从帧恢复 + rfe。
  派发目标 = AddCblk class/id/ccb 链语义(公开源待钉), 非 hook 词本身。
- `_96_remove [S+Psy-Q Fall96 seminar]` = 移除 ISO9660; HLE 恒成功返 1; no-op 返 0
  曾致泵 2M 次/秒空转(served 画像 RCHIST 定位)。
- 教训入钉: 中断派发目标与保存帧严格区分——任何"看起来像指针的 BIOS 注册参数"
  先验证 (对齐 &3==0 + 落在 .text 观测范围 + 多盘交叉) 才可作为跳靶。
- **回归**: 八面 114P/0F; 三盘 300M: 北斗 pc 新领地 exc=1 / 三国志2 exc=0 / NBA exc=0。

## SESSION-152 追加 (楔 14 — Timer 时钟驱动 + VBlank 事件投递, 死等全解)
- **HOT 桶别名坑**: 桶=(pc>>4)&4095 有 0x20000 混叠, "~01a30" 实为 0x80051a30
  (tick 稳定读函数)而非 NOP 海 0x80001a30——读 HOT 先排别名再下结论。
- **Timer 终版模型(瞬时推导)**: cnt = f(主钟) 纯函数, 跨 tick 折叠补 IRQ;
  旧"逐读推进"使连续双读永不相等 → 引擎 tick 稳定读环(80051a08)死转 47 万次。
  真机=时钟纯函数, 背靠背读必同值。**教训**: 一切"读传感器"类寄存器建模,
  读必须是无副作用瞬时值。
- **T1 bit8=内部 60Hz 时钟源真盘实证**: 游戏 ctrl=0x0100(enable 位不置)期望计数;
  per=564456(=每 VBlank +1)。**PSX 只有 T0-T2 + 0x1F801130=RAM_SIZE**;
  引擎 [[0x80075054]]=0x1F801110 读的是 **Timer1**(不是虚构 T3)。
- **事件环接线**: CALLTR 复查翻案——b0:0A/0B/0C a0=0x19000000/0x19000001 =
  **我们自己的 OpenEvent 哨兵句柄**(a1/a2 为调用者残留寄存器); 游戏早先
  OpenEvent+EnableEvent 双事件, 但 VBlank 从不投递 → WaitEvent 永候。
  现每 VBlank 给 enabled 事件 fired=1。**教训**: 观测参数要与自己 HLE 的
  返回值编码对照读, 否则把句柄误读成"DMA 基址"。
- **C0:02/03 = SysEnqIntRP/SysDeqIntRP** [S stubs.json]: 通用中断回复链表(已实现),
  非事件表——一次低锚臆断(hc_init_ev_tab)随重复块整体撤除。
- **盘面**: 北斗神拳 600M pc=0x8003F414(新领地稳态, 非邮箱/非事件死等),
  UNK=0 exc=1; 三盘 200M 全净; 八面 115P/0F; 窗口 hw=1。
- **下楔**: 0x8003F414 处 HOT/等待面新取证; gp0hist f0=6(OTZ 少)——主循环若
  活, 追渲染路径是否首次真正建帧(disp 仍 0)。

## SESSION-153 追加 (楔 15 — R3000 异常出入口正字 + 派发时机翻案)
- **派发时机翻案**: 北斗神拳主线程**全程 IE=0**(IRQDBG 钉死)——LIBSN 世代真机 BIOS
  在**每次 rootcall 陷入时**服务设备链(3-19 内核), 硬件中断到达不是派发点。
  RFE 派发因此挂两触发面(irq_check + rootcall 服务点 vframe 水位), 均 `RETAIL_RFE` 门控。
- **R3000 出入口正字**: 异常入口 EXL←IE、IE←0; rfe=(IE←旧EXL, EXL←0, ERL=1) 与 eret 分义。
  旧 rfe≡eret → 用户态 LIBSN dispatcher 出口 IE 永不归位。
- **[HookEntryInt 槽] = 派发器恢复地址+1 标记**(0x80051E11: SetEntryjmpBuf 返回点 E10,
  v0==0 首进分支/v0!=0 恢复泵分支): 三错=未掩 LSB(奇址乱码)/未置 v0=1(走错分支)/
  未存帧(jr $ra 毁主线)。影子帧模型 ctx_saved+saved_gpr[32] 落地, B0:17 = 帧恢复+rfe 正字。
- **自家手误双案发现场**:
  1. python 切片重写块把 `if (dispw)` 跳 0x180 转发分支切丢 → 一切游戏自装 dispatcher
     被吞源兜底吞掉(ENG e1 当场 FAIL 抓获)。教训: 大块替换后必须 `git diff` 目检删失。
  2. RCHIST `getenv` 落进步热路(20 万次/步)拖慢 10x, **污染三盘对照数据**——
     三国志2"RFE 风暴"实为 getenv 性能+误跳混合; 诊断面自身必须是零开销或门控前置。
- **b0:08/0A/0B/0C 翻案二**: a0=0x19000000 = **我们自家 OpenEvent 哨柄**(a1/a2 是调用者
  残留寄存器非参数); 北斗 RCEND 画像 b0:0A x97 = 事件队列 API 主用户。观测参数必须与
  自家返回值编码对照后再定语义(已入 skill 钉)。
- **回归基线**: 八面 115P/0F; 三盘 200M: 北斗 exc=1(80051078 区活跃)/三國志2 exc=0/
  NBA exc=0 UNK=3(A0:19/1B 无公开名=诚实不猜); 窗口 hw=1。
- **下楔**: ①北斗 rootcall 水位派发开启后泵仅 3 拍=泵内 B0:0A WaitEvent 语义要接
  事件票(VBlank fired 面已有, 但泵在等**设备**事件而非 vsync——按 dev/class 投递);
  ②三國志2/北斗 dispw-180 转发恢复后首次可跑通游戏自装 dispatcher, 复查 HOT。

## SESSION-154 追加 (楔 16 — 主循环活性钉字 + eret/rfe 加固)
- **RETAIL_MEMDUMP 出口 RAM 画像面**: 北斗神拳 `[0x800D2AD0]` 帧阈值 100M=0x3c1 →
  600M=0x3c2、`[AD4]` 0x00168584→0x003C0002 跨时长**持续演化** = 主线程 pacing 循环
  **活着**（每帧在推进），但 gp0/disp 面为 0 → **引擎从未到达渲染提交点**。
  病灶从"死等"精化为"活着但到不了画"——卡在 pacing 与首帧提交之间的某资源门。
- **eret/rfe EXL 正字加固**: 非例外态(EXL=0)执行 eret/rfe = 陈旧 epca 盲跳；
  打击者1945 实证 `ra=800001A8 + epc=801FFFB0` 弹栈风暴自激(82K 次)。
  现语义: EXL=0 时 eret/rfe 视作 nop 顺序续跑(诚实回退, 不伪造返回)。
- **打击者1945 定性**(RCHIST+SYSTR): `A0:3F printf ×82419` = 游戏自身死锁报错环
  (LIBSN "3-19"-型 fatal: 等待资源不可得→printf 循环)。SYSTR 零输出 = 全部
  syscall 均有号可服务, 缺口在**资源到位**, 与北斗神拳泵等资源同一类病。
  非楔 16 引入回归(SESSION-152 收账时该盘 1128912/风暴已如此)。
- **SYSTR 面**: rootcall 未服务回退路径按 (vec,cmd) 形态首见打印(含 syscall code)。
- **路线裁定**: 曾评估装公开 BIOS ROM 解 fatal —— **违反零版权纪律(用户硬约束
  "不用任何 bios")且 .bios=SCPH 禁列, 即 OpenBIOS 亦 BIOS** → 放弃, 继续 HLE 正路。
- **盘面**: 八面 115P/0F; 北斗 200M exc=1(活)/三國志2 exc=0/NBA exc=0 UNK=3; 基线不变。
- **下楔**: 北斗"活着但不到渲染"精查——在 pacing 出口(jal 800519e4 返回后分支
  800510c0)设写点/RWATCH, 钉帧循环后续路径停在哪一 gate; 该类 gate 常为
  [80074xxx 设备块] 某字段 = 等待 CD 数据到位 → 复查 DMA3/CDINT 是否真被消费。

## SESSION-155 追加 (楔 17 — 北斗最后异常=假警报 + NBA 早期定性)
- **北斗神拳唯一残异翻案(假警报缉拿)**: 异常环 `cause=4(读错) epc=8004f458` 曾疑
  "弹栈风暴残余"。EXC1ST dump 反汇编钉死: 该处 = `lw $ra,0x2a58($ra); jr $ra`
  **游戏自身合法 RAM 间接调用**(trampoline 表), 环里 npc==pc 配对是陈旧观测面假象。
  → 北斗 200M **真·零异常**, 四盘异常面全部干净。
- **EIGHT 取证面**: excode=8 首 5 见打印 (epc,npc,ra,syscall_code,t1,t2,v0)——
  syscall 语义分裂类 bug 的标准探针(gated RETAIL_IRQDBG>=3)。
- **NBA Live 2003 定性**(楔 15 "UNK 3 疑因"结案): 200M 零异常, 卡 `800577a0` 双读
  稳定 tick 环(与北斗 800519e4 同型, EA 用自家 LIBSN 变体), tick 指针表
  `[0x8009A468]=0x03ff000a` 非 MMIO = **表未初始化 = 启动未完成**(served 仅 27)。
  UNK(a0:19/1B)非卡点主因; 四盘收敛到**同一类病**: LIBSN 内核 tick/设备表
  初始化链在 HLE 上走不完 → 到不了首帧提交。
- **战役方向收敛**: 四盘证据已把靶心从"零散服务缺口"收缩为单一假设——
  LIBSN 初始化依赖的某个内核服务(候选: C0:0A 设备链注册后的 60Hz 真派发闭环
  [80075054]=1F801110 北斗 tick 源=Timer1 但 [80075050]=1F801814=GPU STAT 读被
  vblank_fire 语义牵连; NBA tick 表写入方 = 未跑到的某 rootcall 回调)。
  下楔: 在 NBA tick 表 [8009A468] 挂 RETAIL_WATCH 写监视, 钉"谁应该写它"。
- **回归**: 八面 115P/0F; 窗口 hw=1; 零版权纪律未破坏。

## 楔18 (SESSION-156/157): 8MB 物理 RAM 正字 — NBA Live 2003 真 8MB 盘解锁
- 取证钉字: NBA hook 槽 [0x8008A510]=0x807FFF09 → 根处理器=0x807FFF08=**8MB 顶**(2MB 折叠永丢失); NBA sp=0x807FFED8 实证 8MB 变体盘。
- 正字: PSX_RAM_ALLOCSZ=0x8000000 (满 8MB, 曾误写 0x400000=4MB → NBA store 0x807FFFF4 宿主段错误 core dump, gdb 现场 rax=0x807FFFF4 钉死); psx_translate RAM 掩码 0x1FFFFF→0x3FFFFFF; 低 2MB 游戏行为完全等价。
- 宿主面防护: RETAIL_EXC epcw 环 epc=0xFFFFFFF8 地址回绕裸解引用崩溃 → 加 gpa∈[0x80000000,0x88000000) + win==RAM 双门 (诊断面绝不可自杀)。
- CPU/HLE 跳转守卫 0x80200000→0x88000000 (5 处: psx_cpu.c×2, psx_hle.c×3) = 8MB 镜像面合法目标。
- 交叉面: NBA 200M 不再钉死 tick(旧 epc fffffff8 风暴消失), 现卡 800577d8 tick 前序初始化(CD_init printk 已出); 北斗神拳 600M 稳定如常 (1063帧, UNK=0, exc=1)。
- 打击者1945 **翻案**: 旧"200M 零异常 pc=8016cfa0"= 2MB 折叠假死环; 8MB 后突破至 served=82426 深度 rootcall 段, 新卡点 jr $ra→ra=0x801FFFB0(栈) 执行数据字 00000001 → excode10×82417 = 下一楔取证对象 (ra 污染链: 疑 rootcall 返回路径某服务未设 ra)。
- 八面 115P/0F (psx 折叠断言 xlat_ram_full_mirror_fold/xlat2_bfc8 升 8MB 正字)。
- 教训: 内存掩码扩容必连带 ①allocs=掩码幅 ②裸指针诊断窗门 ③全部跳转守卫范围; MAP_FIXED_NOREPLACE 保护宿主, 崩溃必是逻辑越界非覆盖。

## 楔19 (SESSION-158): 水位派发 rfe 弹帧正字 — 打击者1945 excode10 风暴根除
- 取证链: RETAIL_TRAP=<excode> 逐拍停 + RETAIL_PCRING pc 历史环 + ringwin 活体 RAM 反汇编
  (文件反汇编≠运行时 RAM 再证: 80162984 处活体=addiu $t2,0xa0/jr $t2/addiu $t1,0x3f rootcall 蹦床)。
- 死环: rootcall 水位派发(task-153 楔15)存帧后 dispatcher 以 **rfe 结束**(不走 B0:17)→
  旧 rfe 处理不弹影子帧 → 主线程 ra/pc 丢 → jr $ra 落栈数据 0x00000001(reserved) → excode10×82417。
- 正字: rfe 遇 ctx_saved=1 → 弹影子帧(与 hb_return_from_exc 同字段)= LIBSN dispatcher 出口兜底闭环。
  打击者1945 20M: served 8 exc 0(旧同窗 exc=82417 风暴); vec 页活体钉字 [a0]=0000100c/03e00008 完好。
- RAM_SIZE 正字: 0x1F801130 从 timer case 组全剔除(PSX 仅 T0-T2), 读=ram_size(0=8MB), 写=只读忽略;
  dev 断言 +3 (ram_size_1130_reads_0_8mb / write_ignored / timer0_stable_read) = 八面 118P/0F。
- 交叉面(rfe 修复后, RAM_SIZE 前二进制): 北斗神拳 200M served=172 UNK=0 exc=1(epc 8004f458 良性同旧);
  三国志2 200M served=11 exc=0 ✓; NBA 200M served=27 UNK=3 exc=0 ✓; 窗口 hw=1 ✓。
- 诊断面新增(全部门控零开销): RETAIL_TRAP=<excode hex/1=任意> 首目标异常即停; RETAIL_PCRING=1 64 深 pc 环;
  TRAP 现场含 vec 页活体 + ringwin RAM 反汇编窗 + 栈窗。
- 诊断坑(本楔踩): RETAIL_HOT 采样 z+=64 实为 1/64 步进(时间轴缩水 64x), HOT 结果不可与 cpu_run 面直比。
- 终版复验(全部改动后): 八面 118P/0F; 北斗神拳 200M served=172 UNK=0 exc=1(良性同基线);
  三国志2 200M served=11 exc=0; NBA 200M served=27 UNK=3 exc=0; 窗口 PSXWIN frames=66 hw=1。
- 遗留(下楔): 打击者1945 单发 cause=0/jump-to-0 残例(80169820 lbu 循环取 NULL 服务, 疑某
  getter rootcall 该返 RAM 指针槽); NBA UNK a0:19/1b; 北斗神拳 disp=0 渲染路径未建帧。
- 本楔宿主崩溃教训: EXC1ST dump `epc-0x80` 无门 → epc=0 时地址回绕; 任何以 guest 地址算窗口
  的诊断面必须 [0x80000000,0x88000000)+win==RAM 双门(与楔18 epcw 同规)。

## 楔20 (SESSION-159): 北斗神拳 vsync→渲染派发链全景取证（方向钉死，实施留下楔）
- 对照实验: RETAIL_RFE=1 对三国志2/打击者/NBA 均 200s 超时(帧首误跳风暴未除根) → 水位派发默认关维持。
- 北斗神拳死环解剖(活体 RAM 反汇编, 文件镜像≠运行时三度实证: 0x80060548 文件=jal RAM=lui):
  主线程 8003F530 轮询 [800D43A8](gp+0x3A8) 内核计数器 >0; 递增者=游戏 LIBSN 内核 vsync handler;
  handler 触发权=BIOS VBlank 设备链。**tick 源正字**: [80075054]=0x1F801110 = 游戏直接轮询 Timer1 MMIO
  (非 RAM 计数器; RWATCH 覆写实验钉死: 强写 54=计数器 → 游戏解引用崩 cause10×10787)。
- BIOS 设备链设施已建: SysEnqIntRP/SysDeqIntRP **编号链**正字(游戏 enq(2,8007706c) 实证; PCB 活体
  布局={pcb@0,func@4,next@8} 翻案 PsyQ 手册序); ChangeClearRCnt(c0:0A) 翻案旧 AddCblk 误登
  (stubs.json 正名), 登记 VBlank 自动清零地址(80091054/800750c0 实证)。
- **最后一层缺口钉死**: RETAIL_RP=1 链火 → 游戏 vsync handler 跳**自己复制进物理 0xDFAC 的 GPU
  dispatcher**(RWATCH pc=8005c044 覆写我方桩实证; 内容=lw GPU 状态+andi 0x80 忙等)→ handler 所需
  寄存器上下文($v1=GPU 基址类)未钉 → 万发 address error。方向正确、门控默认关待下楔:
  钉 vsync handler 被 BIOS 调用时的 ABI 上下文(对照 hook 派发器 80051E10 路径)。
- SWI0#8 低锚实验(EnableInterrupt 假说)**翻案**: 置 IE=1 触发派发链后游戏内核自算指针 a000DFAC 缺页
  0xE00 → 万发风暴 → no-op 政策面保留(号义待钉)。
- 诊断面: TRAP near[a0/v0+] 指针邻域 dump; RPFIRE/RPENQ/CLRRCNT 打点(RETAIL_RPTRACE 门控)。
- 回归面: 八面 118P/0F; 北斗神拳 600M/三国志2 200M/打击者 200M/NBA 200M 全基线; 窗口 hw=1。

## 楔21 (SESSION-160): LIBSN idle 语义 + 链火全帧保护 — 北斗神拳脱离死环进内核派发
- LIBSN idle 正字假设 [P] (RETAIL_IDLEIE 门控): WaitEvent 无就绪事件=内核空闲态 → 置 IE=1
  停等中断(真机 halt-with-IE 语义)。**效果钉死**: 主线程首次脱离 8003F530 死环, 经 RFE 派发
  (IRQDBG->RFE 80051e10 帧已存) 进入内核派发调用链(终态 pc=8005112C, 旧基线恒 8003F53C)。
- hook 槽演化实证派发器真跑过: [800750F8] 从 0x80051E11(首装 odd+1) → 0x80051E10(dispatcher
  消费后自更新) = LIBSN entry dispatcher 至少完整执行一遍。
- 链火(链 #2=**线程就绪链**非 C 回调链, 翻案楔20): func=80065270=ProcessEntry 派发体
  (首条 lbu $a0,($s2) → PCB 经 $s2 传入, LIBSN 内部 ABI 非常规 a0)。
- fire 两大翻案(实验钉字): ①tick 镜像彻底撤销——[80075050]/[80075054] **都是**游戏自写 MMIO
  指针(T1=1F801110), 写计数值=毁指针→lw [2] address error 万发风暴; ②fire 必须全 32 寄存器
  +cop0 保存恢复(只存 ra 毁主线程 s2/s3/s5 索引=cause10×51105 风暴; 全帧后 exc 51105→2)。
- fire 内时钟泵: 每 64 步 c->cycles+=564456/16 + psx_dev_sync(handler 内自带 T1 tick 等待环)。
- ProcessEntry done=0 定性: LIBSN 线程体**不返回**(挂起=ChangeThread), tramp 返回检测永不命中
  → 链火转正需要完整 PCB 上下文切换内核模型(每线程专属栈 807ffxxx + sp/ra/s0-s7 保存区)= 楔22 战役。
- 8005FAF0 计数器第二写者 = 内核 init 批量清零环(swarms sw $zero 43a0/43a4/43a8+8×N), 非递增者。
  真递增者=dispatcher vsync 分支(待其完整跑通后自然出现)。
- 门控矩阵(全默认关): RETAIL_IDLEIE(WaitEvent 开 IE) / RETAIL_RFE(hook RFE 派发, 维持) /
  RETAIL_RP(链火) / RETAIL_RPTRACE(fire 打点)。默认面八面 118P/0F + 四盘基线不动。
- 三合一最优实验组合(留下楔转正候选): RETAIL_RFE=1 RETAIL_IDLEIE=1 → 主线程进派发链无风暴。

## 楔22 (SESSION-161): 出画根因链终极定性 = LIBSN 设备 init(SIO/CDROM)未完成 → 驱动拒渲染
- **考古翻案 task-148 "gp0=16.7M"**: worktree 复跑 bcf9c98 实证 16.7M 全在 f0 家族
  (command 0x00-0x1F=NOP 区)= OTZ 链吃 VRAM 垃圾的假进度; 后续修复只是暴露真态。非回归。
- 出画阻塞真链(全部活体钉字): LIBSN InitDevices(0x41a 位图) → SIO pad 握手
  (W 1048=0x0D baud → 等 RX 字节计数 1044 bits3-6 + 1040 RX 数据)→ 我们 RX 恒 0 →
  init 失败分支 `sh $zero,0x50c2($at)`(8005202c) → 驱动就绪位 [800750C2]=0 →
  GetFreeCount 门 `jal 80051d68`(=return [800750C2])永假 → 主驱动拒进渲染循环 → gp0=4。
- 指针表正字: [8007614C]=1F801070(I_STAT) [80076150]=1F801074(I_MASK) [80076154]=1F8010F0
  = LIBSN 内核直操中断控制器(800520b4 sh zero 清 I_MASK= EnterCritical 模式, 非 SIO)。
- 新增实验门控(全默认关, 基线不动): RETAIL_RFEV0=<0|1>(RFE 派发 v0; v0=0 首进触发
  EntryInt 引导 → 跳 NULL 页 nop 海 = 需 BIOS 内部 entry-init 例程, 零 BIOS 盲区钉字);
  RETAIL_RFEALWAYS(hook 派发不看 IE — LIBSN 恒 IE=0 轮询模型实证 IRQPROBE: EXL=1 内核常驻);
  RETAIL_SIO1044/baud 影子 + 1040 RX=0xFFFF 无垫应答模型(游戏读 0xFFFF 后 SIO 轮询收敛);
  RETAIL_SIOTR(SIO 0x1040-0x104F 数据级 trace 400 条)。
- VBlank 票投递并入 sysrp_fire(全 enabled 事件 fired=1, 真机 BIOS vsync 服务面)。
- 下一楔(楔23)战役=SIO pad BIOS 引擎完整模型: VBlank 自动轮询序列(0x01 0x42→5A FF 无垫/
  5A 01 pad 数据)+ RX FIFO + 1044 bits3-6 计数 + 104A ACK 语义; CDROM 命令链(dev 1820 面游戏
  尚未发到=被 SIO 卡前)后置。完成判据=[800750C2]≠0 → 驱动进渲染 → gp0 真实命令(tri/spr>0)。
- 回归面: 八面 118P/0F; 北斗神拳 600M 基线(excit=1 pc 8003F53C); 全部门控关。

## 楔23 (SESSION-162): SIO pad 引擎握手过关 + HookEntryInt/失败清理两翻案
- SIO 握手全通过(RETAIL_RFEALWAYS+RP+SIOTR 组合): 游戏 SIO 引擎(80065824 活体钉字)
  等 **1044 bit0=TX ready**(楔22 只投 RX 计数不置 bit0=仍死等) → baud 写投 `1|(2<<3)`;
  RX 头应答须 **0x5A 5D**(在位 digital pad)——0xFFFF 无垫头被 800666f4 判超时。
  过关证据: SIOTR 无再轮询(1044→1040→104A ack→清 0 全序列), pc 80051134→800511E8 推进。
- **翻案1: "失败清理函数"(80052004-2c)= LIBSN 内核 idle 函数**: sh zero(I_STAT ack 全源)→
  B0:17 trap(ReturnFromException 收尾)→ `sh zero,[50C2]`(entry-pump 标志清零=正常态)。
  80051d68 返回 [50C2]=0 → 走 800511e4 idle 路 = **健康路径**, 非失败门。楔22 定性修正。
- **翻案2: [800750F8] "hook 槽"= SetEntryjmpBuf 的 ra 保存槽**(80052324=SetEntryjmpBuf 活体
  全帧 sw ra/gp/sp/fp/s0-s7 @0x2C 字节; 80051e0c a0=800750F8 实证)。E11→E10 演化=内核存帧
  所致, 非 HookEntryInt 消费。真 entry 派发目标不变(80051E10)。
- 现状: SIO/事件/中断/内核 idle 全线活; 主线程等 [800D43A8] vsync 计数器 > 0, 递增者=
  entry 派发器 vsync 分支, 其触发权=每 VBlank 一次 RFE 派发(现仅 2 次=派发器 EXL=1 常驻
  泵内等 LIBSN 用户态队列票, 票源=vsync handler, handler 又等 GPU ready [80075050]=1F801814
  字读非零=**GPUREAD 恒 0 面**, 环未闭合)。
- 楔24 战役: 闭合"派发器泵 ↔ vsync handler ↔ GPU ready"环; 候选=1044 面同型的 GPUSTAT
  就绪语义核对([80075050]=1F801814 lhu 读值语义) + RFE 派发每帧一次的 stat ack 时机。
- 门控矩阵(全默认关): RFE/RFEALWAYS/RP/RFEV0/SIOTR/SIO(baud 影子自动随 RP 面)。
- 楔23 续(链火内最后驻点全解): dispatcher func=80065270 → 80065824 SIO TX 引擎(活体反汇编
  全环: 8006587c 等 1044 bit0→800658bc sb ~s3 写 1040 发字节→800658a4 jal 800666f4)。
  **800666F4 = 超时检查器**(非 1044 轮询): 读 Timer2(1124/1128)+RAM 阈值表 [800D6AF4]/[800D6AF8],
  v0=not-timed-out; 800666f0 循环自清 [6AF4]=0(RWATCH 实证)。RPFIRE done=0 驻 pc=80066764 =
  等待环条件 `a0<[800D6AF4]`=0 永假 → beqz 直落 = **等的是游戏侧 TX 完成计数器递增**, 非 MMIO。
  该计数器递增者=真机 DMA/中断收尾(0x807FFE98 高栈帧), 环闭合需 CDINT/SIO INT 到 I_STAT 面。
- 楔23 新增实验面(默认关): VBlank 票注入 [80075040]=1(fire 内, RP 门控)——60M 无消费方,
  泵不读该槽(假说降级); [6AF4] 面为真候选。

## 楔24 (SESSION-163): SIO 语义全面翻案 + TX 完成同步清零模型
- **翻案(800666F4 活体全解)**: 该函数不是超时检查器。正字=游戏**软件 BUSY 标志**
  `[800D6AF4]` 等待: a0=port&0xFFFF, `v0=(a0-[6AF4]) >= [6AF8]阈值`。TX 发送原语
  (80065824 全解): flush 读 1040(期望 idle 残留 0xFF) → 等 1044 bit0 → sb ~s3 写 1040
  → jal 800666f4 循环等 BUSY 清零 → 二次写 → 出。清 [6AF4]=0 的函数=800666f0(sw zero)。
- 楔23 的 5A5D 预投**反而卡死** flush 阈值判定(5A<0x91=0x800D6AF8 活体钉字);
  0xFF idle 残留才 >=0x91。→ **FIFO 模型 v5**: 1040 无数据恒读 0xFF; TX 字节写=
  同步完成传输(排 RX 应答 5A5D + **直接写 RAM 清 [800D6AF4] bit0**, psx_translate
  双门 win==RAM)。握手 SIOTR 全过。
- 默认门(SIO FIFO 常开)北斗 200M: served 180→172, pc 推进进 GetTickWait(800519E4)
  函数体=主线程活; 三国志2 exc=0 基线保。八面 118P/0F。
- 链火 RPFIRE done=0 复勘: pc=807FFE98 = **自家 fire trampoline(jr $ra)** 上=handler
  已返回一轮(楔21 线程体不返回正字的一部分); [800D43A8] vsync 计数器 30M/100M 均=0
  无递增者 → 递增方=派发器 vsync 分支, 其在 fire 4000 步预算内未到达。
- 楔25 候选: (a) fire 预算 4000→加大/分段, 追踪 vsync 分支到 43A8 递增;
  (b) 8005F318 设备表初始化 8×0x18 结构(800D42E0)语义; (c) 主线程 GetTickWait mode3
  与 [80076188] 指针调用环。

## 楔25 (SESSION-164): fire done 判据修正 + 链火线程 A 画像全解 + T3 映射钉
- done=0 假象根除: 旧判据 `pc==tramp && npc==tramp` 因 `jr $ra` 延迟槽差一拍永不命中。
  修正=pc 落回 tramp 即 done; 另加 RETAIL_RPBUDGET(默认 4000) 可配预算 + qtr/half 采样 pc + RPREG 行。
- 链火线程画像(budget=100k): PCB-B done=1 正常返回; PCB-A 在 80065a2c↔800658a4 外层多轮
  循环跑 SIO 多字节序列(qtr/half 采样 pc=66f4/6768 快速通过, **非卡死=在推进**)。
- 800666F4 语义终正字(RPREG 钉 v1=0x91 入参): a0=v1&0xFFFF=0x91 >= [6AF4]=0 走 6714
  Timer-target 累加路(1128=T3 target=0 → +0), 6758 `v0=(a0-[6AF4]) >= [6AF8]=0x91` → **v0=1 即过**
  =该函数在当前状态立即放行; 楔24 "永卡"定性撤销, 是外层序列在跑。
- T3 寄存器映射钉字(本世代 dev 面): count=0x1120 / ctrl=0x1124 / target=0x1128
  (游戏 SIO 引擎读 1124 ctrl&0x200 + 1128 target; target=0 走免累加路, 无需强推 T3)。
- [800D43A8] 计数器: 30M/100M 均=0, 仅有的两写者(8005b754 memset 环 / 8005faf0 初始化)
  都是清零; **递增者仍未现身** = 楔26 主攻(候选: dispatcher SIO 序列完成后的 post 处理)。
- RETAIL_SIOBUSY=值 实验面(覆写游戏软 BUSY [6AF4])已入库门控, 本楔实测未改变行为(佐证
  [6AF4]=0 放行正字)。
- 回归: 八面 118P/0F; 默认门北斗 200M served=172 exc=1 (楔24 同); 三国志2 exc=0; 窗口 hw=1。

## 楔26 (SESSION-165): 驱动 vtable 全解 + DMA 票 RMW 语义 + GPUSTAT busy 窗入库
- **驱动泵真身 = thunk 表转发**: 80051c74/80051ca4/... 全族 = `lw v0,[80076148]; lw v0,+N(v0); jalr`。
  vtable(头 8001111c)@80076128: +4=80052690(票/DMA 属性)、+8=80052050、+0xC=80051da8、
  +0x10=80052198、+0x14=80052468、+0x18=80052238、+0x1C=**[800750C0]=设备 CB**(50C2 就绪位所在)。
- 80052690(a2=票号,a1=DMA 通道) 正字 = **DMA 属性 RMW**: 票槽 [8007619C+4*a2];
  a1!=0 → 清 DMA 票 bit(a2+10) | 0x01000000(触发传输); a1==0 → 置 bit |0x01000000。
  主线程每泵 `jal 80051c74; a0=3` = 反复申 DMA3 票(CDROM 传输)。
- GPUSTAT bit24 = **FIFO ready(非满)**, 非 DMA busy; 游戏 while([GPUSTAT]&0x01000000)
  自旋等 ready。v1 同步消费恒 ready → 驱动直通(非阻塞点)。
- 新入库模型(默认开): GP0 字/GP1 写后 `gpu_busy_until=now+564456`(1 帧), 窗内 GPUSTAT
  清 bit24 = 真机命令突发时 FIFO 非 ready 的物理形态。dev 断言 gpu_gp0_port1810 复原 16P/0F。
- 现状不动点: 主线程泵 pc=800510A0 区(DMA 票循环+GetTickWait), 43A8 仍无递增者, gp0=4。
  **卡点收敛为单点**: DMA3 票申了但 CDROM 传输服务未回流(票触发→CD 读→完成票/计数器)。
- 楔27 战役: DMA3 票 → CDROM 读 → 完成回环(游戏侧 DMA 票消费 + [80076198] 状态推进);
  候选缺口 = ReadSector 命令字面(CD 命令寄存器观测面)或 DMA3 CHCR 触发未被票路径走到。

## 楔27 (SESSION-166): CD 面零命令钉字 + SIO 字节弹出 FIFO + 新 probe 统计面
- **决定性观测**: 北斗神拳 100M **cd cmds=0**(CDROM 零命令!) + dma3 全 0 + i_stat=0x0001
  + i_mask=0x000D = 游戏从未走到真 CD 读代码; 楔26 "DMA3 票→CD 回环"候选降级——
  票是 LIBSN 内部 RAM 票, 未到硬件面。卡点仍收敛于 vsync 计数器 [800D43A8] 无递增者。
- 新 probe 面: `psx_dev_probe_stats`(cd cmds/cmd/sect/resp + dma3 三件套 + i_stat/mask,
  读观测零副作用, 每次 --retail-probe 自动打)。
- SIO RX 转正字节弹出 FIFO(证据=80065854 lbu RX 端口直读): size=1 弹一字节,
  size>=2 弹两; 无数据恒 0xFF idle; 1044 计数随 pop 递减。TX 命令→排 8 字节完整
  pad 应答 [5A 5D 5C 5D FF FF FF FF]。
- 线程 A 画像更新(qtr RPREG): PCB 基 **s0=800D6880**, RX/TX 端口=pcb+0x3C/+0x40
  (均=1F801040), 命令字 s1 ∈ {0x88 poll, 0x22 config}(由 pcb+0x44 状态选: flush 写
  FF → ≥9 → 0x22 路); 66f4 返回 v0=0 的入参实态=entry v1(≠收尾打印值), 前次"立即
  放行"推断部分失真(打印点寄存器已被 6764 覆写)。
- 翻案撤回: [80076194] "TTY 缓冲"预置实验无效(布局未钉=臆测, 已撤并留档)。
- 楔28 战役: 线程 A fire 内**逐字节**钉 RX 消费序列(SIOTR+fire 窗口联合), 判 8 字节
  应答哪一槽位不被接受; 或对照 psx-spx pad 时序(0x01 0x42 → 5A 5F/5A 5D 模式字)。
- 回归: 八面 118P/0F; 北斗 100M served=172 exc=1; 三国志2 exc=0。

## 楔28 (SESSION-167): 1044 bit9 TX 门 + 66D4 快照器全解 + 步幅"正字"翻案撤回
- **1044 bit9 = TX 完成门**(活体钉字 80065568 `lhu 4(v1); andi 0x200` beqz 分支): 投上后
  线程 A 从 65878 深进到 **800655A8**(大跨越)。1044 现模型 = bit0|bit9|(rxf<<3)。
- **800666D4 = 延时测量器装填**全解: `[6AF4]=lhu(1F801120) 快照; [6AF8]=a0 阈值`。
  配 66F4: 出口=(当前读值-快照)>=阈值 = 真机"等 T3/Counter 推进 thr 个 1/8MHz tick"。
- 新驻点(楔29): 800655A0 等待环 `jal 66f4; beqz→自旋`, qtr 采样 busy=0xFFFF thr=0x91
  t3(cnt@1120 旧步幅)=0 → (a0-0xFFFF) 有符号永假。缺口=游戏写的 1120 读回必须是
  **随时间递增计数器**(复位后 T2 cnt 或 free-running Counter), 本模型 1120 复位=0 且
  无 enable 不走。候选修复: 1120 读恒回 free-running(sysclk/8 折叠 16 位)不看 enable。
- **翻案撤回**: 曾按 psx-spx 布局(T3=1124-112B, 0xC 步幅)重映射全部 timer 地址,
  实测游戏行为零变化(仍驻同点)→ 本代 LIBSN 按 Eriksen/no$psx 旧文档 0x10 步幅
  使用(T1 cnt@1110 读 tick, T2/Counter cnt@1120, ctrl@1124, target@1128)。已全撤。
  教训: 两说并存时以游戏活体读址为准, 勿以文档"正字"重排地基。
- **stale binary 实锤再犯**: 撤回轮"全绿"是编译失败后的旧二进制; grep error 复原
  真编译后暴露 3 条误删(I_STAT EVTGEN case/g_rd 计数行), 补回后真全绿 118P/0F。
- 回归(真新二进制): 八面 118P/0F; 北斗 100M served=172 exc=1; 三国志2 exc=0;
  打击者 exc=1; 窗口 frames=125 hw=1。

## 楔29 (SESSION-168): **链火全链 done=1 首次达成** — SIO pad 引擎完全打通
- 三连修复(全常开面):
  1. **1F801120 = free-running Counter**(no$psx 公开义, 1/8MHz, BIOS 常驻不等 enable):
     读回 `(sysclk/8)&0xFFFF`。66D4 快照/66F4 延时判定立即成立 → 线程 A 破 800655A0
     环深进到 RX 等待体 80065D54。busy=0xFFFF 驻点根除。
  2. **1044 RX 计数按需补投满帧**: 计数<8 时自动重排 8 字节 pad 应答
     [5A 5D 5C 5D FF FF FF FF](BIOS pad 服务=每 VBlank 轮询在位 pad 的稳态等价)。
  3. **1044 bit1 = RX 就绪恒置**(65D68 `andi 2` 等待门; 104A 影子联动版无效已迭代)。
- 结果: RPFIRE **每轮 done=1**(PCB-A/PCB-B 均正常返回 trampoline) = 链火派发体
  (pad polling/DMA 服务/延时等待)首次完整跑完。楔21-28 的"线程体永返/驻环"时代结束。
- 未闭合(楔30): [800D43A8] vsync 计数器仍无递增者, gp0 仍 NOP 族 — pad 链≠vsync 链;
  vsync 票需 VBlank 事件→游戏 vsync 回调(C2 表/enq 另一链号)路, AddVSyncCallback
  登记面(A0:32/33 VSync/VSyncCallbacks)CALLTR 从未观测到调用 = 游戏可能用 b0 事件面。
- 门控实验面(retail): RPBUDGET=100000 + RP/RFE/RFEALWAYS 组合下链火全通过。

## 楔30 (SESSION-169): [800D43A8] 身份翻案 = DMA 队列槽分配计数器(非 vsync!)
- **静态+活体交叉钉字**(EXE 文件图像 8006Axxx-8006Bxxx, 该区活体已被覆写为字符串池=
  二段自解包挪作数据用, 反汇编必须用文件图像 vaddr=data_vaddr(80010000)+off-0x800):
  - `8006B27C` = **NewDmaQueue 服务**: a0=通道 id(→[80076CE4+ch*4] CB 指针表), 前置
    CB!=NULL && a1!=0 && **[43A8]<8**; 体=8005F494 环 alloc→写类型 2→名字拷贝→8005FD4C→
    `43A8++`(8006B35C-370)。
  - `8006AB04` = DMA 队列等待环(`slt s1,[43A8]` 扫描); `8006B770` = 8 槽上限判定。
  - `8006B21C-22C` = 初始化清零 [43A4]/[43A0]/**[43A8]**+8×0x10 槽(与 RWATCH 观测的
    两清零写者 8005b754/8005faf0 区分: 后者是 memset 路过)。
- **翻案**: 楔20 "主循环等 [800D43A8] vsync 计数器" 定性错误 → 实为 **DMA 队列管理器**
  状态字。真因链修正: CD 通道 CB 注册表 [80076CE4+ch*4] **全 0 且无人写**(RWATCH 60M
  零命中) → NewDmaQueue 永不可进 → 43A8 恒 0 → CD DMA 队列不存在 → 无 CD 读(cds=0 一致)。
- 楔31 战役: 找 **CD 通道 open/CB 注册**服务(写 [76CE4+ch*4] 的路径; 候选=驱动 vtable
  InitDevice(80052690 族) 的 CD 通道分支或 rootcall B0 CARD/Dopen 面), 使其在 pad 链
  就绪后被调用。完成判据: [76CE4]≠0 → NewDmaQueue 到达 → 43A8>0 → cd cmds>0。
- 方法论钉: 活体 dump 区可能是数据池(nop 海+字符串), 静态文件图像对**未覆写区**仍有效;
  判"文件 vs 活体"谁真 = 先比对已知运行 pc 区, 分区对待。

## 楔31 (SESSION-170): CB 表动态构造钉 + 队列泵直调翻案
- 静态全扫(首 EXE 文件图像 + 已 dump 活体区): **无任何指令写 [80076CE4..CF8]** =
  该 CB 表是运行期动态构造(LIBSN init 自写), 写者代码在**二段解包区**——静态 hunt 死路,
  转活体 RWATCH 长线盯。
- 设备 init 环体正字(活体 8005F318 反汇编): memset 8×24B TCB@800D42E0 +
  **43A0 自环**(+1 mod 8, 产 43A4) = 设备注册环, 非 CB 注册。
- **翻案(实验门 RETAIL_FIREFULL=1, 默认关)**: 每 VBlank 直调队列泵 8005F348 →
  served 172→225 但 exc 1→**55** 风暴(epc=8004f458 已知点反复穿) = 泵不能以
  fire 上下文裸调(依赖 LIBSN 调度器完整上下文: 当前 TCB 指针/s 系寄存器组)。
  vblank_fire 已补全 32-GPR+hi/lo+cop0 保存恢复面(楔21 纪律), 泵体保留门控待
  上下文钉全再用。
- 楔32 战役: 泵的正确触发位 = 游戏自己的 ChangeThread/调度入口(80060708 族,
  wedge24 活体见 8005FB10 `jal 80060708`)——找游戏主循环里的调度调用点, 让
  主循环自然走到, 而非宿主强灌。
- 回归: 默认门北斗 172/exc=1、三国志2 exc=0; dev16/eng9/cpu16 全 0F。

## 楔32 (SESSION-171): WPC 精确 pc 计数面调通 + 主循环自旋环正字
- 新诊断面 `RETAIL_WPC=<pc>>4 的 5-hex>`(全步采样+前 8 命中现场打印 pc/npc/v0/ra)。
  三坑连破(全记录防重踩): ①采样块抢挂 else 链头吞掉常规跑法=零推进假 hits 0
  ②static-once getenv 时机错位 ③**target 键=pc>>4(5 位 hex), 传 6 位全 0**(整楔系列
  hits=0 皆此因)。hits 现可信。
- **主循环正字(推翻"pc 恒驻"错觉)**: 51080 自旋环真转(hits=1.7M/30M): 游戏主循环
  每圈 `jal 800519E4(GetTickWait-1)` → 比 [800D2AD0](BIOS tick 槽) → [800D2AD4] 自增
  → 60 帧节奏面 → `jal 80051D68`([50C2] 门, hits=850K, 新调用者 ra=80050A60) → idle 回环。
- SysInit(80060708) **实际执行 2 次**(WPC 06071 hits=2, 旧"零调用"是键错位): 跑完
  [76CE4] 仍 0 → CD 通道 CB 注册在 StartThread 之后, 线程未起 = 下一环。
- tick 常开注入: psx_dev_vframe 每 VBlank 写 RAM [800D2AD0]=vframes(psx_translate 双门,
  真机 BIOS vsync 服务等价); 本盘主环条件 `v0(GetTick 返回) < [2AD0]` 走 510c0 printf 路,
  注入不改变行为(安全常开, 其他盘可能受益)。
- 楔33: 找 StartThread 触发面(游戏 main 未跑? 对比 51074 jal 前的初始化序与 [80076D7C]
  =SysInit 置 1 后的消费方); WPC 用 5 位键。
- 回归: 八面 118P/0F; 北斗 172/exc=1; 三国志2 exc=0。

## 楔33 (SESSION-172): **[50C2] 语义终解 = ISR-active 标志** + 注入后主泵全活实证
- 全函数活体反汇编(80051EC0-8005204C)= **LIBSN VBlankIntCB(ISR)**: 11 源位循环
  (80051F20×0xb) → 每命中位 `nor` 清 I_STAT + `jalr` 回调表体 → 三条件与
  (I_STAT&I_MASK&[50F0]) → `[6158]` 心跳计数(<0x801) → `jal 800522F4`+
  **`sh zero,[50C2]`= ISR 退出清标志**。楔22 "失败清理" 与楔26 "idle 正常态" 均再修正:
  50C2=1 由真机 BIOS EntryInt 面置位(我们缺), ISR 体本身每帧在跑(RWATCH 清零实证)。
- **实验(RETAIL_ISR50C2=1 门控, VBlank 注入 [50C2]=1)**: 主泵门 51D68 首破 —
  GPU 同步函数 800503B4 调用 **794K 次**(轮 [1F801803]&7, 前恒 0), 主泵走通 50A68
  GPU 状态段(前未达)。渲染提交路 50AF8/51244 仍未到, NewDmaQueue(6B2C) 未达。
- 组合坑钉: ISR 注入 × CB/RP 链火组合 → 游戏 entry 路把 [50C0]=0x00010001 低字当指针
  解引用崩(epc=800750C0 cause=28 pc=10) = **注入只可单变量**, 与 hook 字重叠页写要防
  半字合并污染(写 C2 与游戏 sh 1 [50C0] 同字)。
- 楔34: 单开 ISR 注入深挖渲染驻点(50A8C beqz s0→af8 路 = 503B4 返回状态字 s0 位义);
  注入转正候选=与 [50C2] 语义等价的 EntryInt 序列模拟(需防 hook 字污染)。
- 回归: dev16/eng9/cpu16/hle20 全 0F; 默认门北斗 172/exc=1; 三国志2 exc=0。

## 楔34 (SESSION-173): 渲染真空根因 = **设备 Dopen 被抢置跳掉**
- ISR 注入单变量下 WPC 点位图: 503B4 入口 794K → 全走 `beqz→50900` idle 出口
  (1.06M hits, nibble=0 恒无包) ; 50408 nibble 变化路=0; 主泵 50A80 jal=0; 渲染提交=0。
- CALLTR 画像: `InitDevices(0x41a)` = 请求 7 设备, SysEnqIntRP 仅注册链 #2 一条。
  vtable 8 槽 WPC: +4/+8/+C/+10/+14 全跑过, **+0x18=80052238 恒 0 调用**。
- **80052238 = 设备 Dopen 正字**(活体反汇编): `[50C0]==0` 才走首次 open(B0 装填
  80052314 + `sh 1 [50C0]` open flag + `sh [5032]→[6150]` + `4F464→[6154]`);
  已开返 -1。**[50C0] 被游戏 entry 路(sh 1 @80051E40)抢置 1 → Dopen 永返 -1 →
  CB 注册([76CE4]) 与 DMA 分配(43A8 路)整链被跳** = 无绘制工作/CD 零命令的共同上游。
- 静态无 `jal 52238`(文件图像) = 触发方=动态区 driver open API(二段码)。
- 实验撤回记录: VBlank 追加派发口(fire_extra_fn=52238)时序不利(CB+RFE 组合有
  hook 字污染崩史 cause28 pc=10), 未验证即撤, 树保净。
- 楔35 候选: 主泵路径内(RFE hook 派发面)而非 VBlank 尾注入 Dopen; 或按真机序在
  coldboot 后、entry sh 1 之前让游戏自走 Dopen(找 entry 路 sh 1 的条件分支缺口)。
- 回归: 默认门 dev16/eng9/cpu16/hle20 全 0F(净树=楔33 态)。

## 楔35 (SESSION-174): [50C0] 消歧 = **1 是占位常量非 open flag**; 真 CB 在 [50C4]=800523FC
- 默认门活体 MEMDUMP: `[800750C0]=00000001, [50C4]=800523FC, [50C8]=0, [50CC]=80051858`。
- 楔34 "open flag 抢置" 假说**修正**: 游戏 entry 路写的 1 = 设备 CB 槽占位常量;
  设备 CB 槽对 = {[50C0]=1 标志, [50C4]=分发器}。
- **800523FC = LIBSN 设备事件分发器**(活体反汇编): 循环 8 槽表 `80076168..80076184`,
  `jalr [slot]` 非空即调, 每轮 `[80076188]++` —— **该 6188 正是楔30 钉的
  [800D43A8-0x800C4220 系内同族] DMA queue 分配计数器镜像族**; 8 槽 = DMA 队列槽。
- 下一钉(楔36): 8 槽表 [80076168] 活体是否全 0 + 分发器 523FC WPC 是否被调;
  若分发器零调用 = 泵 vtable +0x1C 路取 [50C0]=1 不当指针(消歧后安全);
  若被调但槽空 = 队列注册口(Driver 侧 EnqueueDeviceCB)缺位。
- 回归: 本楔纯取证, port 树净(楔33 态), 无编译改动。
- 楔35 补钉(同楔): **分发器 523FC 默认门已被调 4 次**; 8 槽表活体:
  `[76178]=800370d4, [76184]=8005d8b0` 两个 CB 已注册, `[6188]=1` 计数在走。
  → 设备 CB 链路**游戏侧本身已通**; 默认门真空的唯一外部门仍是 ISR-active
  [50C2]=0(楔33)——转正方向 = 在 VBlank 派发入口面置 [50C2]=1(等价 BIOS
  EntryInt 置位), 非再找 CB 注册缺口。楔36 = [50C2] 置位转正 + 八面回归。

## 楔36 (SESSION-175): **ISR-active [50C2] 转正** — 默认门主泵自由跑
- 转正面: VBlank 派发入口等价真机 BIOS EntryInt 置 `[50C2]=1`(游戏 VBlankIntCB
  8005202C 退出清零, 语义闭环)。双防误伤门(楔35 装机证据): HookEntryInt 指针槽
  `[50F8]` 合法 RAM 指针 **且** 分发器 `[50C4]` 合法指针, 二者齐才写; `RETAIL_ISR50C2=0` 显式关。
- 默认门北斗: **泵首次自由跑** — 50M vframes=89→200M vframes=355; pc 进 GPU
  sync 深段 `8005090C`; **gp0=4 gp1=2 首次 nonzero**(渲染口开始有字); fill/tri=0 待渲染体。
- 新单点楔37: 单次 `exc cause=20 epc=8004F458 ins=0000000c(syscall) t1=0x35 t2=0xB0
  ra=80051164` = B0:0x35 rootcall 走入异常面(非 served 路); 修后 GPU 命令流望续涨。
- 回归: 八面 8/8 PASS; 三国志2/NBA/Striker 30M exc=0 零误伤, Striker pc 前移至 80169760。
- port commit 见 git log; mirror 同步 ahead=0。

## 楔37 (SESSION-176): B0 syscall 异常面 = **双阶段 rootcall thunk**（t2 页派发未建模）
- SYSTR 正字: `vec=t1=t2=8005BEFC code=0 @pc=8004F458` (撤回本楔 vec&0xFF 折叠补丁——
  折出 0xFC 无语义, 树已净)。
- 活体钉 `8005BF24` 双阶段面: `sw ra,[D3E70]` → `jal 8004F454`(syscall 0 陷入) →
  返回后才 `addiu t1,0x56; addiu t2,0xB0; jalr $t2` = **真机第二阶段跳 BIOS
  KSEG0 页 0x800000B0 派发器**, 本核无该页 → 整链断, B(0x56)=GetVideoMode
  [PSn00bSDK b 表 id86] 未达(后文 `lw 0x70(v0)` 取 vmode 字段闭环印证)。
- WPC key=0 (pc=0x80000080 hits=0) = syscall 异常入游戏自装 handler 路**未跑**
  (LIBSN 面由 CARD init 装, 与 t2 页派发同缺口族)。
- 楔38 修法(已设计): t2∈RAM 页指针 + `jr $t2` 目标低 8 位∈{80,A0,B0,C0} →
  从 t1 或 caller 语义取号交表派发; 或建 4×256 向量页 stub 面(jr $ra+nop 族)。
  异常面本身良性(excite=1 后水位派发恢复, 泵照跑)。
- 回归: 本楔无转正改动, port 树=楔36 态(4eeafc6), 八面 8/8 基线不变。

## 楔38 (SESSION-177): **SWI0 老式内核面转正** — 北斗首入零异常稳态
- 活体钉 `8004F454/4F464` wrapper 族 = PSYQ 老式内核调用: `addiu a0,N; syscall`
  (code=0), **号在 a0, t1/t2 不参与**(本盘 t2=8005BEFC 为陈旧 jr-table 指针)。
  4F454 a0=1 / 4F464 a0=2 = Enter/ExitCriticalSection [S]; 旧 vec==0&&a0 面因
  陈旧 t2≥KSEG0 未命中 → 落异常。
- 转正: code==0 且 vec/cmd 均为 KSEG0 垃圾 → vec 折 0 交 SWI0 面。SYSTR 零输出。
- 默认门北斗: served 172→**173, exc 1→0, unknown=0** (50M+200M 双验); 泵稳态
  355 vframes, gp0=4/gp1=2 持平(渲染提交仍待 InitGeom 后置段)。
- 楔37 修正记录: 8005BEFC **不是 GetVideoMode 路**(阶段二 `t1=0x56;t2=0xB0;
  jalr` 走预放页 syscall→本核派发正常); SYSTR 陈旧值假象已闭环, 双阶段假说作废。
- 回归: 八面 8/8; 三国志2/NBA/Striker 30M exc=0 全持平。
- 楔39 单点: 泵稳定但 gp0 不涨 = 渲染提交段未达; 候选 WPC 钉 50AF8/51244 与
  Exec(A43)/FlushCache(A44) 后二段自解压完成度。

## 楔39 (SESSION-178): 热点终像 = **TCB tick 唤醒环独占** (10.7M/采样)
- RETAIL_HOT + WPC 点位: `80051A08-20` TCB 一致性读环 + `80051B48` + 主环 `80051130`
  三者同频 10.7M = **主线程全占在 GetTickWait(800519E4)**; `50A80` jal=0。
- GetTickWait 正字: `v0=[[80075050]]` 游标, `a1=[[80075054]]=1F801110`(Timer1),
  `bne` 复读等稳 + `[80075058]` 基差值, 出口 `80051B44/48`。Timer1 sync=每帧 per
  推进, 帧内两读同值环可退; 但 **62% 采样在环内 = T0 线程从未让位给任何后续线程**。
- CALLTR 全画像(首见 22 条): GPU_cw(A49)/InitGeom 族/Dopen 预放页(B0)/
  SWI0(a0=1)/GetCD 族(C0) 齐, **无 A0:00 open = 游戏从未请求读盘文件**
  (gp0=4 停在几何初始化后即止)。
- 楔40 方向: LIBSN 世代 = 多线程 TCB 引擎(北斗 [800D42E0] 表, 楔33 记录) ——
  主线程卡 tick 环 = **其余 TCB(渲染/CD 线程)无调度器驱动**; 候选面 = 泵/派发器
  在 vblank 边界轮询 TCB 表切线程上下文, 或钉 80051B48 出口后第一动作找调度残骸。
- 回归: 本楔纯取证, port 树=楔38 态(d7f30b4)。

## 楔40 (SESSION-179): **[D2AD0] 注入翻案撤除** + 主环控制流全图
- TCB 表假说作废: `[800D42E0..2FC]` 活体全 0(楔39 记忆错位)。
- GetTickWait 出口正字: `51B08-58` = 稳读 Timer→`[75058]` 基, 返 elapsed 差值;
  a0=-1 路返 `[[75050]-family]`... **主环 51088 条件 `[D2AD0] < GetTickWait返值`
  → bnez 服务段 510C0**。楔32 的 vframes 直投 = 消费者 cursor 被宿主反超 →
  条件永灭(510c0 hits=0)。**撤除注入**(仅留 RETAIL_TICKAD 显式门面)。
- 撤后实证: 主环到达 `51134 jal 51D68` 泵门(热桶 511E4=continue 1.07M,
  5112C 假 535K) —— 泵门在但返 0(服务段未达致)。服务段 510C0 内容 = 渲染
  工作体(`jal 800412F4/512F4` + s5 表 + [D2AD8])。
- 楔41 单点: 为何 `[D2AD0]([=0]) < 返值` 仍不成立 —— RETAIL_WATCH 钉 GetTickWait
  返回瞬间 v0 实际值 + [6188]/[75050 族] 活体; 假说=返值路读的槽恒 0(稳读基
  [75058]=1 > tick?) 或服务段游标 s2/s3/s5 未初始化。
- 教训(楔40): **WPC key 桶边界** `(pc>>4)` 对 0x...C0 端点 pc 归 0510C 前须验
  对齐; 本轮 0510c=0 为真零(非键错位)因入口 pc 恰=桶首——已复核。
- 回归: 八面 8/8; 三国志2/NBA/Striker 全持平 exc=0。port 4a? 见 git。

## 楔41 (SESSION-180): **渲染服务段全通** — 主环进游戏本体, gp0 真命令族首现
- 三轮翻案终定 [D2AD0]: WPCHIT(补 v1/a0 现场)钉得消费现场 `v1=cursor(0x3C1 初始.data)`
  , RWATCH 钉游戏侧只写 0(51034/5B754 自清) → **楔40 cursor 判定复证**, vframes 直投
  正是死锁根因。终面=宿主恒供 0(BIOS 消费面等价), `RETAIL_ZEROTICK` 实验面转正。
- 门破实证: 服务段 `510C0` 全链执行(`jal 800412F4` 首渲调 + `51934` 字符串道),
  主环跳出驱动层进**游戏本体 `8003F3F8-410`**(战役首次); gp0 4→**10**, 族分布
  `20800003 top3=1`(0x20 多边形族真命令, 非 NOP 海), served 173→181, exc=0。
- 200M 稳态: vframes=355 pc=8003F3F8 本体推进; fill/tri=0 = 命令体量仍小
  (启动画面级), 楔42 = 本体循环喂量(cd 读盘/更多 poly)。
- 回归: 八面 8/8; 三国志2/NBA/Striker 30M 全持平 exc=0 零误伤。

## 楔42 (SESSION-181): 本体主等待钉破 = **80060548 恒返 [800D43A8]=0**（楔30 链复活）
- 本体热环正字(`8003F4B4` 序, 活体 dump): 建状态 → `8003F530` 轮询环每轮
  `jal 8003F3BC`(= `jal 80060548` 取 v0 + `sll v0,0xC` 段算 + `jal 80063D88`)
  等返 1 → `bnez` 不出 = 游戏**主画面装配等第一 DMA 队列**。
- `80060548: lui v0,800d; lw v0,0x43a8(v0); jr $ra` —— 返回的正是楔30 钉死的
  **DMA queue slot allocation counter [800D43A8]**——活体复测 **=4@20M(楔41 后
  已动, 楔30 "恒 0" 结论过期)**: NewDmaQueue 已建 4 队! 本体仍等 `80063D88`
  返 1(非等 [D43A8] 非零, 其值仅作 `sll 0xC` 段算输入)。
- 链条闭环复活: 本体等队列 → 队列需 `NewDmaQueue 8006B27C`(前置 CB
  `[76CE4+ch*4]` 非空) → CB 注册在 CD 驱动 open/DMA 服务路径 = **楔43 主攻面**
  (Dopen 预放页派发已通, 队列已建 4 个)。楔43 主攻改为钉 **80063D88 返值语义**
  (本体等它 ==1; a1=拼合的 GP 显示状态字 0x2c8/0x300/0x2a6 族)。
- 本楔纯取证; port 树=楔41 态(9bb6685); 回归八面未动(无编译改动)。

## 楔42 补钉: 等待链终点 = **CD 状态块 [80076D94] 族, state([76D9C])需 2→1**
- `80063D88` 正字: `jal 80060974(a0=0)` 读 `[80076D9C]`(CD drive state) ==1 且
  `[D43A8]>0`(现=4 ✓) 才返 1。`80060800` init 面实证: 块=state/0xC=0xE/0x10=0x15
  (指令码)/+0x16 参数区 —— 现 state=2(ready), 转 1 需 **CD 命令状态机回执**
  (CDSTAT 响应→驱动 CB 写 state)。
- 即: 楔41 后本体已**首次向 CD 盘片服务提真实需求**——战役首个真盘 I/O 前置点。
  楔43 主攻: 本体请求发起方 `jal 80064028`(init 亦调) 钉字 + CD 命令口
  (1F801820)为何零到达(找请求触发残骸, 或 Dopen 后 CD 线程链)。

## 楔43 (SESSION-182): CD state 门实验 = **注入 1 无增益, 消费语义比静态图深**
- RWATCH 全量: 8M 内 `[76D9C]` 唯一写方=init `8006080c` post=0 → **楔42 记录
  "现=2" 再修正**: 2 是运行中段 `80060808` 族再写(前窗未覆盖), state 语义非
  简单 ready。
- 静态扫 EXE: 状态块访问器群 `8006C1C8-8006C290`(lbu 0x6d94/lw 0x6db8/index 族
  `lw 0x6d9c(a0*4)`) + 请求器 `8006C258`(`[76DC0]` 忙位 → `sw 0x20,[76D98]`
  → `jal 80060A38`) —— 全 EXE 仅一处直接引 `80076d94`, 块经访问器+指针参数扩散。
- 实验(RETAIL_CDSTAT=1 vblank 注入 state=1): 本体热环**不迁移**(HOT 仍
  8003F3C0-3F530 族, gp0=10 持平) → 3F53C `bnez` 退出条件是 `andi(ret)&0xffff
  ==0` 而 state=1 路返 1 = 继续环; 真退出需 **8003F3BC 的 s0 拼合字**(GP 显示
  状态 0x2c8/0x300/0x2a6 族 + [D43A8]<<0xC)整字归零 = 等的是**显示装配完成位**
  而非单纯 CD state。门语义反查转入楔44: 钉 `80063D88` 返值 0 的到达条件全谱
  (state==0 且 [D43A8]<=0? 或 s0 高位族清零), 用 WPC 8003F3F8 双值采样。
- CDSTAT 实验面保留门控默认关(零误伤)。回归: 八面基线未动。

## 楔44 (SESSION-183): CD 命令引擎完整图 — issuer **零调用**, 断点在更上游
- 命令引擎 `80060A38` 正字: `sb cmd,[76D80]` + 参数拷 `800617AC→[76D88]` +
  超时表 `80076DD0[state*4]`(现槽全 0, [DFC]=3/5/2 系它表) + `sw 0,[76DC0]` 投队列。
  **真正写 1F801820 的代码不在 EXE 静态像**(全镜像 `lui $t1,0x1F80`+sw 扫描=0)
  = BIOS CD handler 面(经 rootcall)或二段码 → 与楔27 `cd cmds=0` 归一。
- issuer 触达面: `8006C258`(仅 `jal 80060A38` 6 静态点: 6C180/C280/C53C/C56C/
  C694/C728 共享); 数据段无指针引 issuer 区(二段覆写前)。WPC 实证:
  `80060A38` 入口 **20M 零调用** → CD API 群无上游触发者。
- 本体环退出口径修正: `8003F53C bnez` 退出=返 0; `80063D88` 返 0 ⟺ state==0
  (RWATCH: init `8006080c` 唯一写方 post=0)。**state 需外部推进, 全链无写方**
  = 断点收敛为"谁推进 CD state"= BIOS CD 服务依赖面, 候选建模:
  (a) 本核 CD 服务面在 vblank 推 state 0→1→回执→0; (b) 找二段码里 state 写者
  (活体反汇编 `[76D80]` 区写者 WPC 布点)。
- 楔45 = 二选一先做 (b): DUMPHOT 活体扫 `lui 0x8007 + sw/sb imm 0x6d9c` 在
  运行后 RAM(非文件像)找二段 state 写者。

## 楔45 (SESSION-183 续): CD API 群 = **二段指针表调用, 静态零引用**
- `jal` 静态扫 `8006C000-8006D800` 全域 = **零调用者**; 全域数据字扫指针(文件像)
  = 零命中 → 该 CD API 家族(14 访问器 + 6 issuer 点 + 状态机 8006C6xx)完全经
  **运行期(二段自解压码)填充的函数指针表**调用, 与楔30"8006A-B 区被覆写"同源。
- 已证零触达面(WPC): `80060A38` issuer=0, `8006C6D8` 状态检查体=0,
  `8006C060` 主 API 入口=0, `8006CB50`=0 → 整个 CD 驱动服务层 20M 内无人调。
- 楔46 钉法: 活体全域 RAM 指针扫描(新诊断面 `RETAIL_PTRSCAN=lo:hi:target_lo:target_hi`
  输出含目标指针的 gpa)找二段 API 表基址, 再看谁遍历该表(=本体等 state 的上游
  真正消费者); 备选 = WPC 布点 8006D0F4/8006CEE0 等剩余 API 入口缩族。
- 本楔纯取证(实验面 CDSTAT 楔43 已入库默认关)。回归基线=楔41 态未动。

## 楔46 (SESSION-184): **CD 引擎=运行期 overlay 实锤** + PTRSCAN 零命中
- 活体 vs 文件像逐字对照(DUMPHOT base 修正后): `80060A38/8006C000` 族与
  `/tmp/hok_exe.bin` 经楔30 映射(vaddr=0x80010000+off-0x800, NewDmaQueue 锚
  file@5ba7c 复核仍成立)**几乎全不同** → 整个 CD 驱动引擎是**运行期载入的
  overlay**(二段/覆盖), 楔44/45 的"文件像静态扫零调用者"证明力收缩为
  "文件像内无 CD 码"而非"游戏无调用者"。
- 新诊断面 RETAIL_PTRSCAN(双门 gpa 窗): 全域 RAM 无 word∈[8006C000,8006D800)
  指针; 60 个 0x300 活体窗亦零值引用 → 调用方**经 lui+addiu 立即数自算址**
  (PTR 型扫描天然不可见), 且当前遍历者未运行。
- 楔47 钉法: (a) 找 overlay **载入时刻** —— RWATCH 布点 `[80060A38]`(首写 pc
  = 载入器, 亦是二段解压器本尊!); (b) 载入器即楔30 悬案的 8006A-B 覆写方,
  钉住它=同时拿到 overlay 来源与 CD API 调用线程。

## 楔46 补钉: RWATCH `[80060A38]` 6M 内**零 CPU 写** → 两假说待楔47 分辨
- (a) overlay 在冷启动装载期由**自研 LoadImage memcpy** 投放(RWATCH 只捕 CPU
  store, 天然静默) → 那 CD 区就是盘上 EXE 的一部分, 与文件像的差异 =
  **/tmp/hok_exe.bin 抽取本身错位/截段**(header 字段 PC=6021fd76 等是垃圾 =
  抽取起点可疑!)。
- (b) 真运行期 DMA/二段覆写。
- 楔47 第一步 = 从盘上按真 EXE header 重抽 hok_exe(对照 port 内建 LoadImage
  路径打印的真实 ddst/sz), 重做 live-vs-file 对照, 才许再引"overlay"结论。
  (楔44/45 的"静态零调用者"与本轮"overlay 实锤"**同受此疑**, 标记待复核。)

## 楔47 (SESSION-185): **证据链修复楔** — hok_exe.bin 抽取损坏, 楔44/45/46 静态结论全部作废重做
- 从盘直读重抽(PVD@16 CD001@+24, root lba22, SLPS_029.93;1 lba23 sz952320):
  真 header `PS-X EXE PC=8005B744 ddst=80010000 dsz=0xE8000 SP=801FFFF0`;
  旧 `/tmp/hok_exe.bin`(950272B, header 区垃圾)= **截断/错位抽取**, 已弃,
  金身 `/tmp/hok_exe_true.bin`。
- 三门全清: 冷装载即值 vs 真像 @60800 **192/192 全等**; 8M 后亦 **192/192** →
  **"CD 引擎 overlay"假说作废(该窗未被覆写)**; 楔44/45 "静态零调用者"作废——
  真像内 CD 引擎有 **14 个 jal 调用者**(5F71x/5FBxx/63Dxx 内核族)。
- 活体重测: `80060974` state 读=1.04M(热), `80063D88`=695K(被调), 但
  **issuer `80060A38` 与三个 state 写者 `80060E78/800611C0/80061764` 全零调**;
  写者上游调用族=60C3C/5F7xx/63D50 内核 dispatch 面。
- 楔48 单点(新干净基线): 真像静态扫 `[76D9C]` 写族上游全景 + 内核 dispatch
  残骸(谁调 80060EE0@80060C3C 与 5FB7C 族) —— 即"游戏内核从未走到发 CD 命令
  那步"的最后一环; 注意后续任何静态证一律用 hok_exe_true.bin。
- 教训钉: 抽取像必须验 magic(`PS-X EXE`)+header 域合法+size=目录项, 三缺一不配
  作静态证据源。

## 楔48 (SESSION-186): CD 队列泵**注册时序死锁**钉死 (真像基线)
- 分发器 `800523FC` 基址实证 = `lui 8007 + addiu 0x6168`(s0@52418), 扫 8 槽
  `76168..76184`, `jalr`@5243C 触发 **76 次**; 调用方=VBlankIntCB(ra=51F5C)。
- 8 槽活体非零三个: slot0 `76168=80060C10`(**队列泵**, wed47 经 PTRSCAN 找得),
  slot3 `76178=8003E25C`, slot7 `76184=8005D8B0`。
- WPC 实测各槽函数: slot7 `5D8B0`=8 次(早期注册, 被扫到), slot3 `3E25C`=0,
  **slot0 队列泵 `60C10`=0 次**——非零且排首位却零调 = **注册晚于分发停**:
  泵在分发器已停跑后才挂进 slot0, 此后再无 VBlankIntCB 全表扫。
- issuer `8006C258`/`60A38` 零调 → 队列永不投递 → `[76168]` 泵无活; state 写者零调
  → 本体门 `80063D88` 恒等不到 state 变。**断点=分发器不再全表扫**。
- 楔49 单点: 钉分发器 523FC 为何早期几次后停止(VBlankIntCB 内条件 51F00-51F5C
  段的 `I_STAT&I_MASK&[50F0]` 三条件与是否因某 mask 位不再置而短路);
  或主泵路径是否应独立触发一次 8 槽扫。用 WPC 0523f 随步数增长曲线 vs vframes。
- 纯取证, port 树=楔47 态(净)。回归基线不动。

## 楔49 (SESSION-187): **水位泵解绑 rootcall 时刻** — 主线程推进进 CD 引擎体
- 根因: 原水位派发只在 rootcall 尾部有机会执行, 而本体热环零 rootcall →
  帧 tick 从不派发 (分发器仅 init 期 6 次, 全为初始化)。
- 转正: `psx_hle_waterline_pump()` 抽函数(rootcall 尾同函数) + 探针帧循环尾
  vframe 边界外驱。实测: served 173→**247**, gp0 10→**13**, pc 进 CD 引擎体
  `800602F8`(wedge47 族区), exc=0; 八面 8/8。
- 新单点(楔50): VBlankIntCB 主门 `51F04 (I_MASK&I_STAT&[50F0])` 在**每次进入时
  I_STAT=0**(WPCHIT v0/v1/a0 全 0; [50F0]=0xD, I_MASK=0xD, 探针末 I_STAT=1 平素
  被 51F40 nor 清光)——per-frame ISR 15 hits 全来自 init 路(ra=51E20), 帧中
  VBlankIntCB 根本不被调(注册方=51E18 条件跳)。分发器 523FC 由 523DC
  `jal 51C44(a1=523FC)` 经 callback 注册面挂表; 楔50=钉"谁在帧中应调
  VBlankIntCB/或直接把泵接成每 vframe 调 523FC 等价体面"(高锚=523DC 注册语义)。
- 教训: 水位派发绑"陷入时刻"在纯轮询热环下=永不触发; vframe 边界外驱才是正面。

## 楔49b (SESSION-187 续): 外驱泵**证据门转正** — 北斗 200M served=686 gp0=70
- 楔49 初版无门致三国志2 派进失踪早退(2.1s 零输出实钉); 修正=**双路分离**:
  rootcall 尾路无条件(task-153 语义) + vframe 外驱路需驱动族装机证据
  ([50F8]/[50C4] 合法指针, 楔36 同款) 或 b17_hits。门方向初写反(`!external`)
  当场自查纠正(教训: 门条件写完必须反读一遍语义再编译)。
- 北斗 200M 终数: **served 181→686, gp0 10→70, exc=0 unknown=0**; pc 稳在
  `80051E0C`(VBlankIntCB init 区——泵已把主线程推入 init→帧循环大周期)。
- 三国志2/NBA/Striker 30M **逐字回到楔41 基线**(served 11/27/8, pc 同址)。
- 八面 8/8。楔50 单点不变: per-frame VBlankIntCB 到达面([50F0]&I_MASK&I_STAT
  短路) + 队列泵 60C10 槽位驱动。

## 楔50 (SESSION-188): EntryInt 面 IRQ 锁存正字 + 防挂死弹帧
- VBlankIntCB 主门 `51F04 (I_MASK&I_STAT&[50F0])` 零过根因=**派发时刻 I_STAT bit0
  已被游戏自读路吞清**; 真机语义=硬件 IRQ 在跳 EntryInt 前已锁存 I_STAT。
  转正=两路派发(rootcall 尾 + vframe 外驱)统一 `psx_dev_raise_vblank_irq()`。
  (初版只 external 置位 → rootcall 路 20/35 空转短路, IRQDBG 计数实钉。)
- 防挂死: 外驱派发后游戏陷纯轮询环不归还影子帧 → `pump_stuck` 跨 2 帧宿主等价
  弹帧; **stuck 计数不得在派发口复位**(两路交替清零, 第一版实错已改)。
- 40M 实测: served 247→296, **gp0 13→22, gp1 2→10**(GPU 命令双口起量), exc=0。
- WPC 面注意: **采样路(hot/wpc on)不跑帧尾外驱泵**(在 else 支)——WPC 数字对
  外驱机制天然偏低, 判外驱效应必须用默认跑基线对照。
- 残留(楔51): 队列泵 60C10/issuer 60A38 仍零调——CB 链 bit0 已过但 [76168]
  注册晚于 dispatcher 活跃窗; 候选=派发口顺带 jalr [76168..84] 非空槽(等价
  分发器常驻) 或钉 dispatcher 为何只 7 hits。
- 回归: 八面 8/8; 三国志2/NBA/Striker 20M exc=0 served 持平, pc 健康前移。

## 楔51 (SESSION-189): 翻案"队列泵零调用"——JHIST 地面真值 + 泵体 dequeue 门正字
- **翻案楔49残留**: "issuer/pump 零调用"部分是 WPC 键错位假象(`60C10>>4=0x08006`
  非 060c1; 修正键后仍 0 → 但 JHIST 临时面(钉 jalr@5243C 的 npc 直方图)实证
  **每轮分发都跳 60C10/3E25C/5D8B0**(三非空槽; 60C10 从第5发起常态命中)。
  教训: WPC 桶零必须先对"该址可达性"做 jalr 目标级地面真值, 再下零调用结论。
- 分发器 `523FC` 正字(真像全文反汇编): **单 deref** `lw v0,($s0); jalr $v0`
  直跳槽值(槽=函数指针, 非 CB 结构); 入口 `lw [76188]` 轮计数+1; 注册口
  `523A4` 把 523FC 经 `51C44` thunk 挂驱动表 `80076128`。
- **真单点=泵体 dequeue 门**: `60C24` 仅 `([76DC0]==1)` 才 `jal 60EE0` 完成处理,
  否则跳 `60C4C` 只减超时计数后按命令类型 `[76DBC]` 分发(1/2/0xB/0xC→60E3C)。
  现值 `[76DC0]=23` 命令积压、`[76DBC]=2`、`[76D80]=1`(GETSTAT)、state `[76D9C]=2`。
- 命令块写入者悬案: RWATCH(游戏 store 面) 对 `76D80..76E00` **零捕获**但值 2M=0→
  5M=0x1b→20M=0x17 递减演化, 静态镜像全 0 → 写者**不走 CPU store 路径**
  (rootcall host 直写面)——楔52 首要目标(候选: A0:72 DeviceDrv 口 a0=80076128)。
- 死锁闭环推断(待验): 真机 BIOS ISR 处理命令→置完成→泵 dequeue→计数回落;
  本核该处理面缺位 → 计数恒>1 → 永不 dequeue → 状态机永停 state=2。
- 会话内未改运行时代码(临时 JHIST 诊断面已回净, 树 0 diff, 八面基线不变)。

## 楔52 (SESSION-190): gdb 硬件观察点翻案"绕过 CPU store"——真卡点=state 槽永停 2
- **翻案楔51 写者悬案**: "命令块写者不走 CPU store"不成立。gdb 硬件观察点
  (host 指针级, 跨 KSEG 别名全捕获) 直接命中 `st_val(val=30)` 经 CPU 步执行,
  guest pc=80060AC0(**issuer 60A38 函数体内**, 非经入口 fallin)。RWATCH 漏检的
  真因=旧挂表时点在 loader 之后(装载/InitCdSys 段写全漏)+窗口边界必须与
  gpa 原值同制(KSEG0 全值), 归一化边界对比 gpa 原值=恒假。
- **翻案楔51 "永不 dequeue"**: 观察点序列=60AC0 投 30 → 60C38 递减 30→21,
  队列健康流动。**WPC 采样路不跑帧尾外驱泵**(楔50 已钉)同样毒化 CD 引擎路
  数字(60AC0 gdb 实锤 vs WPC 060ac=0), 凡 WPC 零须对默认跑+gdb 双验。
- **新单点钉死**: state 槽 `[80076D9C]` 全 8M 观察史只一击=InitCdSys `6080C`
  置 2(块 6D94: +0 命令字节=0,+8 state=2,+0xC=0xE,+0x10=0x15 初值), 之后再
  无 state 写。门 `60974`(state reader 104 万发)读 `[6D9C + idx*4]` 恒 2。
  真机 state 2→…→1 推进=BIOS CD 命令执行面; issuer 仅 GETSTAT 一发,
  后续命令投递者(609F8 post, 静态零 jal)未再触发。
- RETAIL_CDSTAT=1 恒置 1 实验致 cause=28(epc=801F0000)——state 与队列实态
  脱钩即崩, 单变量纪律复证; 须走"命令完成语义"正路而非值注入。
- 方法转正: **gdb host 指针观察点脚本模板**(break psx_dev_vframe → python
  psx_translate(d->core,gpa,&g_watch_lo) → watch -l → continue 循环打
  guest pc+val) 是本战役写者取证金标准, 完胜 RWATCH/WPC。
- 楔53 目标: 钉 issuer 为何仅一次(609F8/60AC0 caller 链活体) + 命令完成面
  (60EE0 完成处理器)触发条件; 八面 8/8 保持, 临时诊断面全撤树净。
