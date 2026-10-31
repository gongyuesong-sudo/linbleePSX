# 84 — x86 战线楔9：EXE 直用 Win16 API 七处 + wine-10 动态路线边界（task-101，SESSION-110）

## 1. 重大取证发现：Win16 兼容 API 七处（楔1 ordinal 解析 bug 就此勘正）
w1 解析器用 `fo+2` 跳 hint 未分 ordinal 标志——4 个 ordinal 被误解码成错位名。
正确解析（`tools/x86_imports.json` 已重写落盘，**119 全名**）：
KERNEL32 序数导入 = **ord#1 FreeLibrary16 / #35 / #36 LockResource / #37**，
名导入另有 **MapLS / UnMapLS / QT_Thunk**——**EXE 自身直用 7 处 Win95 16 位兼容 API**。
连锁实证：wine-10 解析这些 import → `find_forwarded_export delay loading krnl386.exe16`
→ **krnl386 loader_init 崩(0xC0000005@GetSelectorLimit16)** → 进程死在入口前。

## 2. win16 屏蔽补丁 = 进程复活（取证副本，原 EXE 只读恒 b61ee13e）
把 7 处导入重定向到池内安全名(Sleep/GetVersionExA/VirtualFree)后，
`demo_patched.EXE`(sha `3b3e110e`) 在 wine-10 **40s 存活无崩溃**(RC=124=timeout kill)。
补丁脚本可重放（ILT/IAT 双表 14 条目，本楔 /tmp 脚本已并入取证命令模式）。

## 3. 动态路线边界（三坑全中，落账防重烧）
| 尝试 | 结果 |
|---|---|
| `winedbg --gdb` + BP 二段交权行 0x553a0/一级 0x4010c2 | **BP 永不命中**——引擎稳态卡在 **COM 初始化等待**(ole32/combase/rpcrt4 载入后挂起 150s+)，一级 stub 的 CreateFile/MapView 链未走到交权 |
| gdb attach 活进程(pid 3623869) | **ptrace_scope=1 拒**(非父进程；"Could not attach") |
| winedbg `<pid>` / `--attach` 附着 | usage 无 --attach；`<num>` 报 error 87 |
结论：**Wine 动态一帧在 wine-10 + demo(需真实 CD/OLE 环境)上不可达主循环**；
静态/动态两路各自的坑已穷尽记录。

## 4. 楔10 定靶（改道 native，技能现成分支）
native -m32 stub replay（wine-pe-runtime-unpack → references/native-m32-stub-replay.md）：
宿主桩只用 11 个 Win32 API(GetModuleFileName/CreateFile/Mapping/MapView/Virtual*7/
Sleep/ExitProcess 型)——.analysis/ boot32/shims.s **stdcall 桩基建现成**，
重放一级 stub+二段块表解包(解包核已金标准验证)，观察 unpack 驱动对 dst 位图，
**不需要真实 DX/OLE**。主循环本体(依赖 DX 运行)判定为不可静态/半动态达——
A3 混合模型 + docs/80/81 服务表已给语义等价物；楔10 后 x86 战役可议收官。
