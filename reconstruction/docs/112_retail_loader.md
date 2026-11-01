# 112 — 零售盘装载面 (task-134, SESSION-140)

## 战役开端
用户提供自有游戏库 `~/bleem/psgame`（.PBP/.bin/.img/.chd + .bios 边文件）。
库分类侦查结论：
- `.PBP` = **PSP homebrew EBOOT 容器**（"00PBP"+PARAM.SFO+PNG+PSP ELF），非 PSX 盘 → 本核不可启；
- `.bios` 边文件 512KB = SCPH BIOS → **永不触碰**（HLE 主线）；
- `.bin/.img` = 真 PSX 2352 帧零售盘 dump（MODE2 头型, user data @ 帧+24）← 战役主目标；
- `.chd` 需专用解压 → 后置。

## 交付（全部零版权可入库；真盘只运行期只读）
- `psx_disc_open` 零售判型：PVD 候选探针 stride∈{2352,2048} × frame0∈{0,150} × data_off∈{16,24,32}
  自动定案（`d->data_off`）；cue/裸盘统一。
- **ECMA-119 正字修正**：PVD 根目录记录 @0x9C（ext LE@0x9E, size LE@0xA6）——
  旧自洽布局 0x86 只对自造盘有效，真盘不可见。
- scan_dir：记录 2B 对齐步进 `(rl+1)&~1`；"."/".."伪项不递归（曾致同盘重复枚举爆炸）。
- `psx_disc_boot_exe()`：SYSTEM.CNF `BOOT = cdrom:\X;1` **宽容空白**（真盘变体实证带空格）
  + `STACK =` 冷启 sp 基采纳；回落 PSX.EXE。files[64→1024]、目录扇区上限 4096。
- `psx_iso_build2`：参数化自造盘（stride/data_off/pregap/CNF），T5-T8 新面钉字。
- **真 Psy-Q PSX-EXE 头布局**（四枚零售盘交叉钉字）：`"PS-X EXE" | pc@0x10 | gp@0x14 |
  数据块 vaddr@0x18 | 块长@0x1C | SP基@0x30`；单块装载。旧自造双段布局废弃。
- `hle_load_exe_host`：冷跳大块直写 RAM 镜像 —— **2MB 折叠坑**：>0x100000 文件过
  SCRATCH(0x801F0000) 回绕毁低端（北斗神拳 952KB 入口页后全零实证）。
- **rootcall ABI 正字（历史级）**：cmd=**$t1(gpr9)**, vec=**$t2(gpr10)** ——
  首版误用 gpr8/9，自造夹具自洽掩盖；北斗神拳首异常 ring-buffer 回环 trace 一击定案。
- A(0x49) `GPU_cw` [R]：GPUSTAT 同步判 + a0→GP0 转发 + v0=0；B(0x5B) ChangeClearPAD [S] no-op。
- `--retail <盘>`（元数据）/ `--retail-probe <盘> [cycles]`（跑 N 周期→缺口报告：
  served/未登记号直方图/异常/GPU 计数；RETAIL_TRACE=1 首异常 16 步回环单步诊）。

## 真盘里程碑（用户私库，零入库）
`B 北斗神拳.bin` (SLPS_029.93)：519 文件枚举 → CNF BOOT 解析 → EXE 头
(pc=8005b744, 块 0xE8000@0x80010000) → 冷跳 → **3M 周期 exc=0**，
6 真 rootcall 服务 + 1 未登记（B0:0x19）。库内 NBA Live/打击者1945/三国志2 头型同验。

## 下一缺口梯队（task-135+）
1. VBlank/timer IRQ 节拍（游戏初始化后必等 vsync；dev 有 EVTGEN+IRQ 自动进入面，缺帧驱动接入）；
2. 未登记号按公开源逐槽定名扩面（B0:0x19 等）；
3. A 族热面续扩（GPU 库/GTE lib 走 EXE 内嵌居多，rootcall 需求随游戏而异）；
4. CDROM 命令面实战（游戏读盘命令序列 vs 我们的响应状态机）；
5. 更多盘型（多轨/CDXA/CHD）。

## 门
`bash tools/validate_host126.sh` ALL PASS (GATE_RC=0)：八面 112 断言 0 FAIL；
合成零售盘 probe 零异常入 halt 环；ABI/EXE 布局钉字 grep；真盘观测面 PASS；GitHub 推平。
