# 02 · 引导桩逐指令语义 (0x401000 / 0x401021)

Evidence: `analysis/fn_entry.json`, `analysis/fn_401021.json`
(dossier 含完整 259 条指令反汇编, 本文件是语义化转写)。

## 指令流关键序列

```
0x401000  entry: JMP 0x401021            ; 2 字节 thunk, 之后内联署名串
0x401021  PUSH 0x401134 / CALL GetVersionExA   ; OSLV1INFOA.cb=148 预置于数据
0x401033  CMP [0x401144], 1 / JNZ 错误出口     ; dwPlatformId==VER_PLATFORM_WIN32_WINDOWS
0x401041  DEC [0x401124]                       ; 计数器 g_error_code: 2 -> 1
0x401053  CALL GetModuleFileNameA(0, 0x4011E0, 0x104)
0x401077  CALL CreateFileA(path, GENERIC_READ, SHARE_READ, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL)
0x401081  MOV [0x401128], EAX                  ; g_hFile
0x401091  CALL CreateFileMappingA(hFile, NULL, PAGE_READONLY(0x2), 0, 0, NULL)
0x4010A8  CALL MapViewOfFile(hMap, FILE_MAP_READ(4), 0, 0, 0)
0x4010B1  MOV [0x401130], EAX                  ; g_pView
0x4010B6  DEC [0x401124]                       ; 1 -> 0 (ZF=1, CALL 保留标志)
0x4010BC  LEA EDX, [EAX + 0x55338]             ; 视图基址 + 追加桩文件偏移
0x4010C2  CALL EDX                             ; EAX 仍 = 视图基址 —— 寄存器传参
0x4010C4  JNZ 0x4010CB                         ; (DEC 留下的 ZF) ==0 才落下一句
0x4010C6  MOV [0x401124], EAX                  ; 桩返回值成为错误码
0x4010CB..  UnmapViewOfFile / CloseHandle x2   ; 逆序清理
0x4010EC  TEST [0x401124],-1 / JZ Exit
0x4010FA  CALL MessageBeep(MB_ICONHAND)
0x40110E  MOV EAX,[0x401124] / PUSH [EAX*4+0x4011C4]   ; 错误表索引
0x401117  CALL MessageBoxA(0, msg, "bleem, llc.", 0x11010)
       ; 0x11010 = MB_ICONHAND | MB_DEFAULT_DESKTOP_ONLY | MB_SETFOREGROUND
0x40111E  CALL ExitProcess(0)                  ; 成败都退 0
```

## 错误码状态机 (核心观察)

计数器 `g_error_code` 初值 **2** (0x401124 处文件数据), 只有两条 DEC:

| 路径 | 到达时的码 | 展示消息 |
|---|---|---|
| 非 Win9x / GetVersionEx 失败 | 2 | requires Windows95/98 |
| 路径/打开/映射/视图失败 | 1 | Error loading program! |
| 成功 | 桩返回值 | 0=静默; 3/4/5/6=内存/重装/系统调试器/调试器 |

3..6 **只能由桩(即 payload)返回** —— 是防调试与内存检查在密文区执行的
行为学证据: 明文引导器里没有任何调试器检查代码, 但错误文案与表槽已备好。

## payload ABI

- 入参: EAX = 自身文件映射视图基址 (无栈参)。
- 调用点偏移 = **文件偏移 0x55338** —— 打包器把“文件即镜像”当作地址模型。
- 返回: EAX = 错误码, 被引导器直接写回 `g_error_code`。
- 桩随后调用 `*(view + 0xFA60)()` (64000 的十进制常量, 见 0x55338 反编译),
  即进入**加密区**内的解压引导 —— 静态不可达。
