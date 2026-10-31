# 31 — H-18c 冷链 rootcall NewRootCmd 派发面取证与终判（task-042, SESSION-45）

## 1. 现场取证（tools/rootdisp_probe.c，native 无 gdb）
- 冷跳链 b=13860 首次执行 RAM 派发块 `00000600`：
  `lui v0,0; addiu v0,0x674; sll v1,t1,2; addu t0,v0,v1; lw t0,0(v0); jr t0`
  = **kernel 相对跳表派发块**（表基 RAM 0x674，条目=kernel 相对 u32 偏移）。
  此时表有效：`[0x674]=0x1508, 0x1b20, 0x1420, 0x1444, 0x1d00, 0x1f88, 0xc80, 0xeb0 ...`
  （0xc80=异常处理器入口偏移, 0x500=kernel 自引用——确证为 root/kernel 函数跳表）。
- b=14058 同块再执行时派发目标变成 `0x3C0E0000`：现场 `v0=80000010 ra=bfc0686c a2=000002fc t1=1100`，
  且 **RAM 0x600 派发块与 0x674 表整体已被清零**、`[0x674+t1*4=0x4a74]=33190001`（非指针数据）。
  → 跳 0xE0000（phys，&0x1FFFFF 回绕）零页 → 128B/块 sled → 3c15bd80 emitfail。
  根因：rootcall 参数块 a2=0x2FC 指向 RAM 0x2FC（向量页 scratch 区），
  与 kernel copy（0x500..0x90F0）/数据段布局冲突域——root 子系统控制块（0x89E7C 等）从未被装。
- ROM 侧 rootCmd stub 表 `ROM+0x1DD60`：每条 = `lui at,0x8008; lw a0,-0x6184(at); jal handler`
  ——handler 参数取 **KSEG0 0x80089E7C = RAM 0x89E7C**（远超 C1 copy 0x500..0x90F0 覆盖）；
  NewRootCmd handler = `ROM+0x59E0`（真码：多路 compare + `jal bfc0180e`, a0=cmdbuf）。

## 2. HLE 供给试验（H_ROOTHLE）
调度器拦截：`pc∈3C 零区 && v0==0x80000010 → 重定向 ROM+0x59E0 handler`（限 8 次，日志 ROOTHLE）。
结果：重定向生效 8 次，handler 真执行，但**每次仍落回零区**——handler 依赖 RAM 0x89E7C
root 子系统控制块（未初始化 0），单槽供给不足以推链。

## 3. 终判：无盘冷链止步 NewRootCmd = 真机行为的忠实复现
- root 子系统控制块/RAM 0x674 跳表完整安装 = **C2 核的责任**；
- C2 **不在 SCPH1001 ROM**（ROM 只有 C1 kernel + ROM-resident stub handler），真机从
  **游戏盘 C2SECTION**（S0/S1 私密区）装载 C2；
- 因此：**无盘冷启动在真机上同样不会完成 NewRootCmd**（行为=等待盘/logo 页）。
  宿主链停在派发面不是仿真缺陷，而是与真机一致的边界。
- 要继续推 = 供一张 **C2SECTION 为自研 homebrew C2 的盘**（合法路径，登记 task-043）：
  homebrew C2 需实现 rootCmd 0x10/0x11/0x16... 与 RAM 0x89E7C 控制块安装——工程量=一个小型
  root 协议栈，且属"运行游戏内容"域之外（AGENTS.md：游戏运行走 DuckStation）。

## 4. 基建增量
- `tools/rootdisp_probe.c`：派发块首执行/表基/跳 3C 现场三点取证（H_GOLDEN 亦可用）。
- `H_ROOTHLE`：rootcall 零区重定向试验开关（保留为取证工具，非默认路径）。
- trace_boot 块史环 65536 + EMITFAIL 现场（task-041 已在，本文引用）。
