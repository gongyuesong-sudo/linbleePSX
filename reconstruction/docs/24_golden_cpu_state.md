# 24 — 金标准 CPU 态判读与终态直跳自洽（H-15a，SESSION-36）

## 1. Beetle(RetroArch) MDFNSVST CPU 区判读（tools/golden_state_parse.py）
- 状态流 = 多 zlib(0x78 0x9c) 段解压拼接（129 段/16MB）。
- 区条目：`len-byte + name + 0x80标志 + 7 pad + size(u32) + data`：
  `MainRAM.data8` size=0x200000 → data = 2MB RAM；**`GPR` 条目 = RAM 数据尾 +0xC2**
  （len=3 name+0x80+pad 8 → 128B = 32 寄存器 LE）。
- 字段条目：`len-byte + name + type(4,=4) + value(4)`（**值直接跟 type，无独立 size**）。
  PC = `BACKED_PC` 值（非 GPR 块内偏移）；NPC = `BACKED_new_PC`。
- 坑（实证）：解析器 magic 曾按 3 字节写致全链差一（`GS1` vs `GS1\0`）；
  `GPR` 名会命中 `SysControl.Regs$GPR` 前缀——搜索窗必须限定 RAM 尾之后。

## 2. 金标准终态语义（SCPH1001 + 空盘 autorun → CD 播放器 shell）
- `PC=0x8003D704 NPC=0x8003D708`（t9=PC = shell 空闲环）——**autorun 失败后 BIOS 进入
  自家 shell 循环**，正是本项目一直要到达的"引导完成面"。
- GPR 合理组：gp=0x801FFD00 sp=0x801FFF00 fp=0x80031544 s3=0x8005AE2C
  v0/a0/a1/s4=0x800E A7xx/A8xx 组(EvCB 等待块) k1=0xA0010FF0(HW 配置镜像)。
- `ra=0x044F4C02` = 空闲环内 lwr 半成品（shell 循环不用 ra，无害）。

## 3. 自洽证明（tools/golden_cpu_probe.c + host/build/golden_cpu_probe）
`GSTATE=<gstate.hdr> golden_cpu_probe <rom> 4000` →
`GOLDEN-CPU blocks=4000 pc=8003d6fc outside=0 halted=0`：
金标准 RAM+CPU 态在**本项目 JIT** 上直跳执行 4000 块无失败、全程停留金标准 PC 页
（循环体在页内合法游走）= 引擎(MIPS JIT)+金标准状态完整自洽。

## 4. 产物（取证区，不入 git）
- `.analysis/golden/gstate.ram`(2MB) + `gstate.hdr`(140B: magic"GS1\0"+npc+pc+gpr[32])
- 再生：`python3 tools/golden_state_parse.py <disc.state> <out>`（disc.state 生产见 docs/23 §1 / 技能 psx-golden-ram-dump-headless）

## 5. 复验
`bash tools/validate_host24.sh`（解析器再生断言 + 4000 块自洽 + 字段/判异落档断言）。
