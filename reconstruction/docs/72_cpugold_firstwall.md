# 72 — A3 楔6：H_CPUGOLD 真起点——首道设备墙现形（task-088，SESSION-97）

## 1. 起点资产链（先否后立）
- `disc.state` **无明文 CPU 段**：值锚法验证 `0x800DFE90` 命中位 = MainRAM 栈窗数据本体
  （非寄存器序列化），section 名仅 MainRAM/ScratchRAM——楔6 主案路否。
- 备胎（docs/24 H-15a 资产）激活：`.analysis/golden/gstate.hdr`（`GS1\0`+npc+pc+gpr32，140B）
  = **真金 CPU 态**：`pc=0x8003D704 sp=0x801FFF00 t9=8003D704 s3=0x8005AE2C`（C2 kernel 内部）。
- 新能力：`jit_lib_set_regs(gpr32,pc)` + `H_CPUGOLD=<hdr>`（fwd_init 装载）+
  MAINRUN entry 改取 hdr.pc。

## 2. 真起点首跑（楔1-5 语境一次性勘正）
`MAINRUN rc=20002 kind=0 endpc=0x8003D6FC`（LOOP-DETECT）——
20,002 块**零死跳**收敛于 `lw t6,[0x8008_3C58] / slti <2 / bne` + `beq -5` 双段轮询环：
**第一道真设备墙 = 状态词 `0x8008_3C58` 的"递减等待"**（等 <2 = 设备完成计数递减）。
有桥/无桥形态相同——**楔1-5 的低页死亡链/僵尸环全是假起点产物**，真实现场根本不经过那里。

## 3. POKE 三连（语义确认）
| RAM[0x83C58] | 结果 |
|---|---|
| golden/POKE=2 | 双检边界死锁（第二检 `slti<2` 不满足→回跳）——**golden 快照自带死锁态**（snapshot 时设备未完成） |
| POKE=0 | **过第一墙**，393 块 → 第二墙 `jr→0x04500ECA`（op=0x0EE40010 数据）= C2 堆过期指针族再现；`ra=0x044F4C02 sp=0x801FFF00` 与 golden 寄存器一致（真起点语境实证） |

## 4. 楔7 定靶
- `0x83C58` 递减设备 HLE（0x8008 状态区 = docs/35 状态词面家族；写者普查：主运行内谁应减它——
  预计 DMA/传输完成方）；HLE 法 = 读窗自动递减（H_DECR 形）。
- 过墙后 `0x04500ECA` 堆指针面：解剖该指针槽来源（0x45xxxx 堆在 golden 的分配器状态）。

## 5. 工程教训（WARN 级）
本轮 fwd_test.c 漏 `string.h` 致 `memcmp` implicit-decl **编译失败但旧二进制掩盖**
——BUILD-CLEAN 检查脚本自身把 error 行打进了输出却未中断（grep 管道 exit 不参与判定）。
修正：验证脚本构建段用 `set -o pipefail` 级检查；本楔数据全部来自修复后重建的真 build。
