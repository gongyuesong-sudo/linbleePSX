# 37 — H-22 attempt1：halt 真身 0x1014 与 ra=4 毒源收网（task-050，2026-10-31）

## 1 本征新事实（推翻/精确化 §36 若干推断）
- **halt 不是 `pc=0`**：CHAIN 打印的 `final=00000000` 是"不可取指目标别名"假象。
  真实现场（EMITFAIL 日志修复后）：`EMITFAIL pc=00001014 op=42000010 ra=00000004 sp=801ffdd0`。
  `RAM 0x1014` 在金标真机 = 数据词 `0x42000010`（COP0 类，从未被真机执行），冷链向量页漫步
  `0x84,0x104,…,0x1000(→0x1014)` 本身**不是真机行为**——是 ra=4 毒后的坠落路径。
- **ra=4 全程不经过 pc=4 执行**（RACATCH 全链仅 1 次 0x4 转移 = halt 前最后一次）：
  → 第一颗毒 ra 由 **jalr/jal $0（寄存器值=0 的间接调用）** 直接 `mov_imm(4)+wrr31` 写入，
  再被帧保存者 `sw ra,sp,0x14` 落进 boot 栈。与 docs/35 `0x3610c jr t9(=0)` 锚一致。
- **JALZ 定址探针 = 0 命中**：毒源非定址 jal 0，必为间接 jalr（下一 attempt 的捕获面）。
- `H_JZSLED`（RAM 0x0 `v0=1;jr ra` ack 跳板 + 向量页定向 flush `jit_flush_range`）：
  跳板被执行（UNIQ 含 00000000）但**全链逐位不变**——jr $0 在 pre-sled 期已产生不可逆毒 ra，
  ack 语义救不回已污染的帧。与 task-043"单槽供给不足"同构。

## 2 机制（本征 commit）
- `H_JALZ`：JIT 发射期捕获 定址 jal→零区（含 from/delay/ra/v0/t9）。
- `H_JZSLED` + `jit_flush_range(lo,hi,n)`：ack 跳板 + 向量页三别名定向缓存失效。
- **EMITFAIL 日志修复**：原写 `devlog_fp()`（默认 NULL）致停机原因静默；现写 jit_rc_log
  并补 ra/sp/v0/t9 —— 这是 H 系列多次"final=00000000 谜团"的显示层根因。

## 4 attempt2 预备实验（同-session 追加，均零命中——重要排除项）
- `H_JALRZ`（jalr 链接寄存器直写钩，全 lreg、constructor 常开）：**0 命中** → ra=4 亦非 jalr→0 所种。
- 结合 §1 JALZ=0：**两种零跳转都不产生第一颗 ra=4**。剩余候选：`lw ra,槽`（内存残值）或
  向量区执行期语义。UNIQ 时序显示链**两次**进入向量页漫步（idx121 起 0,0x80,0x100…与 halt 前
  0x84..0x1014 二次），疑与 EXN 分发器（bfc1e988）扫描向量表/穿洞有关。
- attempt3 正题：反汇编 ROM 0x1E988 分发器本体 + `H_RAWATCH` 抓 `sw ra`/`lw ra` 全事件序列定种。

**终判**：墙未破（未达 Execute!）；attempt1 交付 halt 真身 + 机制面 + 两项关键排除（jal0/jalr0 均非毒源）。

