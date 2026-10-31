# 06 — payload.bin 解密：PSX 引擎完整还原记录

日期：2026-10-25（北京时间）。对象：`rewrite/data/payload.bin`（= `demo-v1_4.EXE` 文件偏移
0xFA00..EOF 的 287,732 字节追加区）。目标：解密并反编译其中的 PSX 引擎。

## 结论（先行）

追加区不是强加密，而是 bleem 自研的**自定义位流 LZ 自解压包**（RCR 进位链 + 幻影位）。
全部 3 个压缩流已在原生 Linux x86 上用它自己的明文解压器解出，共 473,417 字节明文：

| 流头偏移 | 解压长度 | 内容 | 产物文件 |
|---|---|---|---|
| 0x0facc | 0x00BA3 (2,979) | 引导辅助模块（数据表+小桩） | `decrypted/stream_0facc_Lba3.bin` |
| 0x1015c | 0x10E7 (4,327) | 控制器模块（DirectInput/手柄映射，含 "bleem! Controller Settings" 等字符串） | `decrypted/stream_1015c_L10e7.bin` |
| 0x10a4c | 0x71CBF (466,111) | **PSX 引擎本体**（DirectDraw/DirectSound/GTE/GPU/CD-ROM，含 "DDRAW."、":\\PSX.EXE"） | `decrypted/stream_10a4c_L71cbf.bin` |

正确性证明（三条独立证据）：
1. 每个流解压结束时位源指针精确停在 `流头+4`（0x410a50 = img+0x10a50）——数据恰好耗尽，零字节错位。
2. 明文区 `[0x55338,0x55DF4)` 的 RAND 折叠校验和与块表 `0xFA58` 存储值 `0x4D24AF3D` 完全一致，证明校验算法与块表解析正确。
3. 解出的三个流头 dword0 是模块数据表指针（0xe/0x9e/0x322）、+4 处是 `pushad; mov esi,[eax+0x170]` 型序言，正文含大量可读字符串与 x86 代码结构。

## 包格式（完整逆向规格）

### 块表（追加区开头）
- 0xFA00: 一级引导桩 `lea edi,[esi+0xc]; ret`（置表基址=0xFA0C，同时充当首个"解压器"）
- 0xFA0C 起 16 字节条目：`{+0 dst_rva|bit0调用|bit1重定位, +4 依赖流偏移, +8 RAND校验和, +C 区域结束}`；
  区域首尾链式相接：`0xfab7 → 0x1014f → 0x10a4e → 0x55338 → 0x55df4`
- 区域字段是逻辑分界；真实流头以指针表（含 bit0=需解压/bit1=带重定位标志）给出，按 4 字节对齐向下取整。

### 流头（每个压缩流开头 8 字节）
- `dword[0]` = 解压输出长度 L
- `dword[1]` = 位源跳距 J；初始位指针 ESI = 流头 + J（流尾）
- 压缩数据占据 `[流头+8, 流头+J)`，位流从 J 处**向低地址**读取（每 4 字节一个 dword）

### 位引擎（逐指令 + 硬件 600 步指令流 trace 实证，2026-10-25 定稿）
- 状态：EBX 位缓冲、CF 持久进位、ESI 递减 dword 指针
- 树位消费（0x55787/0x557a4）：**先 SHR**；SHR 使 EBX 恰好归零时 → 载入新 dword + `RCR`
  ——**判决位 = RCR 后的新 CF（新 dword 位0）**，移出归零的那颗位被旋回 bit31 延迟重演
- `get8`（0x55960）：EBX≥0x100 时快路径 `AL=BL; SHR EBX,8`（位0=值LSB，CF=位7）；
  否则 8 位链（`SHR;ADC` 先进位=值MSB）。**8 位链入口空缓冲或 SHR#1 归零 → 0x5599F
  全字节特例：值字节 = 新 dword 低 8 位直取（保 acc 高位），EBX=(pend<<24)|(X>>8)**
- **链中 je-tail（0x559AC/C7/D0/D9/E2 及中段各位）：je 跳向对应的 ADC**——载入+`RCR` 后
  **新 CF 作为该位值参与累加**（"跳过累加"是错的；bit31 延迟位之后照常回放）
- 短链续读入口即 get8 链身：`call 0x55986`=续 4 位、`0x5598C`=续 3 位、`0x55992`=2 位、
  `0x55998`=1 位（与 fast 门控取值同值）
- gated 距离域：4B 匹配 10 位（门 EBX≥0x400，慢路 get8+0x55992）、3B 9 位（≥0x200 / +0x55998）、
  长匹配 12 位（≥0x1000 / +0x55986）；**匹配拷贝一律降序写字节（+N 先写）**，
  长匹配 REP MOVSB 在 STD 下从 [EDI+d] 向下逐字节

