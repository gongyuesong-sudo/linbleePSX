# 89 — PE 战线楔2：fixed 原址三窗 + BLEEM_BIOS 装载——薄引擎从"平移模拟"进"原址直置"（task-108，SESSION-119）

## 1. 本楔交付（port/src/psx_core 楔1 之上）
- **`psx_core_init_fixed()`**：三窗 `MAP_FIXED_NOREPLACE` 原址直置
  `0x80000000`(RAM 0x328400)/`0xBFC00000`(BIOS 512KB)/`0x1F800000`(MMIO 0x9400)。
  选 NOREPLACE 而非 FIXED：与 port 自身段冲突=返回 EEXIST 硬失败(-5..-7)，
  绝不覆盖自有映射。fixed 态状态块槽退化为原址恒等（delta 槽=0，
  GPU 基=0x1F800400 原值）——原件 32 位进程天然如此，docs/82 语义零近似。
- **`psx_load_bios()`**（AGENTS 安全约束全执行）：`BLEEM_BIOS` 未设=0 保持 HLE；
  已设但缺失(-10)/尺寸≠524288(-11)/读败=**硬失败不静默降级**；装载后
  sha256 登记（`c->bios_sha`，ROM 字节不入 git）。
- **手算 SHA-256**（FIPS 180-4 紧凑实现，无外部依赖）：对 SCPH1001.BIN 产出
  `71af94d1e47a68c1…e99d3` 与 AGENTS 登记值**逐字符一致**——独立实现互证。
- **selftest Phase B 14 断言**（总 31/31 双态 PASS）：三窗原址/恒等槽/
  翻译恒等/kseg 别名经 translate/BIOS 窗越界 BAD/GPA 直写直读/SVC 表在位/
  BIOS 三态（unset=0、missing=-10、badsize=-11、带真 ROM=sha 登记+首指令
  `3c080013`=lui v0,0x8013 实读）。

## 2. 现场踩坑固化
- **0 别名窗不可直映**：`vm.mmap_min_addr`(64K) 禁裸 0x0 段 mmap——原件 kseg
  别名在 guest 页表层，Linux 对等=translate 软件别名（楔1 语义保留），
  直读仅限已映射 0x80000000 域。
- **C 优先级**：`*(uint32_t*)(uintptr_t)BASE + off` 是 `(读BASE)+off` 非地址算术，
  断言假 FAIL——cast 与 + 必须加括号（SEGV 一次+FAIL 一次两个现场）。
- SEGV 丢 stdout 缓冲致日志空象——诊断法=gdb bt + 独立探针，非加 printf 重跑。

## 3. 边界与后续
本楔仍不含 CPU/GPU/CD；fixed 态双核(楔1/楔2)并存、`--psx-selftest` 双相位全跑。
楔3=MIPS 核（rewrite/host/mips_jit 语义移植）；楔4=设备寄存器堆接 docs/57 语义表；
楔5=帧链+服务全表 37 槽。BIOS 首指令已可经 0xBFC00000 直读——楔3 起可跑真 BIOS 取指。
