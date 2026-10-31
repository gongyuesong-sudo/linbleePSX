# 14 — BIOS A-call / syscall 静态破译（task-023, H-11a）

复跑取证：`python3 tools/scan_bios_syscalls.py`（断言内建，rc=0）。
ROM：`psxbios/SCPH1001.BIN`（512KB，sha256 71af94d1…f99d3，登记于 host/build/bios_sha256.txt）。

## 1. A(0)-A(43) 默认跳表 @ROM+0x4300

- 44 个连续 KSEG1 BIOS 窗指针（`0xBFC00000..0xBFC7FFFF`），小端。
- **A(0)-A(9) = 0xBFC06FDC（未实现默认处理器）**；A(10)-A(43) 有真实入口，
  与 PSX 文献的 A 表构成一致（A(38)/A(39)=SetConf/GetConf 等）。
- 全表（real 34 项）由脚本打印；节选：
  A(10)=BFC022C0 A(19)=BFC02240 A(37)=BFC02EA0 A(38)=BFC02EDC
  A(39)=BFC01A90 A(42)=BFC02B50 A(43)=BFC02B8C。
- BIOS 初始化代码把该表拷入 RAM `+0x180/+0x18C/+0x194`（rootcall/A 派发用）。
  引擎（img=JIT 模板库）若已自拷，则派发可完全在宿主 RAM 镜像内观测。

## 2. syscall(0x0000000C) 普查

- 原始命中 26；代码段判据（前后指令可解码）后 **19**。
- 数据区假命中集中在 0x4C000+（字体/资源镜像）——H-11b 拦截按地址段排除。

## 3. rootcall 桩模式（关键停机面）

`addiu $4,$0,N ; syscall ; jr $31`（a0=N，返回即归 ra）——BIOS 内部经异常面向
RAM rootcall 命令表发起服务：

| syscall 点 | guest | cmd(a0) |
|---|---|---|
| 0x0D8C4 | BFC0D8C4 | 2 |
| 0x0D964 | BFC0D964 | 1 |
| 0x16684 / 0x16694 | BFC16684 / BFC16694 | 1 / 2 |
| 0x42A14 / 0x42A94 | BFC42A14 / BFC42A94 | 2 / 1 |

（脚本 `ROOTCALL-STUBS` 全列出 6 桩，cmd 多重集 = {1,1,1,2,2,2}。）

**warm 引导链（task-022，134 块）停机面 = guest BFC0D964 rootcall cmd=1**
（boot 期第一次 rootcall；docs/13 的 NOP 扫描循环在其后）。

## 4. A 处理器序言画像 → H-11b 指令面结论

34 个入口的前 8 条指令统计：**无 COP0/COP2 依赖**，全为 lui/ori/addiu/lw/sw/jr
族——现有 mips_jit（task-021 指令集）可直接执行 A 处理器主体。

## 5. 停机面处置方案（交给 task-024 / H-11b）

1. `jit_lib` 的 syscall halt 改为：读 `r[4]`(a0) → 宿主 rootcall 命令表
   （最小集：cmd=1 bootinit no-op、cmd=2 设备就绪 no-op，返回值 0）；
2. 执行 handler 后 `pc=r[31]` 续跑（桩语义 `jr $31`），预算内继续引导链；
3. 引擎窗捕获点（htrace BIOS-MIPS-ENTRY）改挂同一接口
   `jit_lib_bios_call(eip,…)`——注意 task-019 attempt3 教训：
   **不得盲回写 EAX**；引擎寄存器↔PSX 状态映射仍是开放问题（0x328400/edi=0x50000
   候选 PCB 未证实）。验收推进优先走 warm 侧链长增长，其次才试引擎侧对接。

## 6. 与旧结论关系

- 证伪补强：docs/12「VERDICT: PSX exec requires JIT」——JIT 已就位（H-10a/b），
  剩余面收敛为 rootcall/syscall 语义 + 引擎 PCB 协议两点。
- task-019 的 VA 供给修复保持有效（alloc -1 归零），其深段验收移交 task-024。

## 7. SESSION-26 attempt2 破译：引擎 PCB 与 A(38) 原生桩协议（实证）

反汇编 img+0x2b8..0x2cf（guest 视图 0x4002b8，模板尾派发）：

```
mov  0x4(%esp),%edi        ; edi = 参数块 = PCB 基（运行时观测 0x50000）
mov  $0x328400,%ebx        ; arena 内 CB 结构基
mov  0x44(%edi),%eax       ; eax = [PCB+0x44] = BIOS 窗基（观测 0xBFC00000）
call *0x174(%edi)          ; svc：原生 thunk 槽 PCB+0x174（htrace 占位表 SEGV→EAX=0）
push $0xffffffff           ; ★ 固定返回值协议：-1（与 EAX 无关——attempt3 回写 EAX 无效之因）
jmp  0x400108              ; 回模板 dispatch 主循环
```

- **PCB 布局（部分）**：`+0x44` BIOS 窗基；`+0x174` 原生调用 thunk；参数经 `[esp+4]` 传入。
- 引擎的 BIOS-A(38)/SetConf 走的是 **原生 svc thunk 而非 MIPS 跳窗**——此前「窗捕获=A MIPS 调用」模型作废（与 §5/attempt1 证伪一致）。
- 独立执行 A(38)@BFC02EDC（JIT，ra 哨兵）：4 块后 emit-fail@0x1001——A(38) 读 RAM 配置跳转表，
  需 **完整引导链跑通后的 RAM 状态**（rootcall cmd=1/2 设备服务先行）。
- 停机面再收敛：引导链需要 ①RAM 异常向量/根表建立(docs/13 的 0x7B60 区) ②rootcall 设备语义
  ③timer/vsync IRQ——即最小设备层（候选 H-12），深度超出 A-call 仿真面。
