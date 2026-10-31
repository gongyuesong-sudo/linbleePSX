# 08b · bleem_port 引擎宿主设计（hosting）

前提事实（均有证据链，勿凭空扩展）：
- 引擎体 `stream_10a4c_L71cbf.bin`（466 KB）= bleem! PSX 模拟器核心，含 GTE/PSX CPU
  解释器形态函数群（docs/07 index 154 函数）；**解密+静态重定位产物已存在**：
  `decrypted/stream_10a4c_L71cbf_relocated.bin` + `reloc_report.json`（task-001）。
- API 派发（docs/08）：slot0 直址 `[0x4022a4]`×11 + `call [eax+disp8]`×91（表指针由
  装载器传入；79 名池声明链 @主 EXE 0x4011df）。
- 入口 `eng_0x4`：三连 VirtualAlloc（含 0xa0000=640 KB PSX 主 RAM）→ 控制块 +0x34..+0x40。
- 资源内层解压 = 同族 RCR 位流（`FUN_0002a038`），`unpack_lib.c` 已有逐位精确 C 实现。
- `bleem_port` 现有能力：SDL2 X11/Wayland + Vulkan 无着色器 blit、`--window` 帧循环、
  资源 UI 合成、payload 格式引擎（`--payload-selftest`）。

## 一、API shim 层（Windows→Linux）

引擎表在宿主侧由 port 构造（替代明文桩的装载职能），EAX=表基址传入引擎入口：

| 引擎槽 | 候选身份（按名池+频次） | Linux 实现 |
|---|---|---|
| slot0 `[0x4022a4]` | VirtualAlloc | `mmap(PROT_READ|WRITE|EXEC, MAP_ANON)` 常驻 arena（640 KB 对齐块）|
| +0x08 ×19 | GlobalFree/VirtualFree 类高频 | `munmap`/no-op arena 记账 |
| +0x54/+0x50 | CreateFileA/ReadFile 类 | open/read POSIX + 资源根重定向 |
| +0xe8 ×6 | GlobalAlloc/Lock 类 | malloc + 句柄表 |
| DDRAW.DLL `DirectDrawCreate/EnumerateExA` | DDraw v1 COM | 本文件 §二 |
| DSOUND.DLL | 音频 | SDL2 audio 后端 |
| `DirectInputCreateA` | 输入 | 本文件 §三 |
| 其余（kernel32 内存/时间/文件） | Win32 | libc 直映射 |

实现约定：shim 全部 cdecl 导出为 `void*` 函数指针填入静态 `bleem_api_table[79]`，
入口 `eng_0x4(relocated_img)` 以 `%eax=table` 调用（复用 `run_stub2` 蹦床，改 `port/src/host/`）。

## 二、DirectDraw → Vulkan 映射

- 引擎 DDRAW 用法（静态证据：模式串 `%dx%d %d-bit`、FrameSkip、DriverGUID）=
  独占/正常模式 + 翻转链 + Blt/Flip，无 3D（Direct3D 在 demo 中禁用、端口按开放策略仅需 2D）。
- 映射到现有 `vk_*` 路径：`DirectDrawCreate → vk_open(w,h)`；表面对象 = Vulkan buffer；
  `Flip/Present → vk_push(frame)`（现有 30fps 循环）；色深转换 CPU 侧 16bit→RGBA8
  （`ui_compose` 的 `uint32_t *px` 路径已验证）。
- Stage-1 不做着色器合成，blit 足够（与 selftest 同构，避免 SPIR-V 依赖）。

## 三、输入（DirectInput → SDL2）

- 引擎控制串 `bleem! Controller Settings` + GTE pad 结构（docs/07）。
- `DirectInputCreateA` shim 返回桩 COM：`GetDeviceState` 读 port 的 SDL2 键盘/joystick
  快照（`vk_alive` 循环内已有事件泵），映射到 PSX pad 位图（14 键 + 2 stick）。

## 四、分阶段路线（每阶段有可验证产物）

| 阶段 | 内容 | 验收标准 | 前置 |
|---|---|---|---|
| H-1 | `port/host/`：relocated img 载入 + `bleem_api_table[79]` 注入 + `eng_0x4` 蹦床调用 | 控制块槽 +0x34..+0x40 出现宿主 arena 地址（C 断言）| 本设计 |
| H-2 | 640 KB PSX RAM + BIOS 装载（ROM 数据在 blk1/blk2 流 0x8e8/0xe18 模块内）| 复位向量 0xBFC00000 区可读映射 | H-1 |
| H-3 | DD shim → `vk_push` 帧通路 | 窗口出帧（BIOS logo 需用户自备 ROM，仓库不分发）| H-2 |
| H-4 | SDL2 audio + pad 位图 + PBP/ISO 挂载 | 音频回调 + 输入注入到控制块 | H-3 |

| 风险 | 说明 | 缓解 |
|---|---|---|
| CPU 解释器正确性 | 154 函数仅伪代码级理解，未逐函数验证 | 以对拍测试驱动（PSX 指令级 golden）|
| GTE/中断时序 | 真机级行为差异 | 分阶段用商业游戏回归 |
| 版权边界 | BIOS/游戏资产不外发 | 用户本地路径注入，仓库仅代码 |

## 五、与 demo 限制解除的关系

端口 FULL-FEATURE 策略（README_PORT）在宿主侧同样生效：时限 gate（`DAT_00401124`
错误码链）不进入宿主路径；sound/memcard/color_fmv 由 shim 层直接提供实现。

