# 16 — SCPH1001 BIOS REA/Ghidra 反编译定案（SESSION-28）

工具链：ROM 封装为 **LE** ELF32 MIPS（`e_flags=EF_MIPS_ARCH_32R2(0x70000000)`，
p_offset 0x1000↔vaddr 0xBFC00000，附 51 符号 .symtab）→ REA `open_binary`（Ghidra 12.1.4，
language=MIPS:LE:32）→ `batch_decompile`。产物：
- `.analysis/bios_rea/SCPH1001_le.elf`（可重载封装）
- `.analysis/bios_rea/decompiled/SCPH1001_decompiled.c`（48 函数伪 C）
- `.analysis/bios_rea/evidence.json`（REA 证据束）

★ 教训：PSX R3000A 是**小端**；SCPH1001.BIN 文件即小端。曾按 BE 封装导致全图 `halt_baddata`。
REA raw-binary 不支持 MIPS；必须 ELF 封装 + symtab（batch_decompile 只认已存在函数）。

## 1. 引导链 `bios_reset_vector(0xBFC00000)→bios_init(0xBFC0150)→bios_init_tail(0xBFC03CC)` 全语义
1. IRQ/设备基表写入：`0x1F801000=0x1F000000, 1004=1F802000, 1008/1010=13243F, 100C=3022,
   1014=200931E1, 1018=20843, 101C=70777, 1020=31125`（= docs/15 dev log 观察值，逐一对应）。
2. `mtc0 Status=0x10000`（**SR.ERL=1**，向量基=ROM 镜像）→ 两遍 RAM 清零循环
   （8×u32/0x80 步进 + 32×u32/0x80 步进，[0x0,0xF80)）——即 htrace 画像的 `[0x2A0↔0x320]×34`。
3. 写 RAM `0x130`（经 0xFFFE0130 截断地址）三值：0x804 / 0x800 / 0xE988 = **rootcall/异常基数据**。
4. COP0 清寄存：HWREna/EntryLo1/PageMask/Wired/Count/Compare/Status/Cause=0（`mtc0` 面 = 我们
   JIT 新增 COP0 支持的依据；`mtc0 Status` 的 ERL 位驱动向量基切换）。
5. `[0xA0019000,0xA001C160)` 清零 = RAM lib/设备配置区。
6. tail：`1F801060=0xB88`（TIM0 mode）；**RAM 0x60=2、0x64=0、0x68=0xFF**；`1F801D80-86=0`；
   → `FUN_bfc06ec4`（lib 主 init：`FUN_bfc01a60(0xF/0xE)` 中断库两段、`FUN_bfc0703c` 机型判定
   →`FUN_bfc0711c`、`FUN_bfc06784` 收尾）。
   ★ RAM 0x68=0xFF 是 BIOS 固定写（"无已注册设备"掩码）——docs/15 的 STR 取证与本次反编译互证；
     printf 走 `0x3CE0` 设备扫描在无设备时跳哨兵是**冷启动真实行为**，不是我们的仿真错误。

## 2. A(0)-A(43) 表定案 = **BIOS 内置 C 运行库**（修正 docs/14 §5 的猜测）
- A(0)-A(9)：未实现默认（return 0）。
- ctype：A(10) char→digit 值、A(37) **toupper**、A(38) **tolower**（ctype 位表 @ROM 0xDDB1）。
- 数值：A(12) strtol、A(13) strtol 基变体、A(14)/A(15) abs、A(16) atoi、A(17) atol、A(18) sscanf 头。
- 内存：A(39) strncpy?（正向拷贝）、A(40) memset、A(41) memcmp、A(42) memcpy、A(43) memset2。
- 字符串：A(21) strcat、A(22) strncat、A(23) strcmp、A(24) strncmp、A(25) strcpy、A(26) strncpy、
  A(27) strlen、A(28)/A(30) strchr、A(29)/A(31) strrchr、A(32) strpbrk、A(33)/A(34) strspn/cspn、
  A(35) strtok（游标 RAM 0xA0009130）、A(36) strstr。
- A(19)：**寄存器上下文快照**（ra/gp/s0-s8 写入 ctx buf）＝异常/rootcall 现场保存；A(20) 恒等返回。
- A(11)→FUN_bfc02324(x,0)（lib 内部）。
⇒ 引擎模板调用 A(37)/A(38) 等 = 纯 ctype，MIPS 直接执行零障碍；docs/14"rootcall 停机面"结论不变。

## 3. rootcall 桩反编译确认
`rootcall_stub1/2 = addiu $4,$0,N; syscall; jr $31`（Ghidra 呈现 `syscall(0)`+terminal flow），
与 docs/14 §3 静态普查一致；`0xBFC0D570` 附近含 `mtc0` cop 写（根向量安装路径）。

## 4. 对 task-024 深段路线的定论影响
- warm 链 150 块停机 = 引导**正常**走到"无设备 printf"哨兵；继续推进需要的是 BIOS 之后的
  **设备注册**（libfs AddDevice / CD-ROM 驱动），即 CD 设备面——docs/15 预设的转 C 归档判据成立。
- JIT 侧本轮新增 COP0（mfc0/mtc0+Status/Cause/EPC，ERL 向量基切换）语义，方向被反编译证实。
