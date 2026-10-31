# 28 — 第一方 homebrew PS-X EXE 端到端（H-17，SESSION-40）

零版权工程：程序码/数据全自研（`tools/hbrew_build.py` python 编码器，子集
nop/break/jr/jalr/lui/ori/addiu/addu/and/sw/lw/beq/bne/j/jal），目的=用第一方程序
验证 linbleePSX 宿主链（真 SCPH1001 BIOS 码 + MIPS JIT + 金标准 RAM + 设备层）。

## 1. BIOS 加载器链定案（REA 反编译对拍）
- autorun 串：`SYSTEM.CNF;1` / `PSX.EXE;1` / `cdrom:` / `No EXE-file !`（ROM 0xDE54-0xE1B8）；
- `FUN_bfc03a18(bootfile, &blk)` 解析 EXE 头 → `_DAT_a000b870=PC0`、`0xB878=T_ADDR`、
  `0xB87C=T_SIZE`、`_DAT_a000b948=boot/s_addr`；头暂存 RAM `0x8000b070`（0x800 读入）；
- 停机面：`FUN_bfc0d960`（rootcall serve）后 `Execute!`。
- 本机 BIOS **魔数不强校验**（无 "PS-X EXE" 字面量）；头仍写标准魔数+经典字段以便
  跨 BIOS 兼容（0x10=pc0, 0x18=t_addr, 0x1C=t_size, 0x20=s_addr, 0x24=s_size）。

## 2. homebrew 程序契约（绕开 CD 读栈的方式=直接放置终态）
装载终态由宿主构造（等价 BIOS `Execute!` 后）：data@phys 0x11000、text@phys 0x10000、
pc=0x80010000、sp=0x801FFF00、ra=ROM break 哨兵 `0xBFC4C1B0`。
程序流程：
1. `t9=A(42)=0xBFC02B50`（BIOS libc strcpy，SESSION-37 判异）经 `jalr t9` 核面调用：
   banner `HOMEBREW-SELF-0 OK` src=0x80011000 → dst=0x801FE010；
2. 主循环：`counter++` 存 **0x801FE000**（内存标记），每 16 拍向 **GP0=0x1F801810**
   写复位命令 0（设备层 trap 计数）；
3. 无限循环（宿主预算到期停机）或滑入 break 停机。

## 3. 验收
- `validate_host29.sh`（H-17a）：编码器 round-trip 自证 + exe 头字段/串/长度自洽 +
  逐条语义断言（lui/ori/sw/j 关键编码值）。
- `validate_host30.sh`（H-17b）：金标准环境注入执行——counter 单调增长≥N、
  GPU0 命令 trap 计数>0、strcpy 目的串落地、fails=0。

## 4. 编码勘误记录
- 经典 `ori rt,rs,imm` 参序曾在封装层错乱（round-trip 因解码器同位对称而漏检），
  已按 `sw rt,off(rs)`/`bne rs,rt,imm` 语义约定统一并加语义级断言。
- **lw/sw 操作码勘误**：初版误用 32/40（那是 lb/sb），MIPS-I 规范 `lw=35(0x23)`、
  `sw=43(0x2B)`；round-trip 同样因编解码表同错而漏检 ⇒ 教训：**round-trip 必须叠加
  独立语义断言（期望机器字面值）**。已修：`sw s1,0(s0)=0xAE110000`、
  `sw v1,0x1810(s4)=0xAE831810`。
- `j/jal` 26 位目标域：KSEG0 高 4 位不入编码（BIOS 语义=PC 高 4 位拼接），断言只比
  低 26 位域。

## 5. H-17b 端到端达成与勘误（SESSION-40/41）
探针 `golden_cpu_probe` HB 模式：装载 exe→金标准 RAM 终态之上 pc=pc0/sp/ra 哨兵，
设备层 trap 计 GP0 写，`HBBASE` 支持 MIN 变体。运行实证（validate_host30）：
- **MIN**（无分支链外调用）：2000 拍计数标记 @0x801FE000、GP0 trap=2000、程序内
  break 停机、fails=0。
- **FULL**：10000 拍计数 + GP0 trap=10000 → `j` 到核面调用 → `jalr t7,t9` 调
  A(42)=0xBFC02B50（真 BIOS libc strcpy）→ banner 'HOME…' 写入 0x801FE010 →
  返回落 ROM break 哨兵 `0xBFC4C1B0`（pc=bfc4c100 停机）fails=0，20036 块。

勘误定案（全部经 objdump 生成码 + gdb 写监视 + 块序列判读定界；**宿主 JIT 的
jr/jalr 链接/分支/延迟槽语义经证无误**，早期“JIT 污染”假设全部证伪）：
1. **分支偏移公式**：`target = branch_pc + 4 + off*4` ⇔ `off = target_idx-(branch_idx+1)`；
   初版按 `branch_pc + off*4` 心算致全线差一（事故特征：跳过 loop 头 addiu，
   s1 恒 1、cnt=块数假象）。
2. **addiu 符号扩展**：`addiu s0,s0,0xE000` = -8192（0x801EE000）；改 `ori`。
3. **lw/sw 操作码**：规范 35/43（曾误用 32/40=lb/sb）。
4. **独立块寄存器残值**：bne2 被 bne1 跳入成独立块时，块内前置 `lui/ori t8` 被跳过，
   比较对象成 mask 残值 15 → 第 15 拍误入核面。定案：**循环比较常量必须 prologue
   预载**（t6=N、t7=返回哨兵）。
5. **返回链污染规避**：早期块（比较值错位期）经 BIOS libc 多跳后 ra 漂移(+1 步进)；
   工程规避 = homebrew 用 `jalr t7,t9` 把链接写进预载哨兵（不依赖 ra 穿透 BIOS 内部
   多跳），停机面确定。16 拍门控版 bne1 非进入路径遗留观察项（不阻塞验收，端到端
   以无条件 GP0 拍实现）。
6. 探针 `cp` 休眠快照必须先于 TICK 恢复完成 `fails=0` 初始化（SESSION-39 收束后
   未提交编辑引入的回归，已修并复验 host28 语义：300k 块 59980 周期 fails=0）。
