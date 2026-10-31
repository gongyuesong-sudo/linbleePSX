# 30 — H-19 JIT 正确性基线重锚（task-041, SESSION-44）

## 0. 背景
SESSION-43 修复 JIT 三勘误（docs/29 §3：R-type ALU 自操作、分支 flag 槽与 ra 同址 ST+124、
inc.h 漂移）后，全部历史链数字失效。本档重锚 host14/21/23/28 并定性新停机面。

## 1. 新停机面定性（真机依赖边界，非翻译错误）
冷跳 0xBFC00000（无 HLE 预放）在新语义下首次走出真实路线：

1. ROM 引导向量 → libinit 簇 → **kernel 自装**：`FUN_bfc00420` 复制 ROM→RAM 0x500（0x8BF0 字）；
   `bfc003b8/bfc00434` 两段循环 = 清 RAM 0x19000..0x1C160（8955+3159 块，旧 bug 语义到不了这里）。
2. **RAM kernel 码真执行**：`BLK a0000500/00000500 → 00000598 → 000005a8`（内核轮询环 ×1327）。
3. **边界 = rootcall NewRootCmd**：libSetConf 簇 `jal bfc01a60`（rootcall stub，v0=0x80000010=cmd 0x10）
   → dispatcher 取 RAM 处理槽（t9=0，未初始化）→ `jr $t9` 跳 **0x3C0E0000**（phys &0x1FFFFF=0xE0000
   RAM 零页）→ JIT 对全零页 32×nop/块合法 → **128B/块 sled 爬升**至 0x3C15BD80 非零字节 emitfail。
   gdb watchpoint 实证首次 3C 转移：`pc=3C0E0000 ra=BFC0686C v0=80000010 a0=a1=974`。
4. 金标准 RAM 同界：golden `0x9E7C/0x9E80` 槽=0（真机后引导亦 0），golden 链同停 3c15bd80；
   但 golden 链更长（142176 块，进 RAM kernel 段 1353 块）。

结论：冷链下一依赖 = **root handler 表/NewRootCmd 处理槽安装面**（真机由 C2/libSn 安装）——
归 task-040（HLE 安装面补槽）域，不是 JIT 语义缺口。

## 2. validator 重锚明细
- host14：`bios_smoke` ok 谓词 `rc==2 && pc>=0xBFC00000 && blocks>=100`（旧基线=ROM 内 syscall
  浅停机 bug 伪像）→ **`rc==2 && blocks>=15000 && v0==0x80000010`**（抵达 rootcall NewRootCmd）。
- host21：EXN 事件锚（budget 8000 内旧链触发）→ 代码路径不变但新链不再经过；改锚
  **chain≥18000 + final=3c15b***（冷链边界）** + v0=80000010。docs/18 判异原文保留为历史。
- host23：锚 `0x4a4c/0x32e24`（bug 语义特定路线）→ **`a0000500|00000500`（RAM kernel 入口首执行）
  + `000005a8`（内核轮询环）**；chain>206 不变（实测 18021/142176）。
- host28 TICK：旧 500 块/周期的"闭环"是 ra-flag clobber 伪环；真哨兵 = golden shell 到
  **break 词（hk=3, pc∈[BFC4C1B0,+0x10)）**。probe TICK 条件扩 hk==3；每周期 ~6 块 +
  flush 全清 → 预算 30M→3M（cycles≥4e5，实测 3M/≈5e5 cycles/fails=0）。
- 取证配套：trace_boot/call_log 块史环 8192→65536 并修输出掩码回绕截断；EMITFAIL 附带
  前 8 块+ra/t9/v0 现场。

## 3. 数值基线（SESSION-44 实测）
- smoke：blocks=18021 rc=2 pc=3c15bd80（v0=80000010）。
- htrace 无 golden：chain=18021；有 golden：chain=142176，RAM kernel 块 1353。
- TICK：3M 块 ≈ 5e5 周期 fails=0；homebrew E2E/host26/host19/host31 在新基线仍 PASS。
