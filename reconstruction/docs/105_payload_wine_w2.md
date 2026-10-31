# 105 — ③战役楔2：payload Wine 反调试二墙分辨（task-124，SESSION-135）

## 1. 对话框取证通道建成（可复用武器）
`tools/x11_win_dump.py`：ctypes 直调 libxcb `xcb_get_image`(Z_PIXMAP) 读目标
窗口自身 pixmap→手写 PNG。背景=观察通道三断（ffmpeg x11grab 在 wayland
合成下抓 X 窗口全黑；xwd/scrot/import/grim 全缺；winedbg/+msgbox 无效）。
四坑入账：`xcb_connection_has_error` 正名；`xcb_get_image_data/_size` 系
header-inline 须手工 `data=reply+24, len=length*4-24`；GetImage reply wire
`depth@1, line_stride@12(4B 单位, 本机报 0 需 fallback)`；尾行截断防御。
已用它读出两墙全文（下 §2/§3）。

## 2. 首墙已破：GetVersionEx 门 = 二进制补丁 v2
- 文案 `This program requires Windows95 or Windows98!`
- docs/02 指令级账本直接命中：`0x401033 mov 0x401144,eax; cmp 1; jne err`
  （`.text` VA 0x401000 = 文件 0x600，实测 section 表钉死映射）；
- **demo_v2.EXE** = NOP `jne`(6B)@file **0x63B**，sha 前缀 `d266056d`；
- 破墙后 MessageBox 变文案 = 执行流进入 payload 加密区（0x55338 stub）。
- 附带证伪：Wine10 `AppDefaults\Version=win98` 伪造（user.reg 写入在位）
  对 GetVersionEx 平台面**无效**；`--no-wow64` 不存在；新 32 位前缀同态。

## 3. 二墙判读：反调试报错 = 值面失配假说（原 wow64 bug 假说自我证伪）
- 文案 `This program is not designed to operate with a debugger!...`，
  relay 钉死调用者 = booter `0x40111c`（错误表槽 = payload 返回码 6 系）。
- **自审**：上一楔"CheckRemoteDebuggerPresent=1 恒真=wow64 bug"系**误读**
  ——打印的是函数返回值(恒 TRUE)非 out 参数。重写探针 `pebprobe.c`
  实测：`*present=0`、`PEB.BeingDebugged=0`、`NtGlobalFlag=0`、`DebugFlags=0`
  ——**Wine 环境所有真调试位全部干净**。
- **+relay 119MB 全量足迹**：`DebuggerPresent/OutputDebugString/
  NtSetInformationThread` 零命中；`NtQueryInformationProcess` 177 次全为
  Wine loader/wow64 内部（class 0x1a/3/7，ret 地址 6fff/7b 系内核区）。
- **真失配候选（现役假说）**：Wine 对 `ProcessDebugPort(7)`/
  `DebugObjectHandle(0x1e)` 返回 **0xC0000004 STATUS_NOT_IMPLEMENTED**；
  payload 自解析直调 ntdll 导出（绕过 thunk = relay 不可见），若其对
  查询失败状态判"异常环境"则触发本墙。Win98 原生 ntdll v4 对 class 7
  同样可能失败 = 需真值面表（下一楔动态 trace 判）。
- v3（错误显示层 NOP 放行）失败入账：NOP 区尾露 `.text` 后数据区 =
  执行数据崩溃——放行类补丁必须保留合法出口。

## 4. 下一楔（③W3）
payload 动态 trace：0x4010BC `CALL EDX`(view+0x55338) 后单步/断点抓
payload 检测逻辑与其对 ntdll 的直调目标（.analysis native harness 资产
可复用）；判出实际失配点后二选一：Wine 侧 ntdll 值面仿真 or 定点补丁。
