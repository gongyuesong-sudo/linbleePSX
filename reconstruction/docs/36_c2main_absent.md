# 36 — H-21c attempt1：C2main 从未被调起（task-049，2026-10-31）

## 1 机制（已 commit）
- `H_DEVRST2`：dev_write 对 DMA 通道控制块 `0x1F801080..0x1F8010FF` 的 CHCR 字（o&0xF==0xC）实现
  **Trigger(0x01000000)→自清+Finished(0x02000000)**，且对应 BC 字清 0（传输完成语义）。
  消除 C2 对 `0x1F801DAC`（DMA7 完成位）的 10008 次/链忙等。

## 2 本征取证
- **C1→C2 直 jal 普查**（ROM 全镜像，目标落 RAM 0x30000..0x97FF0）：**741 个不同入口**
  （热点 0x44a50×80、0x4665c×75、0x336ec×45、0x393a4×42、0x5a910×36…）。
  → "全部 C2 入口换自研 ack" 路线不成立：C1 自身按 Sony C2 语义分支，替换面即重写 C2。
- **C2main 缺席实锤**：H_JITBLOCKS 全链环内 `RAM 0x30000..0x30100` 块数 = **0**，
  copier 区（0x50000..0x52000，除 memcpy）块数 = 0（被 425440 次 memcpy 环淹，但 C2main 段
  即便存在调用也应再出现——从未）。C1 拷完 C2SEC 后**没有调起 C2 内核 init**（0x30000 =
  golden `27bdffe8 addiu sp,-0x18` 入口帧），因此 `[0x89E2C]` setter 链、事件泵、
  设备注册全没跑——这就是 docs/35 §3 "健康相缺失"的上游根因。
- 停机面：memcpy 尾链跳 `pc=0`（EMITFAIL hk=2），`ra=4` 毒值；毒点锚（SESSION-51 H_RAWATCH）：
  `bfc03610c jr t9(t9=0)` 写 ra=4、C2 `0x31a3c` 写 ra=0。
- **C2main 起载面细化（本征 ROM 全镜像扫描）**：ROM 内 `jal 目标phys=0x30000/0x30004` 仅 **1 处**，
  且位于 **C2SEC 内部** ROM off 0x710D4（`jal 0x80030004`，周围是 C2 数据/代码混合区）= C2 自引用。
  → **C1 侧不存在静态 `jal C2main`**；真机 C2main 由 C1 kernel（RAM 0x50000 区）经
  CreateThread/jalx(t9) 间接起载。attempt2 锚点：在 JIT 里监视 `jalx/jr t9` 实值==0x80030004
  的起载点是否到达（当前链块历史 0x30004 起块 = 0，即间接起载从未发生——被 rootcall 单槽 ack
  吞掉或 C1 kernel 线程表未建）。

## 4 SESSION-53 修正与新图景（attempt2 取证，UNIQUE 清单机制）
新增 `H_JITUNIQ`：块首达 UNIQUE 有序清单（免疫 65536 环饱和盲区）+ EMITFAIL 打 UNIQ-TAIL + EXN 行补 sp/ra/hk/v0/t9。三条旧结论被推翻/修正：

1. **"C2main 从未执行"撤回**：UNIQUE 清单显示链实际执行 `80030000/80030010/8003002c`（C2main 入口！）
   + C2 段共 **141 个 unique 块**（0x43xxx/0x4efxxx/0x54xxxx/0x5a8d0…）。§2 的"缺席实锤"是环饱和假象。
2. **"AdEL/sp 非对齐"作废**：EXN 现场 `epc=8005aa9c sp=801ffd78`(对齐完好) `ra=80054100 t9=1 cmd=1`
   （cmd=日志字段=rootcall 命令号，非 ExcCode）。epc 落在 C1 kernel 派发区，t9=1 垃圾值→该点本身是被污染后的二次受害者。
3. **停机全时序**（UNIQ 尾部）：C2main 起 → C2 141 块（含 0x362c0/0x363bc）→ `bfc1f694`→`800362c0`→
   **pc=00000004**（C2 epilogue `lw ra,sp,0x14` 复活 ra=4，槽位无人写）→ 向量页漫步 `0084,0104,…,0f84`
   （stride 0x80 执行向量页数据）→ halt `pc=0` EMITFAIL。

**新根因假设（attempt3 正题）**：金标 RAM 混入注入面污染活栈/控制块——
- `C2GOLD fillhole sp=00000000`：切片填充发生在**冷态 sp=0** 时；
- 切片覆盖 `0x30000..0x8FFFF`，**越过 C2SEC 尾(0x96FF0 内数据段)与 C1 堆/CB 区(0x89E7C 等)**——金标机器指针≠冷链布局；
- `statepatch 653 词`含堆区旧指针。`sp+0x14` 应由金标机"栈底 C2 主线程帧"写入，冷链无对应帧 → ra=4。
**修复方向**：切片/补丁消费加**上界门**（`H_C2END` 默认 0x89E00，绝不覆盖 ≥CB 区与栈区）；
状态词补丁仅允许 C2SEC 数据段白名单；栈区 ra 槽由自研线程起载帧建立（H_C2BOOT：冷态手写 sp=0x8009xxxx 帧+sw ra）。

## 5 终判（attempt1+attempt2 合并，SESSION-53）
墙未破（未达 Execute!）。§3 旧正题 1)（copier 尾部 C2main 调起条件）**已消解**——C2main 实际会跑。正题改为：
1) 反汇编 copier（RAM 0x50000 区）尾部：C1 在 C2SEC 拷贝完成后按什么条件调 C2main——
   预计读某设备/内存状态（CDROM 就绪字 / rootCB / 中断掩码）；
2) 若该条件依赖 CDROM 就绪 → 接 H_CDHLE 的 ready 语义（ack 后 2 秒就绪轮已建模）；
3) C2main 跑通后事件泵+CD ack+TIMERS 节拍（原 task-049 域）才真正开始。
