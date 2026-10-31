# 95 — BIOS 战役楔3：CDROM 负结果 + 金标对比修正 + 三件战役工具（task-114，SESSION-125）

## 1. 本楔是**诊断楔**：两个被推翻的假说 + 一个负结果（全部如实入账）
1. **假说 A 推翻（BIOS 未走到 CDROM）**：`DEV_TR` MMIO 访问直方图实跑
   20 万步——BIOS 设备足迹 = `0x1F802040`(SPU)×10、`0x1F801D80/84`(XA)×6、
   `0x1F801060`(RAM_SIZE)、`0x1F801000-1020`(控制系)；
   **CDROM 0x1F801C00 系访问 = 0 次**——"补 CDROM 应答即可推进"的楔2 预案
   不成立，BIOS 在 CDROM 对话前就进入无盘冷终态。
2. **假说 B 推翻（0x100-0x3FF 窗 RAM alias）**：曾按推论实现"BIOS 窗向量区
   读 RAM 镜像"——被两处证据否决（golden RAM[0x150]=0x6ee0 ≠ ROM[0x150]=0
   非复制关系；标准 J 语义下该假说不必要），**当日实现当日回滚**，
   translate 恢复全窗 ROM。
3. **金标对比修正（heatmap 反证）**：golden RAM vs 我方 RAM 分块差分显示
   0x30000-0x80000 差 72K 字——但那是 **RetroArch 运行中快照含 demo 程序体**，
   冷 boot BIOS 无权写该区——"BIOS 主初始化未复制"**不成立**。
   金标准对照必须先分型（冷 boot / 运行中），否则拿别人的中场当自己的开局。
4. **J 判读澄清**：手工判读器把 `0x0BF00054` 读成 jal 0x150 引发一场虚惊——
   真实 = **J 0xBFC00150**（26 位 idx 0x3F00054<<2），BIOS 走 ROM 合法
   向量初始化码区，**核的 J 实现一直正确**。判读器 bug ≠ 核 bug。

## 2. 本楔真交付（战役装备面）
- `psx_dev`：**RAM_SIZE(0x1F801060) 写后回读**（BIOS reset 探测依赖，
  eng e4b 断言）+ 控制区 0x1010-0x102C 写观测面；四面 61 断言双态。
- `--bios-run` 升级：`BIOSRUN_MMIOTRACE`(DEV_TR top-N 直方图) /
  `BIOSRUN_GOLDEN`(金标 rootcall 指针表装载 53 条) /
  `BIOSRUN_COLD`(金标 0x100-0x800 冷种子 112 字) / `BIOSRUN_DUMP`(2MB 全 RAM)。
- `psx_load_golden_vectors` / `psx_boot_seed_cold_vectors`（golden 派生运行期
  工具，golden 字节不入 git 约束保持）。

## 3. 现状定盘与楔4 方向
BIOS 当前落点 = **无盘冷 boot 合法终态**（ROM 初始化流 74k 步全走通：
RAM_SIZE/缓存/SKU 区写、GPU `a000b068` 探测、rootcall stub `rfe`；
随后 nop 漂流）。再推进需要**读解 BIOS ROM 主循环真码**（`bfc06xxx` 系）
判断它在等什么条件——那是 BIOS 码逆向战役（周级），不在薄引擎快速推进
线内。**楔4 建议**=转「BIOS ROM 主循环反汇编判读」侦察楔（REA/objdump 面，
产出"BIOS 等待条件清单"再决定补什么），或由用户改点候选面
（GTE 对拍/DMA/速度面/停线）。
