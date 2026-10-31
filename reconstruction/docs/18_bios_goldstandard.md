# 18 — BIOS 分发器安装金标准判异（H-12d，SESSION-31）

权威源：PsxEmu `Docs/HLE-BIOS.md`（2026-10-09，boot_runner 对 SCPH1001 真内核 RAM 逐地址校验）。

## 1. 金标准事实（判异结论）
1. **内核 RAM 镜像 = ROM trap 区复制**：`FUN_bfc00420` 把 `ROM 0xBFC10000 → RAM 0x500`，
   长 `0x8BF0`（RAM 0x500-0x90F0）。**kernel 函数入口 = ROM `BFC10000h + n*10h` trap 桩**。
2. **RAM 内核图（逐地址）**：向量 0/80h/A0h/B0h/C0h；table-of-tables @100h；A0h 表 @200h；
   分发器 @5C4h/5E0h/600h；B0h/C0h 表 @874h/674h；**异常处理器 @C80h**（hook 槽 +70h..+AFh，
   游戏会校验/打补丁）；异常栈 @6CF0h/6CF4h/75D0h；DCB @6EE0h；FCB @8648h；ExCB/EvCB/PCB/TCB @E000h+。
3. **RAM 0x130（经 0xFFFE0130 硬件窗写）= rootcall/entry 表项，存 BIOS ROM 相对偏移**：
   值 `0x1E988` = trap 槽 → **全地址 = 0xBFC00000+0x1E988 = exception_dispatcher（ROM 直接执行）**。
   ⇒ 此前"RAM 0x1E988 需复制安装"假设**证伪**——分发器本来就 ROM 直执。
4. `model_probe` 比对的 RAM 0x84 字符串 = 真机 boot 动画期安装（"Licensed by Sony..."，
   ROM 内串 @0xE288）；HLE 系（PsxEmu）直接预置实现 instant boot。
5. syscall 处理器语义含 psx-spx 记录的 bug 面（"syscall handler stepping EPC even in a delay slot"）。

## 2. 落地修改（host/mips_jit.c）
- serve EXN：`h=[0xFFFE0130]`，`h<0x80000 → h+=0xBFC00000`（ROM 偏移语义）——EXN 现进
  **真分发器**（异常态机 ROM 0x1E988 主体，docs/17 §6 已反编译）。
- CEL=1 时二次 syscall = 递归直调入口（EXN2，不重置 EPC）。

## 3. 链现状与最终边界（实证）
- warm 链 = 4199 块（EXN 生效，分发器执行中），停机 = **RAM 0xB1D20 设备函数指针表项未写**
  （分发器 `jalr` 未注册驱动；对应反编译 `func_0xb003xxxx` 系表项）。
- 判异定案：**引导链全通的最后一层 = pad/memory-card/CD-ROM 设备驱动注册**
  （trap 区内核函数 + DCB/FCB/TCB 结构 + PAD/INT2 中断时序）——即完整设备仿真面，
  与 docs/15/17 预判一致；`init_finalize→autorun_open` 的 `cdrom:SYSTEM.CNF` 请求坐实 CD 为硬依赖。
- 游戏运行路线判定（AGENTS.md）：**DuckStation**；本仓库引擎宿主链知识（A/B/C 函数 trap 图、
  RAM 内核图、异常语义、PCB 协议）已完整可续写。

## 4. 复验命令
- `bash tools/validate_host21.sh`（金标准断言 + EXN ROM 偏移语义 + 链 ≥4199 且停机在设备指针面）
- 证据：`H_BIOSEXN=1 /tmp/atest` 输出 `EXN ... -> h=bfc1e988`（ROM 分发器）。

## 5. task-027 联动复跑结论（attempt1 实证）
- EXN 分发器激活（h=bfc1e988，链 4199）后引擎侧 deep **仍=4 / max=004002cf**：
  BIOS 侧异常链全真执行**不改变**引擎深段——与 task-024 证伪一致：引擎深段钥匙在
  img 模板 ABI（x86 回填）与设备驱动注册面（pad/card/CD），二者均超出 MIPS-JIT 宿主范围。
- 定案：引擎驱动 BIOS 到游戏 = 需 (a) img 模板 ABI 逆向回填 x86 或 (b) 完整设备驱动面；
  游戏运行路线维持 DuckStation（AGENTS.md）。

## 6. 勘误(SESSION-44, task-041 重锚)
本文原链数字(chain 4199/停机设备指针面/EXN 事件路线)建立在 JIT addu 自操作与分支 flag-clobber-ra 两 bug 语义上(docs/29 §3)。
新基线冷跳链越过 kernel 自装抵达 rootcall NewRootCmd(v0=80000010)处理槽未装边界(停机 3c15bd80)，
不再途经 0xD964 rootcall cmd=1 停机面；EXN ROM 偏移语义(RAM 0x130=ROM 相对偏移)代码路径不变。
详见 docs/30。
