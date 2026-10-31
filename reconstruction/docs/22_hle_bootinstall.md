# 22 — HLE kernel-RAM 终态预放 v1（H-13b attempt2，SESSION-34）

## 1. 实现（host/mips_jit.c，env `H_HLEBOOT=1`，首个 serve 时安装=boot_init 之后）
- `hleboot_install()`：
  - 内核镜像复制 ROM 0x10000→RAM 0x500（0x8BF0，FUN_bfc00420 等价）
  - model 串 @0x9100（避开向量页）
  - 分发器 boot 状态机 9 个 C2 callee 桩（0x32E24→返 2 成功推进；33D58/34998/336EC/376AC/37FA8/37CC8/5AA00/5719C）
  - boot 完成态 `_DAT` RAM 0x79E44=2、0x79E5C=1；root2 头 0x150/0x158
  - TTY devsw 结构（0x90000，PSX deviceSwitch 0x28 布局+桩函数）写入 stdin/stdout/stderr 槽 0x64/0x68/0x6C（**破 0xFF 哨兵**）
  - cdrom: devsw 结构（0x66B00+桩 0x66C00，open/read 返 -1=无盘）+ DevCtl 0x66940=0x3B/0x66948=0x32
  - RAM 向量页 j stub（0x80/A0/B0/C0 → j 0xC80）+ 0x84-0xBC fence（jr ra）+ 写保护 g_vecprot（[0x80,0xC0) 非零写重定向 0x9300 影子页）

## 2. 实证推进（相对基线 b00b1d24 零海）
- devsw 0xFF 哨兵停机**消除**；分发器状态机真跑：BLK 尾 `b0032a90→…→b0032e10`（线程枚举环）
  `→ bfc1f648/680/694（设备初始化路径, RAM 0x6D054 状态字节×50/51 比对）→ bfc1f69c`
- 进入 **boot 动画/model 门** `bfc02edc → bfc02f10`（docs/16 model_probe 域）——引导面历史最深。
- 新停机两处：
  1. `EMITFAIL pc=0x84 op=74740008`：内核把 "tty:" 名串写进 RAM 0x84-0x8B（向量页=数据页复用），
     某返回路径 ra=0x84 落入数据。fence+写保护后仍现（写发生在 install 前窗口/半字合并基值）。
  2. `EMITFAIL pc=0xF01 op=9cac43ff`：动画 init 调未装 RAM 的 C2 动画体指针（奇地址=数据当指针）。

## 3. attempt3 判定条件（或收束判定）
- 两处停机都指向同一事实：**HLE 终态需要真机 RAM dump 对齐**（0x84 页复用协议、动画体地址
  均不可猜）。金标准获取途径（按成本）：
  a. 编译 PsxEmu `boot_runner`（其 README 命令 `boot_runner bios/SCPH1001.BIN --frames 120 --dis 80000C80:200`）导真内核 RAM；
  b. DuckStation 带 GUI 手动跑 boot 后 dump RAM（AppImage Qt 插件问题需先修）；
  c. 放弃 boot 动画段：HLE 直接伪造"动画完成"跳转目标（需 dump 佐证 0x150 头终值与 0xF01 语义）。
- 无 dump 前继续猜 = task-024 式烧 attempt，不推荐。

## 4. 复验
- `H_BIOSEXN=1 H_HLEBOOT=1 /tmp/atest` → `BOOT blocks=206`（BLK 尾含 bfc02edc/2f10 动画门=推进证据）
- 基线（无 HLEBOOT）停机 b00b1d24 零海；`bash tools/validate_host22.sh` 回归不破坏。
