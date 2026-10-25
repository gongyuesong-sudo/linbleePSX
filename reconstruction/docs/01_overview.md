# 01 · 二进制布局与导入面

全部地址为文件偏移或镜像 VA (image base 0x400000)。
Evidence: `analysis/overview.json` (binary_overview), `analysis/procs.json`,
`analysis/strs.json` (154 串全量), 手工 PE 头解析。

## 段表

| 段 | VA | 虚拟尺寸 | Raw off | Raw 尺寸 | 内容 |
|---|---|---|---|---|---|
| Headers | 0x400000 | 0x270+ | 0 | — | PE32 GUI, 子系统 4.0 |
| `.text` | 0x401000 | 0x4A0 | 0x600 | 0x600 | **全部可见代码**: entry(2B thunk) + FUN_00401021(259 条指令) + 内联数据/字符串 |
| `.idata` | 0x402000 | 0xC74 | 0xC00 | 0xE00 | 110 个导入 |
| `.rsrc` | 0x403000 | 0xDCE4 | 0x1A00 | 0xDE00 | 68 个资源叶子: 25 图标组/1 光标组/20 对话框/版本信息 |
| `.reloc` | 0x411000 | 0x54 | 0xF800 | 0x200 | |
| (追加区) | — | — | 0xFA00..0x55DF4 | **287,732B** | 压缩/加密 payload; 尾部 [0x55338,EOF) 为明文桩 |

## 可见代码极小的解释

`.text` 只有 1,536 字节、Ghidra 只定义 2 个函数, 但 `.idata` 有 110 个导入 ——
包括 PropertySheetA、CreateDialogParamA、GetThreadContext/SetThreadContext、
SuspendThread/TerminateThread、MapLS/QT_Thunk (Win95 16/32 位 thunk)、
FindFirstFileA、VirtualAlloc 等。解读 [推断·高置信]: 链接期对象 (引擎/UI 代码)
事后被打包器整体搬进追加区并压缩, 只留下引导桩和未清理的完整导入表。
payload 在运行时**不使用**这份 IAT(映射视图里 IAT 是未修复的文件原值),
而是走自己的 API 编号派发 (见 docs/03)。

## 导入清单 (按 DLL)

COMCTL32(1: PropertySheetA) · ADVAPI32(7: RegCreateKeyExA/RegQueryValueExA/
RegSetValueExA/RegDeleteKeyA/RegDeleteValueA/RegFlushKey/RegCloseKey) ·
GDI32(7: 字体/调色板) · SHELL32(1: ShellExecuteA) · KERNEL32(40) · USER32(54)。

注册表 7 件套 = 配置持久化 (`HKCU` 下 bleem! 设置) 的静态证据;
GetThreadContext/SuspendThread + 两条 "debugger" 错误文案 = 防调试器证据。

## 字符串区 (全部在 .text, 引导桩自用)

0x401002 `bleem! written by Randy Linden`   ← entry 后内联彩蛋
0x4012E5 `bleem, llc.`                       ← MessageBox 标题
0x4012F1 `This program requires Windows95 or Windows98!`
0x40131F `Error loading program!`
0x401336 `Not enough memory to load program!\n\nClose other applications and try again.`
0x401382 `Error loading program!\n\nPlease reinstall and try again.`
0x4013BA `This program is not designed to operate with a system debugger!...`
0x40142D `This program is not designed to operate with a debugger!...`

错误指针表 0x4011C4..0x4011E0 (槽 [1..6] → 上表 [1..6]), 初值与流转见 docs/02。