## 六、证据文件索引（H 阶段直接引用）

| 文件 | 用途 |
|---|---|
| `decrypted/stream_10a4c_L71cbf.bin` | 引擎体（解压输出，金标准）|
| `decrypted/stream_10a4c_L71cbf_relocated.bin` | 重定位应用后镜像（H-1 输入）|
| `decrypted/reloc_report.json` | 2 fixer 槽位与 arena 分配记录 |
| `decrypted/stream_0facc_Lba3.bin` | 引导辅助模块（blk1）|
| `decrypted/stream_1015c_L10e7.bin` | 控制器模块（blk2）|
| `decrypted/decompiled/index.md` | 154 函数索引（GTE/CPU 定位入口）|
| `docs/06_decryption.md` | 位引擎/装载协议定稿 |
| `docs/07_engine_map.md` | 函数×字符串/API 依赖/内存布局 |
| `docs/08_api_dispatch.md` | 派发槽表实测（91 点/24 槽）|
| `tools/native_unpack/unpack_lib.c` | 逐位精确解压器（blk 流 + 内层资源共用）|
| `tools/apply_reloc.py` | 静态重定位器（可重跑）|
| `port/src/vk_host.c` | 现有 Vulkan 显示层（`vk_open/vk_push/vk_alive`）|

## 七、H-4 定案与 H-5 战略转向（SESSION-13）

**H-4 实证结论（validate_host4.sh）**：
- init 阶段（`img+0x4..0x2b7`）为纯 x86-32 原生代码，宿主服务协议齐备后**完整走完**；cb 句柄/窗口链（`+0x24/0x28/0x2c` scratch、`+0x34..+0x60` 分配与 BIOS 窗）全部写满
- 引擎推进到 **BIOS 复位向量边界 `0xBFC00000`** 后跳转执行——该页内容 = **bleem 的 PSX BIOS x86-JIT 机器码**（历史外部文件 `PSX.EXE` 的职责，字符串 `":\PSX.EXE"` 即此）

**H-5 战略转向（关键洞察）**：
- 引擎镜像内 **172 张调度表（img+0x5AC4C，步长 16）= PSX 指令 → x86 JIT 例程偏移表**——`stream_10a4c_L71cbf.bin` 实为 **bleem 引擎的自解压载荷**（含内层同族 RCR 位流解压器，运行时解出 JIT 体与 BIOS-JIT 到分配块）
- 因此端口路线收敛为：**原生 ELF 宿主 + 运行时自解压**：
  1. 宿主提供 cb 控制块 + 服务回调（已就绪，host32.c）
  2. 引擎运行时解出自身深层模块（JIT/BIOS-CPU/PSX 设备驱动）
  3. 解压目标 = cb 交付的句柄块（`+0x38..+0x60`）；BIOS 窗（`+0x44..+0x5c`）= 8MB 分配内 PSX 内存镜像
- 剩余依赖：仅**外部用户 BIOS ROM（512KB，合法自有）**——引擎 JIT 模板与全部模拟代码都在载荷内

### 7.1 修正（SESSION-14，blocks.txt diff 实证）

- **init 阶段引擎不写任何分配块**（控制块 pre/post diff = 0 字节）；"非零内容"系 arena C3 滑道填充假象
- 引擎到达 BIOS 复位向量后停在 `0xBFC00000` ret-sled：**BIOS-JIT 代码页生成需要真实 BIOS ROM 输入**（历史 `PSX.EXE` 即预生成 BIOS-JIT 工件）
- **H-5 修订路径**：
  1. 宿主加载用户 BIOS ROM（512KB，`BLEEM_BIOS` 环境变量），放入 cb+0x44 BIOS 窗块
  2. 引擎对 BIOS 页设 RX 保护（VirtualProtect）+ 以 `call [alloc]` 向块首跳入 → **宿主拦截 = BIOS-JIT 编译器入口**：扫描 BIOS MIPS 入口链，按 img+0x5AC4C 的 172 张 JIT 模板生成 x86 代码到该块
  3. 生成完成后 BIOS 页 EXEC 放行，引擎正常执行 BIOS-JIT → 进入 PSX 模拟主循环

### 7.2 H-5b BIOS 输入观测（SESSION-15）

- 全盘无 BIOS ROM（`find ~ SCPH*/PSX.EXE/*bios*.bin` 零命中；psgame 仅 1 个 PBP）。**真实 BIOS-JIT 路线要求用户合法提供 512KB ROM（`BLEEM_BIOS`）——法律与技术双重必要，不可伪造**
- **HLE stub 可行性已证**：BIOS 向量 `0xBFC00000` 置 ret-sled（返回 EAX=0）后，引擎**不陷入错误循环**，继续执行至 **162,658 条 trace 记录**（其中引擎地址段 `0x400000-0x4002b7` 指令 275 条，其余为宿主恢复路径与 arena/宿主段记录）——PSX 状态机在无 BIOS 内容时仍完成大量初始化写入（arena 深区 `0x40d11000` 等）；**深度数字主要反映 stub 容忍下的持续运转，而非纯引擎进度**
- 生产宿主 stub-sled 三区覆盖（BIOS 向量/低位伪 BIOS/arena·memtable call）+ cap 保护，init 干净返回（signal≤1）
- 引擎在 htrace 下 fatal 点 `eip=080495fe` = htrace 自身 C 地址——**引擎把宿主函数地址当代码跳**（宿主返回栈暴露给引擎；诊断工具现象，非引擎缺陷；生产 handler 已用栈扫描过滤）
