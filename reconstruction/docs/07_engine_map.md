# 07 · PSX 引擎体地图（stream_10a4c_L71cbf.bin）

数据来源：REA/Ghidra 154/154 反编译（`decrypted/decompiled/eng_all/` + `index.md`）、
131 条字符串（`list_strings`）、主 EXE 119 项导入（PE `.idata` 静态解析）、
二进制 imm32 交叉引用扫描。地址均为流内偏移（raw base 0）。

## 一、函数×字符串 交叉引用

方法：字符串地址的 LE-imm32 出现位置归位到包含过程 + 伪代码 `DAT_` 引用合并。

| 过程 | 引用字符串（样例行） |
|---|---|
| `0x1578` | `DirectSoundEnumerateA` |
| `0x11598` | `DirectPad Pro Controller` |
| `0x12c98` | `FX\ADV` |
| `0x13718` | `%dx%d %d-bit`; `%s (%s)`; `%s_GUID`; `Button L`; `DiscID  %010u`; `GlobalDosFree` |
| `0x2c298` | `Window_T` |
| `0x2ca84` | `Black&White` |
| `0x2d8e0` | `Button 0`; `Button 4`; `Thrustmaster Flight Control...` |
| `0x3d218` | `be using the sound card at ...` |
| `0x485b8` | `DDRAW.DLL` |
| `0x4c8d8` | `GetMonitorInfoA` |
| `0x4cc98` | `z-axis` |
| `0x59dcd` | `%s_ObjType`; `6/99 21:29:51`; `DirectDrawCreate`; `DirectDrawEnumerateExA`; `Microsoft SideWinder game pad`; `Mouse` |
- `0x59dcd` — sound-card 冲突提示 / 自动配置对话框文案（伪代码级引用）

- `0x338` `DDRAW.DLL`
- `0x342` `DirectDrawCreate`
- `0x353` `DirectDrawEnumerateExA`
- `0x11744` `Keyboard`
- `0x12650` `Mouse`
- `0x2c7bd` `Force`
- `0x2c7c3` `SelMode`
- `0x2c7cb` `Width`
- `0x2c7d1` `Height`
- `0x2c7e4` `Sizing`
- `0x2c7eb` `SavePos`
- `0x2c826` `LimitSpeed`
- `0x2c831` `FrameSkip`
- `0x2c86c` `Stereo`
- `0x2c873` `DriverGUID`
- `0x2c897` `Drive`
- `0x38d78` `DINPUT.DLL`
- `0x38d83` `DirectInputCreateA`
- `0x3cf28` `Right`
- `0x3cf2e` `Select`
- `0x3cf35` `Start`
- `0x3cf3b` `Triangle`
- `0x3cf44` `Square`
- `0x3cf4b` `Circle`
- `0x3cf52` `Cross`
- `0x48343` `MonitorFromPoint`
- `0x48354` `MonitorFromWindow`
- `0x48366` `GetMonitorInfoA`
- `0x4e3cf` `GlobalDosAlloc`
- `0x4e3de` `GlobalDosFree`
- `0x503f8` `DSOUND.DLL`
- `0x50403` `DirectSoundCreate`
- `0x50415` `DirectSoundEnumerateA`
- `0x568c5` `DisplayName`
- `0x568d8` `UninstallString`
- `0x5690e` `DefaultIcon`
- `0x59b63` `UNINSTALL`
- `0x59f7c` `Enter`
- `0x59fad` `Insert`
- `0x59fb4` `Delete`
- `0x5a00d` `Start`
- `0x5a013` `Shift`
- `0x5a1d5` `Standard`
- `0x5a1de` `Analog`
- `0x5a1e5` `Mouse`


> （完整 131 条字符串表为专有程序的衍生数据，本地 analysis 产物，不随仓库分发。）

## 二、Win32 API 依赖（0x4022a4 槽序机制）

- 引擎代码经 `_DAT_004022a4` 调 API：入口 `eng_0x4` 用 `table[0]` 连续 `VirtualAlloc(MEM_COMMIT|MEM_TOP_DOWN)` —— **槽 0 = VirtualAlloc**（装载器按引擎声明顺序回填真实导入地址，docs/06 §装载器）**
- 桩对主模块同样机制：`mov eax,0x4022a4; add eax,[esp+0x10]`（delta 槽）
- 主 EXE `.idata` 119 项（stub 可见依赖全集，装载器据此构建引擎槽表）：

| DLL | 数量 | 符号（截取） |
|---|---|---|
| USER32.DLL | 55 | `AdjustWindowRectEx`, `BeginPaint`, `CheckDlgButton`, `CreateDialogParamA`, `CreateWindowExA`, `DefWindowProcA`, `DestroyWindow`, `DialogBoxParamA`, `DispatchMessageA`, `EnableWindow`, `EndDialog`, `EndPaint` … |
| KERNEL32.DLL | 47 | `CloseHandle`, `CreateFileA`, `CreateFileMappingA`, `CreateThread`, `DeleteCriticalSection`, `DeleteFileA`, `EnterCriticalSection`, `ExitProcess`, `ExitThread`, `FindClose`, `FindFirstFileA`, `#36` … |
| ADVAPI32.DLL | 7 | `RegCloseKey`, `RegCreateKeyExA`, `RegDeleteKeyA`, `RegDeleteValueA`, `RegFlushKey`, `RegQueryValueExA`, `RegSetValueExA` |
| GDI32.DLL | 7 | `CreateFontIndirectA`, `CreatePalette`, `GetObjectA`, `GetStockObject`, `SelectObject`, `SetBkMode`, `SetTextColor` |
| COMCTL32.DLL | 2 | `#17`, `PropertySheetA` |
| SHELL32.DLL | 1 | `ShellExecuteA` |

引擎体内 DD/DS/DI 名字串（运行期自解析补充路径）：
- `0x338` `DDRAW.DLL`
- `0x342` `DirectDrawCreate`
- `0x353` `DirectDrawEnumerateExA`
- `0x38d78` `DINPUT.DLL`
- `0x38d83` `DirectInputCreateA`
- `0x503f8` `DSOUND.DLL`
- `0x50403` `DirectSoundCreate`
- `0x50415` `DirectSoundEnumerateA`

## 三、内存布局

| 区域 | 大小 | 证据 |
|---|---|---|
| PSX 主 RAM | `0xa0000` (640 KiB) @ VirtualAlloc 循环（块 0x10000，基址+0xa0000 偏移） | `eng_0x4` 入口三连分配 |
| 引擎模块目的 RVA | `0x60 / 0x8e8 / 0xe18 / 0x5a244` | 块表字段（docs/05/06） |
| 追加区视图 | VIEW 文件映射，块表 @VIEW+0xFA0C（16B/条） | docs/06 |
| API 槽表 | 模块坐标 `0x4022a4` + delta；scratch 视内 `VIEW+0x11E0` | SESSION-2/3 实证 |
| 桩 stage-2 基 | `[esp+4] + 0xFA00`（call-view-pushes-EAX：S+0=VIEW） | boot32 实证 loader 分支 |

入口链（已实证的 loader 协议）：主 EXE `FUN_00401021` 完成 `GetVersionExA → GetModuleFileNameA → CreateFileA(PAGE_READONLY mapping) → MapViewOfFile → call view+0x55338`，桩 prologue `stos EAX→frame[0]`、`delta=[frame回退槽]-0x4010c4→frame[4]`，stage-2 基=`frame[4]`+0xFA00。

