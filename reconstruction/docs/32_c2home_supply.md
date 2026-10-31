# 32 — H-20 homebrew C2 根协议面供给首攻（task-043 attempt1，2026-10-31 SESSION-46）

## 1. 机制（H_C2HOME，默认关）
`host/mips_jit.c` trace_boot 调度环，pc==0x600（RAM 0x600 派发块 `v0=0x674;
t0=[v0+t1]; jr t1 基`）访问时懒填**当前被读槽** `[0x674+t1]`（t1=cmd 偏移，非
左移二次——实测勘误：首版误填 `[0x674]` 单槽，链无变化，证明派发读的是
`[0x674+t1]`）：失效判据 `0 或 >0x200000` → 装自研 handler `RAM 0x40100`
（`lui v0,0x8008; ori v0,0x9F00; jr ra; nop`＝返回 cb 句柄 0x80089F00），
同步 `RAM 0x89E7C=0x80089F00`（root 控制块指针，ROM handler 约定）。
**不 flush 代码缓存**（RAM 0x600 区被 BIOS 自身清码后，缓存派发块仍忠实读活槽）。

## 2. 成果（现场 /tmp/c2h46g.log）
- 冷跳链 **18021 → 22772 块**，越过 rootcall NewRootCmd 原停机面；
- 自研 handler 实际执行 **132 次**（BLK 00040100）；
- 新暴露停机面（链更深才见到）：
  1. `bfc012a0` 块 = **JIT 缺 REGIMM bltz/bgez/*al**（已补，rt 域=0..3 勘误
     ——首版误按 16..19 编，bfc1290 现场 `14600003` bltz a2 证伪）；
  2. 同块 SPECIAL `divu/mfhi`：原内层 `case 16/17:"COP0"` 是**死码劫持**
     （funct16/17=MIPS-I MFHI/MTHI，COP0 在 op16/17 外层已有实现；内层
     `f3c==0 && (w&63)==0` 永假→mfhi 静默 NOP）。已实现 HI/LO 状态 +
     MULT/MULTU/DIV/DIVU helper（除零守卫）；JIT 单测 PASS。
  3. 最终新边界：rootcall 后 `jal bfc0d9e0`→ RAM 异常/序列表 `[0x130]` 被
     BIOS 自清 0 → EXN h=0 回退 ROM 起点 → 向量页 0x0/0x80 执行 →
     emitfail `0x100`（RAM 向量表数据当码）。

## 3. 判负教训（attempt1）
- **按位回填≠语义供给**：0x600 首访期就盲填 `[0x200]` 序列表 → 链退化
  17905（更早停机）——`0x200` 区早期是 boot 数据区，填非零即毒。C2 槽
  必须在使用时刻、按条目语义装。
- NewRootCmd 单字返回句柄被调用方拒绝：v0 现场 `80089F00→FFFF9F00` 被改写、
  0x600 访问循环重派——真机 NewRootCmd 须返回**结构体 root CB**（后续
  DelRootCmd/root 命令族按字段消费）。单 handler 不够，需整个 root 子系统。
- 结论与 task-042 终判一致并细化：越过 rootcall 达 Execute! 的前置是
  「NewRootCmd 结构 CB + RAM 0x130 异常表/C2 向量面语义化安装」，登记
  task-044 续攻。

## 4. 判据复现
`bash tools/validate_host35.sh` — 断言：JIT 单测 PASS(bltz/div 覆盖)、
无 C2HOME 基线链 18021 面不变、H_C2HOME 链 ≥21000 且 handler 执行 ≥100 次、
新边界 emitfail∈{bfc01280,bfc012a0,00000100} 记录在案。
