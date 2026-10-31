# 88 — PE 战线楔1：port PSX 薄引擎骨架——docs/81/82 施工图首次落成可跑代码（task-106，SESSION-117）

## 1. 本楔交付
`port/src/psx_core.{h,c}` + `bleem_port --psx-selftest`（17 断言全 PASS）——
把逆向定案首次落成 port 的**可运行对等代码**：
- **状态块构造**（docs/82 槽位/算式逐一镜像）：`+0x44`=RAM(0x328400 alloc)、
  `+0x48`=RAM−0xBFC00000(BIOS 窗)、`+0x2C`=scratch−0x1F800000(MMIO)、
  `+0x24`=scratch+0x400(GPU 端口族基)——H 时代全部 MMIO 常数自此在 port 内有实体。
- **地址翻译**：RAM 主窗+kseg 镜像别名、BIOS 窗(delta 平移)、MMIO 窗
  (0x1F801040/0x1F801800/0x1F801120 族)、越界防护——读写 roundtrip 实证。
- **服务分派表 ABI**（docs/81）：表基址+偏移取指再调、空槽守卫、分派日志计数——
  "0x4022a4+索引 call"的 Linux 函数指针对等物。

## 2. 工程决策（诚实记录）
- **MAP_32BIT 而非 MAP_FIXED**：原件 32 位进程地址天然 <2^32，port 用 MAP_32BIT
  维持 uint32 状态块槽无截断语义（首版 SEGV 即 64 位地址截断事故，已修+32 位
  不变式防回归）；MAP_FIXED 原址直置（0x80000000 域）与 host↔guest 真指针混用
  列入楔2。
- **CHK 宏双Eval 事故**：selftest 宏内 cond 被 printf+if 求值两次，
  含副作用的 svc 分派断言 nlog 双计(6≠3)——固化"断言宏必须单次求值"进 port 侧。

## 3. 范围声明（防误读）
本楔=**骨架**（内存图+翻译+服务 ABI 三件套），不含 CPU/GPU/CD 仿真——
port 显示面/解包面无改动（final_check 回归在 host98 D 项实证）。
薄引擎后续楔路线：楔2 MAP_FIXED+BIOS 装载 → 楔3 MIPS 核(可移植 rewrite/host JIT 经验)
→ 楔4 设备寄存器堆接真语义(docs/57 表) → 楔5 帧链+服务全表。月级战役已按用户意愿启动。
