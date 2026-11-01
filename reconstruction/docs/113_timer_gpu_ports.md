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
