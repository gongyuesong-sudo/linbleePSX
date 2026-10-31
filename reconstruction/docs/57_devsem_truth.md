# 57 — M-14 批3实现2：设备真值语义三件与"等待不在设备位"终判（task-071，SESSION-80）

## 1. 本楔实现的 PSX 真值语义（宿主资产，均 env 门控零回归）
| 能力 | 语义 | 实现点 |
|---|---|---|
| `H_ISTATCLR` | I_STAT(0x1F801070) **写 AND 清**（PSX 真值：写 0 位清中断，旧实现为覆写） | `jh_sw` 设备写分支 |
| `H_EVTGEN=<mask>` | 节拍事件发生器：每 64 次 I_STAT 读翻转置位（vblank=0x8 型事件流） | `dev_read` |
| `H_GPUOK` | GPU STATUS(0x1F801810) 读置 ready+can-send（0xB0000000） | `dev_read` |

## 2. 实验矩阵（ctx24 相关面）
- `b0/3d`（打印族）：WR 普查实证**字符出口 = DMA ch0/GPU 命令路**（非 TTY 设备字节端），
  119,220 块后 halt `0x800586d0` = **suspend 函数 epilogue**（与 0x19 同族在飞语境墙）。
  GPUOK/ISTATCLR/EVTGEN 全组合下**逐字节不变**。
- IRQ13 族（H_IRQTICK+FWDSERVE 基线上叠加三件）：轨迹逐字不变，转化 0/13。
  收敛点 `0xB0` = 嵌套 rootcall 进入**线程 suspend 语义**（0x19 族）——轮询早已过去，
  更深一层等的是**调度器切换完成**，不是设备位。

## 3. 终判（修正 docs/56 §3 施工顺序）
设备真值面（I_STAT 清语义/节拍/GPU ready）在无盘冷环境下**无独立增益**——
所有 WAITER 的第二层等待都汇入**切换出口墙**（halt 落点 `0x800586d0/0x80058628` = suspend/
resume epilogue 对在飞寄存器的依赖，docs/56 §1.2 的 ra 非内存来源）。
→ 批3 唯一关键路径收敛为一件：**切换出口钩（原规格 3）**；顺序修正为 **3 → 1**，
事件生产者(2)降级为切换出口完成后的节拍补丁。
切换出口钩的具体落点已被本楔钉死：`0x800586d0`/`0x80058628` 型 epilogue——
宿主在这些"恢复返回点"提供 `ra←线程入口` 的出口注入即打通（候选实现=对 epilogue 块
`jr ra` 前插宿主回调，或 JIT 对恢复类 `lw ra,sp,+N` 后 pc=非法/尸检值时改道 FWDLiveThread 帧）。

## 4. 保留资产
`H_ISTATCLR` / `H_EVTGEN` / `H_GPUOK`（未来真机节拍与 GPU 面对拍直接可用）。
