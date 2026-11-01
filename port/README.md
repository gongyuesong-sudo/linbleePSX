# linbleePSX — 原生 Linux PSX 模拟器 (零 BIOS · 零 payload · 清洁房)

原生 Ubuntu 可运行 PlayStation (PSX) 模拟器：C11 + CMake，显示走 SDL2/Vulkan，
不装载、不执行、不依赖任何有版权工件 —— 无 SCPH BIOS、无 `payload.bin`、
无 golden dump。BIOS 由自研清洁房 HLE 内核替代；盘读与 ISO9660 为 100% 自研实现。

## 能力表

| 子系统 | 文件 | 状态 | 验收 |
|---|---|---|---|
| CPU r3000 解释器 | `src/psx_cpu.c` | MIPS I 整数 + 延迟槽 + 异常/CO0 eret | `--cpu-selftest` 16 断言 |
| 内存映射/MMIO | `src/psx_core.c` | RAM/KUSEG/BIOS 窗/MMIO 翻译 + 严格对齐 | `--psx-selftest` 31 断言 |
| I/O 设备 | `src/psx_dev.c` | 定时器/DMA/IRQ/GP0/GP1/CDROM 寄存器面 | `--dev-selftest` 10 断言 |
| HLE 内核 (替代 BIOS) | `src/psx_hle.c` | A/B/C rootcall 分派、LoadImage/Execute、putchar、堆、事件环、文件 API、临界区 | `--hle-selftest` 20 断言 |
| CD-ROM 读取器 | `src/psx_cdrom.c` | BIN/CUE + ISO9660 (ECMA-119)、2048/MODE1-2352/MODE2-2352，自研零外部库 | `--cd-selftest` 6 断言 |
| 冷启链 | `psx_hle_coldboot` | 盘上 PSX.EXE → 头暂存 → LoadImage → gp/sp/ra 武装 → Execute | `--boot-selftest` 7 断言 |
| GPU v1 软光栅 | `src/psx_gpu.c` | psx-spx 家族解码：填充/传输(0xA0)/矩形精灵(0x60-0x7F)/多边形(0x20-0x3F, Gouraud/四顶点)/环境 E1-E6/显示区 | `--gpu-selftest` 9 断言 |
| GPU Vulkan 硬件加速 | `src/psx_gpu_scanout.comp` + `src/vk_host.c` | compute 扫描输出：VRAM 上载→设备端 RGB555→RGBA8→blit present，CPU 零逐像素；`--gpumode hw\|soft` | X11 实窗出画，`--psx-window` |
| 端到端 | `--psx-window` | 自造 ISO → 冷跳 → guest 程序写 GP0/GP1 → GPU 绘制 → Vulkan 硬件 present | 门 `validate_host124.sh` |

## 构建与运行

```sh
cmake -S . -B build && cmake --build build -j4
./build/bleem_port --help
./build/bleem_port --psx-window --seconds 5        # Vulkan 硬件扫描输出实窗
./build/bleem_port --psx-window --gpumode soft     # 软件光栅对照
./build/bleem_port --psx-selftest                  # 全部八面 selftest 同理
```

依赖：SDL2、Vulkan loader（硬件 present；无 GPU 环境可 `--gpumode soft`）。
不需要、也不会读取任何 BIOS 文件；`BLEEM_BIOS` 仅为历史取证线兼容开关，
清洁房全部路径与它无关。

## 零版权纪律

- 仓库不含也不接受：SCPH BIOS、`payload.bin`、ISO/EXE/PBP/CHD 盘镜像、
  golden dump、解密流。发布镜像 `.gitignore` + CI 级负测试（暂存区命中即 rc=2 拒收）。
- rootcall 命令号仅登记公开来源（PSn00bSDK 桩表、公开 ROM 静态锚、公开文献），
  未登记槽 = 未实现，绝不臆造。
- 零售盘运行需用户自有镜像，运行时装载，永不入库；HLE 覆盖面对零售 EXE
  仍在扩展（见下）。

## 现状与路线

已通：CPU→MMIO→HLE→CDROM→冷跳→GPU(软/硬)→窗口，全链零版权，八面 108 断言全绿。
进行中 (task-133+)：零售盘兼容性夹具扩展、纹理映射采样面、GTE、SPU、Pad。

## 目录

- `src/` 模拟器源码（清洁房主线：`psx_*.c`、`vk_host.c`、shader）
- `src/lzfmt.c`、`src/boot.c` 历史取证工具面（仅在用户自带工件时可用，非运行时依赖）
- `tools/final_check.sh` 全量回归；上游工程账本见 rewrite 文档系列（docs/109、docs/110）
