# 101 — BIOS 战役楔8b-①前段（精判楔）：等待点再定位 = CDINT0 复位 ACK，非命令应答（task-120，SESSION-131）

## 1. 本楔性质：侦察+精判（零语义改动）——楔8a 设计的一次现场修正
楔8a 判定"呈现面第一道门 = CDROM 驱动器 ready 应答（GetStat 命令）"。
本楔 30M 步现场实证该表述**不准确**：

1. **CDROM 命令寄存器全程零写出**（watch 0x1F801800-0x1F801830 正字窗
   ——含 0x1820-1827 数据/STAT/SEL ——30M 步 0 事件）：BIOS 从未发
   INIT/GetStat 命令。旧文"0x1C00 系"= expansion ROM 窗混称，正字
   **CDROM = 0x1F801820-0x1F801827**（入账勘正）。
2. **A(0x20)=InitCD 已被主序调用**（0x6F44 `jal 0xDB40` stub，t1 组 8/
   0xC0 向量）——InitCD 不是没跑，是**跑进了等待**。
3. **主循环真身判读**（0x6FA4/0x7010 系 dis+TR 双证）：
   `jal 0x2B50`（64KB half→byte 转换例程）→ `jal 0x1920`（A(0x804)
   cache/RAM_SIZE 内服）→ `jalr $t6(0x8003xxxx)` = **事件队列回调 drain**
   ——idle = "转换+轮询+队列排空"自洽循环，非死锁。
4. **内存面与 golden 全等**：RAM[0x220]=0x4024、dispatch 表 RAM[0x870-0x8A0]、
   handler 码 RAM[0x4020-0x4040] 与 GOLDEN 逐字一致——引擎装载链无缺陷。

## 2. 等待点终定位
InitCD handler 等的"驱动器就绪"= **CDROM 上电复位完成中断 CDINT0
（I_STAT bit2）**——真机时序：驱动板上电 → ~百 ms 内自发 RESET ACK →
BIOS CD handler 置计数器/队列 → 主序继续发 INIT/GetStat → 无盘响应 →
CD player 分支 → GPU 绘制。**本引擎无该中断源 → 计数器永不增 →
主序停在轮询 idle（=当前观测）。**
外部事实核查入账：SCPH-1001 无盘开机**自动进 Music CD Player 画面**
（consolemods/tonyhax 双源），确证绘制分支存在且依赖 CD 驱动枚举。

## 3. 楔8b-①（实现楔）定稿清单
1. psx_dev 增 CDROM 块：上电计时器（~0x30000 周期量级）到点置
   I_STAT bit2 一次性（复位 ACK）；0x1F801820-1827 寄存器面（STAT ready
   位 + SEL 读回 + 命令字节 state machine 最小：INIT→ACK、GETSTAT→0x11）。
2. 验收链（全部可 machine 判定）：
   a) DEV_TR 出现 0x1820 系读（BIOS 开始枚举）；
   b) 主序过 0x6EF4/进入更深处（ra 锚从 0x7010 迁移）；
   c) 事件循环出现 GPU 端口(0x1800/1810)写流（CD player 绘制启动）；
   d) dev/eng 双态断言（无 ROM 面守恒）。
3. 工数：单楔可完成（估计 40-60 分钟档）。GPU 命令/VRAM = 楔8b-②不变。

## 4. 本楔取证增量
- watch 正字窗 1f801800-1830 一次性覆盖 GPU+CDROM 全族（已建）。
- RAM golden 对表法（dispatch/handler 表逐字对）= 新标准动作，入本楔
  第 1.4 节实证；后续任何"表未装载"假说先跑此对表再立项。
