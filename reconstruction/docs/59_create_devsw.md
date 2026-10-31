# 59 — H-26 楔1：b0/00 创建服务真身 = printf 横幅链 + 广义零出口设计（task-074，SESSION-83）

## 1. 取证链（实测）
- `b0/00` 孤跑恒定 `115,871 块 → halt 0x40E4`，`EMITFAIL pc=0x3104 op=0 ra=bfc014e4 t9=0x30`。
- ra=ROM+0x14E4 现场 = **boot 横幅 putchar 内联宏序列**（`lw t9,[k](sp); jal 0xD880族; addiu a0,字符`），
  ROM+0xD880 区实测为 **rootcall thunk 池**（`addiu t2,0xB0; jr t2; addiu t1,0x3d|0x32|0x36|0x34`）——
  创建/init 服务自带 printf 依赖，cmd 0x3d=printChar 族（呼应 docs/53 自递归定性）。
- `0x3104` = RAM epilogue 零垫被当块头取指 = **零值 jr 目标/尸检返回地址**类死跳。

## 2. 否证表（全部实测逐字恒定 115871/0x40E4）
| 注入 | 结果 |
|---|---|
| FWDTTY（[0x64/0x68]→devsw@0x90000 重装 + TTYCHK 实证 in68=80090000/outfn 有效） | 不变 |
| +FWTTYSLOT（函数桩迁 0xB1D00 零页，避引擎 free-block 覆写 outfn=800b1d60 实证） | 不变 |
| +FWDTTY2（boot 栈缓存槽 [sp+0x64]/[-0x2c] ← fn） | 不变 |
| +FWDSERVE / +H_RAFIX | 不变（t9=0x30 不在 RAFIX [0xE00,0xFFF) 窗） |
→ t9 的**最终死值来源不在 devsw/已知栈槽**；EMITFAIL 是流末端，115k 块消耗在更早的 thunk 深链。

## 3. 设计结论：广义零出口钩（H_ZEROFIX）
死跳共性 = **jr/返回目标落在解引用零值**（0x30 字符码、0x0、epilogue 零垫、低页 thunk）。
RAFIX 的窗口式改道推广为**值域式**：块界检测 `jr 目标 ∈ 不可执行零区` → 宿主出口 v0=0
（真机旧 BIOS 对未装服务根call 的合法返回）。验收 = 0x00 转化 + 假阳性门（无钩基线逐字 +
全序 45/4a 恒 rc=0 + host59/60/62 全门）。

## 4. 宿主侧修正（本楔已做）
- FWDTcb/FWDLiveThread 注入区 0x9000→**0x9400**（0x90100 实测为引擎 free-block，曾被压）；
- QDUMP tcb0 读取位改随 `[0x89C44]` 动态解析；
- 新增 `FWDTTY/FWTTYSLOT/FWDTTY2/FWDTTYCHK`（devsw 重装+取证链，供未来实现复用）。
- **勘误纪律**：本文档初稿曾预填未实测的 §4 实验表（0x3D60/97531 为预测值），已整体重写为实测；
  此类"先写结论后跑实验"列入禁止（progress WARN 留痕）。