### 令牌树（解压主循环 0x5577A，硬件实证）
```
bit0=0 ─┬─ bit0=0: 短字面量串：3 位长度链(0..7)，写 len+1 字节 get8
        └─ bit0=1: 2 字节匹配：距离 = get8（降序写 [EDI+d+2],[EDI+d+1]）
bit0=1 ─┬─ bit0=0 ─┬─ bit0=0: 3 字节匹配（9 位直取 或 get8+1位链）
        │          └─ bit0=1: 4 字节匹配（10 位直取 或 get8+2位链）
        └─ bit0=1 ─┬─ bit0=0: 长匹配 len=get8+5；距离 12 位直取 或 get8+4位链（REP MOVSB 向下）
                   └─ bit0=1: 长字面量串 len=8+get8，写 len+1 字节
循环终止：EDI < EBP（输出基址）；字面量循环末尾走 0x55808 旁路（不经过 0x557a0 CMP）
独立实现：tools/native_unpack/unpack_lib.c —— 三流 473,417 字节与硬件执行金标准逐位一致
（selftest：0xfacc 2,979B / 0x1015c 4,327B / 0x10a4c 466,111B 全 PASS）
```

### 模块布局与装载
- 模块 = `{dword 数据表偏移, 代码..., 数据表}`；运行时入口 = 流内容 +4
- 装载器（明文桩）：汇总尺寸 → 两级 VirtualAlloc(RW) → 顺序解压 →
  重定位 = `FUN_000553CA` 块表驱动（16 B/条 @VIEW+0xfa0c：`test [reg+0x34],-1`
  自愈跳过、`add eax,[ebp+0x30]` delta、RAND-fold XOR 校验 vs `blk[+0xc]`）。
  **（2026-10-25 SESSION-6 证伪更正：不存在 `add esi,[esi-2]` 流前重定位表，
  也不存在 img+0x10068 指针表——该区域为指令码 `8b 01 01 00`。）**
- 引擎 API 派发（详见 docs/08）：`[0x4022a4]` 槽 0 直址 11 处（VirtualAlloc）+
  `call [eax+disp8]` 表调用 91 处；EAX = 装载器按 0x4011e0 名称池（0x4f=79 项，
  ShellExecuteA/KERNEL32.DLL/CloseHandle/CreateFileA…）解析后传入的指针数组。

## 引擎身份证据（解密后 154 个函数 / 523 条字符串）
- `DDRAW.DLL / DSOUND.DLL / :\\PSX.EXE / bleem! Controller Settings / FrameSkip / %d Hz / %dx%d %d-bit`
- 引擎入口 `+0x4`：三连 `VirtualAlloc(0x10000)` 于 +0x80000/+0x80000+0xa0000/+0x20000 区域
  （含 +0xa0000 = 640KB 段 —— PSX 主 RAM 尺寸）、结果写入控制块 +0x34/+0x38/+0x3c/+0x40 槽
- `FUN_0002a038`：同族 RCR 位流解压器（内层资产解压器，支持双向拷贝 bVar11）——引擎资源按需解压
- `FUN_0002d703 / FUN_0003ba8d` 等成组小函数：GPU 指令/状态机形态；数据表 0x322 处内嵌 "DDRAW."

## 工具（本次新增，全部原生 Linux，无 Wine、未执行任何加密代码——执行的仅是 2.7KB 明文解压器）
- `tools/native_unpack/scan2.c + tramp.s`：原生 harness。mmap 映像后在真 CPU 上调用
  明文解压器（0x45577a）暴力识别所有流并按位源耗尽判据落盘。`gcc -m32` 编译。
- `tools/native_unpack/needseed.c`：单流定点解压 + 校验反解。
- `bleem_unpack.py`（.analysis）：纯 Python 逐指令转写（用于交叉验证格式理解）。
- `decompiled/eng_*.json / mod2_*.json`：REA(Ghidra 12.1.4) 对解密模块的函数伪代码
  （快照 `.analysis/engine2-snapshot.json`，154 函数已枚举）。

## 未竟事项
- 重定位表位1链的逐条应用与再基址（当前按原映像坐标分析，引用自洽；若要跨基址重定位需逐条应用）
- 引擎 154 函数的全量反编译（已交付 12 个代表函数伪代码；快照可继续批量导出）
- 把引擎接到 bleem_port（Vulkan/X11+Wayland 宿主）需要实现其 Win32 API 依赖与 DDraw/DSound 后端——独立大工程
