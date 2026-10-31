# 17 — 设备注册/rootcall 派发面静态破译（H-12a，SESSION-29）

数据源：REA/Ghidra 反编译 v2（tools/wrap_bios_elf.py，65 符号）。本文 = task-025 交付。

## 1. `dev_command_walk` @0xBFC03CF0 = root-vector 帧跳板（完整协议）
```
frame(a0): +0x00 目标函数指针(=RootFunc 入口)  +0x04 gp 值
           +0x18 缓冲区  +0x1c 长度(逐 4B 清零)
           +0x20/+0x24 新栈指针/基
           +0x28 sp  +0x2c s8  +0x30 gp  +0x34 ra  +0x38 s0 (保存/恢复区)
流程: 保存 s0/ra/sp/s8/gp → 清 buf → 切栈 → move a0,a1/a1,a2 → jalr [frame+0] → 恢复 → v0=1
```
⇒ rootcall cmd=1 的真身 = `devCommand`：以 **RAM root 向量帧** 为参。链在 3d58 `jalr [frame]`
跳进 0x68 垃圾 = **frame 指针本身取自未初始化 RAM 配置区**（值=0x68=表槽地址）。

## 2. RAM 低 512B 语义定案（bios_init_tail 写入 = 真机行为）
- `RAM 0x60=2、0x64=0、0x68=0xFF` = **设备号分配位图**（0xFF=全设备掩码），非函数指针。
- `RAM 0x100-0x1FF`（经 0xFFFE0100 截断地址写）= **ExceptionVectorBaseData 区**；
  `sw 0x804/0x800/0xE988 @0x130` = rootcall/异常处理器偏移（0xE988 → ROM 0xBFC0E988 handler）。
- RAM 0x80-0x1BF = BIOS root 向量表槽（0xa0/0xb0 见 §3）。安装者=BIOS 异常分发链，**需要
  syscall 异常真跳转**（我们此前 serve pass-through 绕过了它）。
- `model_probe(0x703C)` 读 RAM 0x84（0x1F000084=KSEG1 RAM 镜像，我们 phys 折叠正确）比对
  "Licensed by Sony..."（0x1F000084 与 ROM 版权串区）→ 机型分支。

## 3. rootcall 派发与向量槽间接调用
- `rootcall_site_a(0xD570)` = cmd1 本体：`FUN_d8c0(syscall); if(RAM DFFC){d72c;d7bc;} FUN_d960(syscall); dev_command_walk(...)`
- `FUN_bfc0d970 = jal [RAM 0xB0 槽]`、`FUN_bfc0d950 = jal [RAM 0xA0 槽]` = **root 向量槽间接调用**
  （0xF0000003 = "DelayDevice/默认设备命令"参数形态）。
- TTY 核心（0x593C/0x5558/0x57B0）= **打印机口状态机**：读写 `0x1F801800/0x1F801801`
  （phys 折叠入 RAM 0x801801——设备窗与 RAM 折叠在 dev trap 里已被优先拦截），状态存 RAM
  0xA0009154/0x158/0x1c4/0x1c8（DevCtl），并频繁 `FUN_d970(0xF0000003, ms)` 走设备延迟槽。
- `libinit_dispatch(0x1A60)` = 写 GPU 数据窗尾字节 `1F802041=param` → FUN_3990。

## 4. `init_finalize(0x6784)` = BIOS 自动运行 —— CD 面铁证
```
strcpy+strcat 拼 "cdrom:SYSTEM.CNF;1" 与 "cdrom:PSX.EXE;1" → FUN_bfc067e8 (打开+执行)
```
BIOS 引导链终点**必然**访问 CD-ROM 驱动 ⇒ 引擎若驱动 BIOS 到 logo/exe 加载，宿主必须提供
CD 设备面（或预置设备表绕过自动运行）。这是 docs/15 预设"C 归档判据"的决定性静态证据。

## 5. 对 H-12b 的实现处方（task-026 输入）
1. **syscall 异常真语义**：JIT syscall 终止 → PC=RAM 向量槽 0x20/0x28（BEV=0 时）；
   槽未装（=0）→ ROM General handler **0xBFC0E988**（RAM 0x130 值佐证）。
2. 移除/降级 rootcall serve pass-through（让 BIOS 自己的异常分发建 root 向量帧）。
3. 期望：链越过 3d58 哨兵（>150 块），抵达 RAM 向量表安装完成态；再观察 §4 的 CD 请求点
   决定是否可停在"logo 就绪"而非死机。

## 6. SESSION-30 EXN 落地记录与 4199 块机制全解
- **实现**（host19 PASS，链 150→4199）：
  1. `0xFFFE0130` = **ExceptionVectorBaseData 硬件寄存器窗**（非 RAM 别名；掩码陷阱：`&0xFFFF0000`）
     → JIT 增 `exvbd[]` 寄存器组 + 读写 trap；serve EXN 从窗读 handler。
  2. `trace_boot` 原 inline pass 绕过 serve → 统一走 `jit_rootcall_serve`（htrace/atest 双路一致）。
  3. syscall/break → halted(kind 1/3) → EPC=pc+4/Cause(0x80/0xC0)/SR.CEL → `pc=[0xFFFE0130]`；rfe→EPC。
- **4199 块真身 = NOP 海滑动**：首次 rootcall(bfc0d960) → EXN → RAM 0x1E988（BIOS 写入的 handler 地址）
  但该处 RAM **从未被装入分发器代码** = 全零 = 每块 32×NOP +0x80 直线滑行 ~4000 块，
  直至 RAM 0x9DA08 非零垃圾 → emit-fail。停机面本质 = **分发器安装缺口**，非仿真错误。
- **分发器本体已反编译**（exception_dispatcher @ROM 0x1E988，5.4KB 伪 C）：boot 状态机
  （`_DAT_80066940` 设备态枚举 0x34/0x3b/0x16/0x0f/0x38/0x37；DevCtl RAM 0x669xx/0x79Exx）
  + `func_0xb003xxxx` 设备驱动函数指针（RAM 设备表项 = **注册机制写入**）。
- **安装者排除法**：ROM 无 DMA ch 寄存器写（全通道扫描=假阳性排除）；
  `FUN_bfc00420` = 唯一成规模复制例程（ROM 0xBFC10000→RAM 0x500，0x8BF0B，lb/sw 循环），
  调用者 = `autorun_open(0x67E8)`（init_finalize 尾）——**在首个 rootcall 之后**，时序矛盾。
- **定论判据（下一手）**：用 DuckStation（debug）跑同 ROM 抓**首次 syscall 时刻**的
  RAM[0x130 窗]/RAM[0x1E988]/RAM[0x500..] 金标准 = 直接判分发器来源（BIOS 早期复制 or
  硬件从 ROM 总线别名取指）。若前者→找到复制块照抄；若后者→JIT fetch 增加
  "handler 地址 0x1E988 → ROM 别名"特例（一行）。
