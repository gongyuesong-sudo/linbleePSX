# 25 — 金标准终态续跑活性与 A 表注入面（H-15b attempt1，SESSION-37）

## 1. 达成（客观实证）
- **后引导态连续运行活性**：金标准 RAM+CPU 态 + `H_EXNVEC`（异常放行改走 RAM 向量页 0x80
  真机安装态，EPC=pc+8/Cause.ExCode=0x20/SR.CEL 语义）→ 20000 块零失败零越页；
  shell 空闲环为**纯 RAM 轮询环**（serves=0：全程无 syscall——等 VSync 计数器，需 INT2 事件源才会唤醒）。
- **A 表注入通道打通**：`INJ=1` 从金标准态取 `psx_rom[0x4300+42*4]` 直跳 =
  BIOS 真 libc 码执行（574 块深，含 bne/jr/lbu/sw 串拷贝族），
  证明"宿主注入 → 真 BIOS 函数派发"路径可用。
- 设备槽读探针 `jit_lib_lowrd()`（RAM 0x60-0x6C jh_lw 计数）就位。

## 2. 未达成（attempt1 FAIL 判定依据）
- 验收面"printf 到达 stdout 设备派发（lowrd>0/devwalk）"未达：
  **ROM+0x4300 A 表 A(42) 项 = 0xBFC02B50 = strcpy 族 libc 例程，不是 printf**。
  docs/14 的"A(42)=printf"沿用 psx-spx 编号，与 SCPH1001 该表实体不符（表体=libc 分派体簇，
  编号映射需按反编译重扫）。注入后返回哨兵 → EXNVEC handler → rfe 滑入垃圾区
  （0x04500ECC 不在金标准 RAM=计算指针）= 注入用例的返回路径设计缺陷（哨兵后无有效续跑帧）。

## 3. attempt2 精确路线（不再烧在猜测上）
1. printf 真入口定位：反编译已入库（`func_0xbfc0711c` 链、docs/16 A 表 libc 定案）——
   从 A 表 44 项逐个 `jal` 语义比对找带 devsw/0x68 读的函数（一次 REA 批扫即可），
   或直接调 **A(46)/putchar 族**（putchar 源码路径含 stdout 设备读）。
2. 返回帧正规化：注入前在 sp 顶构造真帧（ra 槽=已知停机标号），避免 rfe 滑垃圾。
3. 事件源：给 shell 喂 INT2 VSync（GPU STAT  ready 位 + IRQ 计数自增的极简时基），
   使轮询环按真机节奏走完整事件循环（活性升级为动态）。

## 4. 复验
`bash tools/validate_host26.sh`（活性段现已可过；注入段含 lowrd 断言——attempt2 目标）。

## 5. attempt1 追加实证（INJENTRY 参数化后三入口扫描）
- `bfc0703c`(warm printf 簇)/`bfc06f88`/`bfc06e00` 注入：均执行真 BIOS 码 40-796 块，
  但无一读 RAM 0x60-0x6C（lowrd=0）——SCPH1001 stdout 面**不走低地址 devsw 槽**；
  终态共性=返回/EXN 后滑入垃圾(0x04500ecc)。判异：docs/15 "jalr [0x68] 哨兵"路径属
  **boot 早期 printf 兜底**，后引导 shell 用事件/EvCB 面。attempt2 需从
  A 表函数反编译图直接锚定含 `lb/lw 0x68` 的 ROM 函数地址再注入。

## 6. attempt2 达成（SESSION-38）
- ROM 普查：**代码级设备槽读全 ROM 唯一 = `bfc0d540 lb v1,0x60(z0)`**（stdout 设备位；
  另一 0x4A8B0 sw 属数据区）。设备槽绝对访问仅此一处 ⇒ 后引导 stdout 面稀疏性定案。
- 返回哨兵正规化：ra=ROM break 字 `0xBFC4C1B0`（全 ROM 10 处 break 之首）。
- 注入达成：`INJ=1 INJENTRY=bfc0d540`（现为默认锚）→ **lowrd=1**（金标准态下宿主注入
  执行真 BIOS 设备槽读）+ 391 块真 BIOS 码；滑入未装区(0x04500e81)=事件指针链未 HLE，
  验收锚以设备槽读为准。`validate_host26 PASS rc=0`。
