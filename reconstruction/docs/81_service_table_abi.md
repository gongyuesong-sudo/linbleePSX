# 81 — x86 战线楔6：服务表 ABI 实证——0x4022a4 运行时语义与位引擎状态块（task-098，SESSION-107）

## 1. "thunk 表重建"的真身
`0x4022a4` = **PE IAT 首槽**（KERNEL32 ordinal#1，LoaderLock 时代自解压 loader 手填 IAT；
`tools/x86_iat_map.json` 119 槽全表落盘：KERNEL32×47/USER32×55/GDI×7/ADVAPI×7/COMCTL×2/SHELL×1）。
但**运行时语义 = 服务分派表**——二段 `0x55459` 是通用分派器：
```
55461: mov $0x4022a4,%eax ; add idx(%esp),%eax ; call *(%eax) ; ret
```
**索引调用 ABI**（VirtualAlloc/0x1000a 等全走 table[idx]）。

## 2. s3 对外面 = 单表 ABI（楔5"无明文 API 串"结案）
s3 全流 IAT 引用仅 **table[0] ×11**，且 11 处**全部聚簇在 s3 头 0x27-0x270**
= s3 自带的服务转发壳（模块 prologue 域）；其余 Win32 全走 37 处 `call *reg` 动态面。
**宿主引擎与 Windows 之间只隔一张服务表**——与 guest 侧设计同构(§4)。

## 3. 位引擎状态块现形（本楔最锐发现）
s3@0x26C-0x298 初始化序列直引 scratch 页字段：
`0x2A0000`(页基)/`0x2A0010`/`0x2A0020`/`0x2A0021`/`0x2A000D`/`0x2A0029`(字节指针)/`0x2A002A`。
二段解包驱动 `mov $0x2a002a,%ebx`(0x558AD) = **写状态块位缓存字段初值**——
docs/06 位引擎的**完整内存布局**（EBX 位缓存@+0x2A、源指针@+0x29、页基@+0x00…）
由两侧同址常量互证闭合。scratch 引用密度峰：s3 0x2c 页×10、0x0 页×8、0x5a 页×8；
reloc 槽 0x38427/0x4e3c7 → 0x2a1000/0x2b1000 (fixer 0x455845) = host↔C2 状态交换窗。

## 4. A3 对照闭环（楔6③交付）
A3 guest 侧低页服务槽（H_SVCLOW 0x100..0x1FF、EMITFAIL 0x10C 槽）=
本 x86 服务表的 **guest 侧投影**——两侧同构"表基址+索引 call"ABI：
guest stub→低页槽→view+0x55338→x86 table[idx]。宿主服务实现分层自此可逐槽对表。

## 5. 主循环状态（诚实）
未闭环。已剪枝判据：主循环必轮询 scratch 窗（0x2a0000-0x2bffff）——
密度峰 0x2c/0x5a 页与 s3 头壳是候选带；楔7=对三峰域做"回边+scratch 读"双判据扫描。

## 6. 坑(再+2)
- PE data directories 起点 = **OptionalHeader+96**（dir[1] import = +104），非 +8×N。
- 常量 `0x2a002a` 与字段地址 `0x2a0029/2a002a` 同值域两种身份——立即数扫描须带指令语境。
