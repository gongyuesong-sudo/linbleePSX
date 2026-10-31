# 106 — ③战役楔3：payload 动态 trace 首通（task-125，SESSION-136）

## 1. 武器建成：winedbg --gdb + gdb 批管道
`printf 命令串 | winedbg --gdb demo_v2.EXE` = winedbg 自动 spawn gdb 并
直通 stdin（**pty REPL 与 `--command` 后 tty 等待均卡死，唯管道通**）。
能力实测：静态地址断点（`break *0x4010c2`）、**条件断点**
（`if *(int*)($esp+8)==7`）、硬件 watch（`watch *(int*)0x401124`）、
**内存 dump**（`dump binary memory`）。
坑入账：①符号断点 `MessageBoxA` 在早期不存在（user32 晚加载）=
`Function not defined` 级联后续命令全废 → **一律用 booter 静态地址
`*0x401117`**；②`continue` 后 target running 期间命令被拒 = 批脚本每步
命令须排在 continue 之后成对出现；③winedbg 无 `--gdbserver` 参数，
`--gdb` 即内置 gdb 前端；④watch 命中序=booter DEC/DEC/MOV 三连。

## 2. 二墙触发链钉死（三条硬证据）
1. **view 基址 = 0x12e0000**（CALL EDX 现场 `$eax` 实读，= 文档推定 ABI 印证）；
2. **g_error_code 0→6 单次写**（watch 实测）：payload 内部完成检测，
   返回 EAX=6，booter `0x4010C6 MOV` 一次写入——**无中间态**，
   检测函数在 payload 解压区内部；
3. **ProcessDebugPort 假说证伪**：`break NtQueryInformationProcess if
   class==7` 在 payload 全程执行窗**零命中**（上一楔"Wine DebugPort
   值面失配"现役假说被本楔条件断点直接排除——payload 根本没查这个
   class）。候选残余：PEB 直读 fs:[0x30]（但 pebprobe 全干净）、
   `int 2D/CC` 自触发时序、GetTickCount/RDTSC 时序、或其它 NQIP class、
   **或错误槽文案 ≠ 实际语义**（3..6 槽位由 payload 自定义，
   "debugger" 文案可能是别的失败落到该槽——docs/02 表本就是行为学推定）。

## 3. dump 战果：解压落地区确认（下一楔入口）
MessageBox 时刻（payload 已全跑完）dump 0x12e0000-0x1330000（320KB，
sha dae4d3dce1，`.analysis` 域不入 git）：
- **前 4KB 全零** + 0x1000 起仅 104/256 字与文件 .text 相同
  = **该区已非原只读文件映射**（PAGE_READONLY 不可能被改写）=
  payload 运行中 **unmap 后原地重映射/重分配 = 解压落地痕迹**；
- xref（0x401124/0x40142d 引用）零命中 = 检测代码在 dump 区之外/更大；
- 下一楔 ③W4 = **完整落地取证**：内存区扫描（0x12e0000±8MB 探测可读区）
  + 全量 dump → 离线反汇编找 `push 0x40142d`/`mov [0x401124]` 写点上游
  = 检测函数本体 = 二墙真语义终审。

## 4. 方法论
- 第 N 次行为学教训：**文案=槽位≠语义**，检测点未定前禁止把
  "debugger" 文案当真 debugger 检测立项（本楔 class7 零命中即此教训的
  直接代价=一整个假说周期）。
- gdb 批脚本模板已固化 `tools/wdb_payload.gdb`，③W4 直接续用。
