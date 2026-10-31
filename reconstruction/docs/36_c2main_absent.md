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

## 3 attempt1 终判
墙未破（未达 Execute!）。**下一 attempt/task 的正题从"设备真值"收窄为**：
1) 反汇编 copier（RAM 0x50000 区）尾部：C1 在 C2SEC 拷贝完成后按什么条件调 C2main——
   预计读某设备/内存状态（CDROM 就绪字 / rootCB / 中断掩码）；
2) 若该条件依赖 CDROM 就绪 → 接 H_CDHLE 的 ready 语义（ack 后 2 秒就绪轮已建模）；
3) C2main 跑通后事件泵+CD ack+TIMERS 节拍（原 task-049 域）才真正开始。
