# 103 — BIOS 战役收束楔：vblank 位转正 + CPU 侧里程碑定案（task-122，SESSION-133）

## 1. vblank 位真 bug 修（guest 可见面）
- 现象/根因：EVTGEN 翻转置位 vblank 节拍用 **0x8**（docs/57 判读），该位
  系 **BLEEM 宿主私有语义混入 guest 可见面**；PSX 正字 I_STAT **bit0=VBlank**，
  真 BIOS 轮询 I_STAT（2M 步 3091 次读实测）等的就是 bit0。
- **判读更正（本楔自我审计）**：曾据"BIOS 轮询 I_STAT 3091 次"推出
  "轮询等 bit0"——该计数来自一次 **grep 空文件**（日志文件不存在、输出为
  错误文本被误当数据）的幻觉观察。重新实证：**2M 步 idle 的 DEV_TR 读侧
  仅 0x1074(I_MASK) x1，I_STAT 零读** ——BIOS idle 几乎不读任何设备寄存器，
  等待完全发生在 RAM 轮询面上。"轮询 vblank"假说不成立，撤回。
  bit0 修复仍保留，但依据改为两条实腿：①PSX 正字 guest 可见面 bit0=VBlank
  （外部双源）②旧 0x8 使 E1 清位断言恒真闭环自证（本楔已真验化）。
- 修复：EVTGEN 置位 `0x8→0x1`；dev D2 断言改名 `evtgen_64read_vblank_bit0`；
  eng E1 场景五位迁移（mask/poll/AND-clear）+ **初值改 0x1220**——
  bit0 不在初值=EVTGEN 置位路径实走、bit3 不在初值=清位断言从"恒真"变真验
  （旧 E1 的 `(i_stat&8)==0` 断言实为初值含 bit3+handler 清 bit3 的闭环自证，
  本次连带补强）。
- 断言面：四面 66（31/16/10/9）全绿。

## 2. vblank 单bit 实验记录（诚实入账）
bit0 转正后 BIOS idle **不解锁**（ra 锚仍 0x7010、2M 步 rom 段健康）——
**等待条件=复合**：polling I_STAT + 事件队列（被每周期清零环维护）+
GPU vcounter/DMA 时序中至少两者以上联动。单寄存器/单位的"钥匙式"假说
在 BIOS idle 面上**三度全灭**（命令应答→CDINT0→vblank bit），该战线
到此为 CPU 侧可解部分的终点。

## 3. BIOS 战役 CPU 侧里程碑定案（楔1..8b 全账）
可对外声明的完成面：真 SCPH1001 在本引擎上——冷 boot 30M 步零异常、
异常向量装载生命周期逐词判读、REGIMM/CMEXP/cache/RAM_SIZE/CDROM
寄存器面/I_STAT-vblank 正字全部落地、idle 事件循环全语义判读
（清零环+drain+内服链）、dispatch/handler 表与 golden 逐字全等。
**未达面（诚实）**：无盘黑屏画面输出——依赖 GPU 命令口/GPU DMA/vcounter
完整模型（估计 10+ 楔规模的大战役）。

## 4. 战略选项（交用户）
①GPU+DMA 大战役（PSX 图形栈从零，长期）
②BIOS 战役收线（CPU 侧里程碑封存，线仍不停 marker 在）
③**主战场回迁 demo payload**：本仓库初衷=BLEEM 宿主 demo v1.4 原生
运行——x86 战役已判明宿主码+guest 片段混合，薄引擎五面已备——
把 demo 的 host 引擎服务表(0x4022a4)+PSX guest 片段接到本薄引擎
五面上跑通，才是"port 可运行"最短路径。
