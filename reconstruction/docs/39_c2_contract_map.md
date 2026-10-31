# 39 — H-23a C2 初始化契约取证：包装器链地图（task-051，2026-10-31）

## 1 金标 boot 栈"真返回地址"逐条溯源（docs/37 §6 五个词）
| 金标槽值 | 定样 | 语义 |
|---|---|---|
| `0xBFC06998` | ra-8 @0x6990 = `jal 0xBFC06FF0` | C1 内深层 init 包装器（帧 sp-0x20 + sw ra,0x1c） |
| `0xBFC0702C` | ra-8 @0x7024 = **`jalr ra, t6(0x8003xxxx)`** | **C1→C2 尾跳包装器**：先 `jal 2B50(memcpy)`/`jal 1920` 再 tail-jump 进 C2SEC init 区 |
| `0xBFC067D8` | ra-8 @0x67D0 = `jal 0xBFC067E8` | C1 内 init 子步 |
| `0xBFC0E158` / `0xBFC0DF20` | 邻区为 `"%s: gpu"` 格式串调试区 | GPU init/错误报告路径 |

## 2 C1→C2 尾跳包装器形态（实证修正版）
- 形态 = `lui tX,0x8003` + `addiu tX,tX,低16位`（分置，非相邻对）+ `jr/jalr tX`。
  实证实例：`0xBFC07020 lui t6,0x8003` → `0xBFC07024 jalr ra,t6`（ra 落 boot 栈金标槽值）。
- **全 ROM 普查 `lui r,0x8003` 恰 4 处**：`0x7010`（lui a0→C2 缓冲参）、`0x7024 jalr ra,t6` 包装器、
  `0x26254 addiu t9,t9,0x48ec`+`jr t9`、`0x263b4 addiu t7,0x48ec`+间接 —— **C2 址族派发点集中**
  （t9/t7 基 = 0x80030000+0x48EC 表）。0x26254 族带 `andi t9,0xF0` 类命令过滤 = **事件/回调分派器**。
- **运行时 UNIQ 已证** C2 init 区块被链执行过（0x80030000/0x80030010/0x8003002c/0x80030798… docs/36 §4）。
- **与 H_C2HOME 拦截点同面**：`pc==0x600` 派发环 + `[0x100+cmd*4]` 根向量 stub 正是被我们
  blanket-ack（v0=1）替代的通道 → ack 抹掉的不是单个调用，而是**整条 C2 init 序列**
  （memcpy 缓冲、CB 初始化、回调数组登记都在其体内）。

## 3 回调表结构扫描结论（否证简单布局）
- 金标 RAM 0x80000..0xC0000 以 (KSEG0 fnptr, 小 flag) stride-8 连续 ≥3 项模式扫描 = **0 命中**
  → libgpu 回调表非平坦 (fn,stat) 数组布局（或不在该区）；回调登记语义内嵌于 C2 init 函数体。
- 结论：**"契约完整化"的正确颗粒度 = 按 cmd 实现根call 语义**（NewRootCmd=分配并初始化 CB、
  AddCallback/DelCallback=操作 CB 内链表、FlushPrim=...），而非外挂在栈/CB 区写数据结构。

## 4 H-23b 输入件（task-052 直接依赖）
- 待实现 cmd 语义清单来源：C2HOME visit 日志的 `idx=t1` 序列（docs/32 §5 现场已有）。
- NewRootCmd 真处理体 = ROM `+0x59E0`（task-042 定样）+ 其下游 `jalr 0x319xx` 族。
- 金标 CB 参考 = `.analysis/golden/c2_state_patch.bin` 653 词中 [0x89E00..0x89EFF] 段
  （root CB 区）可作字段布局对照表。
- 验收口径（052/053）：boot 栈帧 `0x1FFDCC/0x1FFDF8` 由**链自身运行**写出非零真返回地址
  （`H_WATCH` 可复算），0x1014 坠落点位移或消失。
