# 96 — BIOS 战役楔4（侦察楔）：A(24) rootcall 漂移完整剧本 + BIOS 等待条件清单（task-115，SESSION-126）

## 1. 本楔零引擎语义改动 —— 纯侦察，产出=完整证据链
三件新侦察装备（均 --bios-run 工具面，不改执行语义）：
- `BISO_JUMPZERO`：计算 j/jal/jr/jalr **真实跳转目标**（修正旧 LOST 探针
  读延迟槽的假阳性），目标 16 字全零 = 现场记录（from/insn/tgt/ra/寄存器像）。
- `BIOSRUN_WATCH=lo,hi`：RAM 写事件窗 watch（归一化地址），写后回读真值
  post=，含 pc/step —— 表区生命周期全程可追。cap 400。
- `BIOSRUN_TRACE=a,b`：步号窗口单步 trace（pc/insn/a0/a2/a3/ra）。
- `tools/psx_rom_dis.py`：ROM 反汇编判读器（GPA 窗，R3000A 主指令集）。

## 2. 漂移完整剧本（step 级时间线，全部 watch/TR 实证）
| step | 事件 |
|---|---|
| 94-800 | BIOS 冷 boot 清 RAM 0x0-0xF80（含 0xB0 向量区）|
| 17389/17442 | GPU 探测 v0=a000b068（docs/57 H 时代真值命中）|
| 79228± | `jal 0xBFC042D0`+`jal 0xBFC042A0` = **InstallExceptionHandlers**：42d0 拷 ROM[0x4300-0x4604]→RAM[0x200-0x504]（dispatch 码）；42a0 拷 RAM[0x510-0x540] 模板→RAM[0xA0-0xD4]（向量 stub 表）—— **TR 逐词实证装载正确，0xB0=lui $t0,0/0xB4=addiu $t0,0x5e0/0xB8=beq+8 与 golden RAM[0xB0] 逐字一致** |
| 79491/79854 | **BIOS 自身稀疏清零循环**（ROM 0x1958 区：t2=0→0xF80 步距 128，每轮 sw $0 于 +0,+16,...+112）—— 把刚装载的 0xB0 stub **抹回 0** |
| 80910 | 主 init `jal 0xBFC0D9A0` = **rootcall 仿真桩**（`addiu $t2,$0,0xB0; jr $t2; addiu $t1,$0,N` —— t1=24/A(0x18)=GetEnv 系）—— `jr` 落已抹零的 RAM[0xB0] → nop 漂流 → 81896 `jr $26(=0)` 归零 |
| 终点 | ra=bfc06868（主 init 后半：CDROM SetLocation 0x1F801C00+0x180-186 组写 + I_STAT 清零 + GPU 数据口写 —— **watch 实证这些指令从未到达**）|

## 3. BIOS 等待条件清单（本楔核心交付）
1. **W1 = RAM[0xB0] 向量 stub 存活**（当前死因）：真机 golden=stub 在位。
   stub 由 42a0 装载、被稀疏循环抹除、**再装载点只存在于第二主序 0x6CDC 组**
   （jal 42d0/42a0/db10/db20 全 ROM 仅 2 个调用点：0x6844 系与 0x6CDC 系）。
   真机 A(24) 时刻 stub 必须在位 → **中间必有一条"清零后→再装载"的真机路径
   我们未走通**——候选：(a) 稀疏循环在真机上范围/行为不同（RAM_SIZE 探测
   依赖 0x1F801060 写 0xB88——我们已回读正确）；(b) A() 实走 CPU 异常机制
   （syscall/break→BEV 向量→**ROM** handler）而非 jr $t2；(c) 0x6CDC 系在
   A(24) 之前有条件调用（条件=GPU/CDROM 应答）。
2. **W2 = CDROM SetLocation 应答**（0x1F801C00+0x180-0x186 写 + 0x1C02/0x1C03
   读）——主 init 后半硬依赖，watch 实证未到但序码在案。
3. **W3 = I_STAT/I_MASK 主循环 + vblank 节拍**（0x6890-0x6894 清 I_STAT +
   后续 GPU 中断等待）——psx_dev EVTGEN 已备。
**楔5 定向（按性价比）**：W1(b) 最廉——A(N) rootcall 若走 break/syscall 进
BEV=1 ROM 向量（0xBFC00080/0xB0 区 ROM 码=dispatch 入口 ROM 0x240 系），
则 stub 抹除无关紧要，流程直通。试验 = 引擎把 d9a0 系 jr 0xB0 视作异常进入
（不改 jr 语义，先证 ROM 向量区码存在性）—— ROM[0xB0-0xF8]=0（本 ROM 文件
实证）→ (b) 仅对 RAM 向量成立 → **排除 (b)**；剩 (a)/(c)。

## 4. 判读副产物（ROM 结构知识，入 map 面）
- ROM 0x00-0x70 复位头；0x150=数据(&S 串区)；**0x240+ = 向量 handler 码原形**
  （ROM[0x240]=240a0000/240b0f80 与 RAM 清零循环 0x1958 同码——0x1958 即
  ROM 码区本体，dispatch 由 42d0 拷贝进 RAM 0x200 系）。
- 0xA0000510 系模板 = 运行期生成（42d0 拷贝后由 dispatch 码自填）。
- InstallExceptionHandlers 双例程 0x42A0/0x42D0 = 标准 BIOS 公开语义互证。
- 教训入账：RAM dump 打印 range(step=8) 造成"表缺词"假象——**取证输出
  步长必须与语义单元(4B 字)对齐**，否则制造伪 bug 一场虚惊。
