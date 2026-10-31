# 100 — BIOS 战役楔8a（侦察楔）：GPU 呈现面前置勘察 = CDROM 驱动器模型才是第一道门（task-119，SESSION-130）

## 1. 本楔零语义改动——三个反直觉实证（默认转正态 2M 步）
1. **GPU 命令端口（0x1F801800/1810）零读写**——BIOS 无盘 idle 从未发起
   GPU 命令流。旧文档"GPU 探测 v0=a000b068"再判读：那是 **kseg1 RAM
   指针**（GPU 初始化参数块，指向 RAM 0xB068；golden RAM[0xB060]=
   0x80140000/[0xB064]=0x8014006C = GPU 环境块指针在案），不是 MMIO 读回。
2. **CDROM 命令/状态寄存器（0x1F801C00 系/0x1F801E00 系 XA）零读**——
   DEV_TR 全量仅 14 项（13W+1R：SPU 0x2040、XA 音量 0x1D80/84、RAM_SIZE、
   I_MASK、Timer 组、控制系），**BIOS 从未对驱动器发过命令**。
3. **I_STAT 零 trace 读 ≠ 无 vblank**——irq_check 直读 dev 字段不进 trace
   桶；EVTGEN 64 节拍在 step 头静默运转。195 步 syscall 周期 = BIOS 主循环
   A() 轮询节拍，vblank 中断通道实际在工作。

## 2. 卡点定位（呈现面的真前置）
BIOS 主序 ra=bfc07010 深段：0x6EE8 `jal 0x703C` 返回值判 ==1（`bne $v0,$1`）
= **等待"驱动器就绪"标志**——该标志在真机由 CDROM 上电复位中断链置位；
本引擎驱动器模型缺失 → 标志永不置位 → BIOS 停在 A() 轮询 idle。
**结论：GPU 呈现的依赖链第一环不是 GPU 模型，是 CDROM 驱动器应答模型**
——补应答后 BIOS 才会发 GetStat→无盘响应→进 CD player/画面绘制分支→
GPU 端口写流自然出现。

## 3. 楔8b 实施清单（GPU 呈现面三段式，按依赖排序）
1. **CDROM 设备堆最小面**（小楔）：`0x1F801C00-0x1C06`（RESP0-3/STAT/SEL）
   + `0x1F801E00-0x1E03`（XA 数据/状态）；上电即 ready；命令字节流
   state machine 最小实现（GetStat→0x11 no disc 应答 + INT1 触发）；
   验收 = BIOS 出现 0x1C00 系读写 trace + 0x703C 返回 1 + 主序推进
   （ra 锚迁移）+ dev 双态断言。
2. **GPU 命令/端口模型**（中楔）：0x1F801800 写命令 FIFO（GP0 包解析：
   0xE6340000 显示环境/0xC8/0x64 多边形）+ 0x1F801810 STATUS 真值
   （H 时代 0xB0000000 资产 + ready/VRAM 空位 bit）+ VRAM 1MB 后端；
   验收 = BIOS 绘制分支的 GP0 写流 trace + VRAM 指纹对 golden 区。
3. **呈现接线**（中楔）：VRAM→现有 Vulkan 呈现链（host46 双代硬件面已
   备），输出 BIOS 无盘画面。
工数评估：1=1 楔、2=2-3 楔、3=1-2 楔。风险点：BIOS 命令 state machine
细节多（真值靠 trace 驱动迭代，同 BIOS 战役方法论）。

## 4. 本楔取证工具增量
- watch/DEV_TR 已够用（GPU/CDROM 窗零访问本身就是结论）；
- psx_rom_dis.py 判读器本轮 4 处判读（6EC0 函数体/0x1C00+0x180 混称终判/
  0x703C 等待点/GPU 指针链）全部入本文，无新装备需求。
