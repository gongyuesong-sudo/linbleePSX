# 23 — 真机终态 RAM 金标准获取与消费（H-13b attempt3，SESSION-35）

## 1. 金标准管线（可复现，无 root）
无头 `RetroArch(null video) + libretro-beetle-psx(Debian apt) + SCPH1001(作 scph5500 副本，SHA1 WARN 但跑 Sony 码)
+ 自制 2352 raw cue/bin(内含 loop PS-EXE+SYSTEM.CNF)` → BIOS 完整 boot(动画+autorun) 运行 30s
→ `retroarch --command SAVE_STATE`(**UDP 55355 网络命令，绕过 GUI 热键**) → RZIP 状态
→ 多 zlib 段解压拼接 → `MainRAM.data8` 区头(size u32=0x200000)后随 2MB = **终态 RAM**。
- 文件：`/home/gill/bleem/.analysis/golden/GOLDEN_RAM_2MB.bin`
  sha256=`5e8eb0c82feaa05f250a5f66dda4eeafdab1d737ac78adebd97634a610c28ef2`（**不入 git**，取证区）
- 环境链细节：Ubuntu apt `retroarch/libretro-beetle-psx/libchdr0/libtrio2/libvorbisidec1` 解包 `~/.local`；
  BIOS 需 `system/`(=GET_SAVE 根) 下 scph550{0,1,2}.bin；`network_cmd_enable=true`(注意 RA 退出会用内存值重写 -c 文件)。
- Beetle PS-EXE 头私有偏移：PC=+0x10 TextStart=+0x18 TextSize=+0x1C SP=+0x30，body 从 0x800 起。

## 2. 金标准判异（相对 docs/18/21/22 假设）
| 地址 | 真机终态 | 此前假设 |
|---|---|---|
| RAM 0x150 | **0x6ee0**(DCB 区) | root2ptr=0x80032E24(nocash 文档值) |
| RAM 0x68 | **0xFF 哨兵** | 我们装 devsw 指针(错) |
| RAM 0x80 向量页 | `lui k0,0x1a00` 指令序列(0x3c1a0000) | j 0xC80 stub(错) |
| RAM 0x130 | 0 | 0x1E988(那是 0xFFFE0130 窗早期值) |
| RAM 0x32E24 | **真 C2 kernel 码**(`lui v0,0x8008; lw v0,0x9f68(v0)`…) | 需复制装入(方向对,源未知) |
| RAM 0xB1D00 | **全零** | “设备指针表”(证伪：那是我们的未装海) |
| RAM 0x66940 | 0x37(DevCtl 终态) | 猜 0x3B |
| RAM 0xDE8-0xE0C | 真 kernel 设备遍历环(lb/lb/jalr) | — |

## 3. 消费实验（H_GOLDEN=<路径>，serve-defer 时整体覆盖 RAM）
- warm 链 206→**216 块**，BLK 尾首次进入**真 RAM kernel 码**(0x4A4C/0x4A80 返回桩、0xDE8 遍历环)，
  再次越过动画门(bfc02edc)。
- 新停机 `EMITFAIL pc=0xE09/0xF01`(金标准真码+1 字节)= **CPU 寄存器态(RA/SP/GP)与金标准不一致**：
  RAM-only 快照不足以复现——autorun 线程的 ra/sp 由当时 CPU 态决定。
- 终判：引导面推进的最后缺口 = **金标准 CPU 态**(PC/RA/SP/GP + COP0)。Beetle 状态含核心自有 CPU 段
  (MainRAM 区之外的其它 memdesc)，下一轮从同一状态文件解析 CPU 寄存器区即可闭环(HLE 直跳终态 PC)；
  或接受"链已证可达真 kernel 码"作为本项目引擎宿主链的收束证据(AGENTS.md 游戏运行=DuckStation)。

## 4. 复验
- `bash tools/validate_host23.sh`：金标准文件+哈希在场断言、判异表落档断言、
  `H_GOLDEN` 链块数 > 无金标准 HLEBOOT(206) 且 BLK 含 0x4A4C(真 RAM kernel 码执行证据)。
- 金标准重生产：本节 1 管线脚本 `tools/golden_ram_pipeline.sh`。

## 勘误(SESSION-44, task-041)
原文 0x4a4c/0x32e24 锚系 bug 语义链特定路线；新基线链执行 RAM kernel 入口 0x500/轮询环 0x5a8(a0000500 亦证)，
golden 链 142176 块、与无 golden 链同停 rootcall NewRootCmd 处理槽边界(golden 0x9E7C 槽同为 0)。详见 docs/30。
