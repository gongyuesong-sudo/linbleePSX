# LinBleePSX — 原生 Linux PSX 模拟器（零 BIOS · 零 payload · 清洁房）

GPLv3。在原生 Ubuntu Linux 上运行的 PlayStation (PSX) 模拟器：C11 + CMake + SDL2/Vulkan，
X11 与 Wayland 双显示后端，Vulkan 硬件加速。
不装载、不执行、不依赖任何有版权工件 —— 无 SCPH BIOS、无 `payload.bin`、无 golden dump。
BIOS 功能由自研清洁房 HLE 内核替代；CD 读取与 ISO9660 为 100% 自研实现（零外部盘读库）。

## 能力（8 面 selftest 共 108 断言全绿；端到端实窗出画）

- CPU r3000 解释器（MIPS I 整数 + 延迟槽 + 异常/CO0 eret） — `--cpu-selftest`
- 内存映射/MMIO/定时器/DMA/IRQ 设备面 — `--psx-selftest` `--dev-selftest`
- HLE rootcall 内核（LoadImage/Execute/putchar/堆/事件环/文件 API/临界区；
  命令号仅登记公开来源，未登记 = 未实现，绝不臆造） — `--hle-selftest`
- CD-ROM BIN/CUE + ISO9660(ECMA-119) 读取器 + 冷启链（盘上 PSX.EXE → 跳板 → 用户码）
  — `--cd-selftest` `--boot-selftest`
- GPU v1：psx-spx 家族全解码（填充 / 0xA0 CPU→VRAM 传输 / 矩形精灵 / 多边形 Gouraud·四顶点 /
  环境 E1-E6 / 显示区） — `--gpu-selftest`
- **Vulkan 硬件加速扫描输出**：VRAM 上载 → 设备端 RGB555→RGBA8 compute → blit present，
  CPU 零逐像素；`--gpumode hw|soft` 双路 — `--psx-window` X11 实窗端到端
  （自造 ISO → 冷跳 → guest 写 GP0/GP1 → 光栅 → 硬件 present）

## 项目构成

```
port/             原生模拟器（清洁房主线）
├── src/          psx_cpu / psx_core / psx_dev / psx_hle / psx_cdrom / psx_gpu /
│                 vk_host + Vulkan scanout shader / 窗口入口
├── README.md     构建运行 + 能力表 + 零版权纪律
└── tools/        回归脚本
reconstruction/   demo-v1_4.EXE 历史逆向档案（纯文档与脚本，无原程序字节）
├── docs/         逆向报告 01~111 + harness 账本
└── tools/        取证/解包/同步工具链（不内置任何受版权工件）
```

## 构建与运行（Ubuntu 22.04+）

```bash
sudo apt install build-essential cmake libvulkan-dev libsdl2-dev libx11-dev
cd port
cmake -B build && cmake --build build -j
./build/bleem_port --psx-window --seconds 5      # 端到端实窗（Vulkan 硬件出画）
./build/bleem_port --gpu-selftest                # 八面 selftest 同理
```

不需要、也不会读取任何 BIOS 文件；`BLEEM_BIOS` 仅为历史取证线兼容开关，
清洁房全部路径与它无关。无 GPU 环境用 `--gpumode soft`。

## 零版权纪律

- 本仓库不含也不接受：BIOS ROM、`payload.bin`、ISO/EXE/PBP/CHD 盘镜像、golden dump、
  解密流。发布管线对暂存区做二进制禁列扫描，命中即拒收（负测试实证拦截）。
- 零售盘运行需用户自有镜像，运行时装载，永不入库。
- 历史注记：本工程源自对 bleem! demo v1.4 的合法互操作性逆向研究；
  模拟器主线（清洁房线）不链接、不执行任何原程序代码，
  独立于 `reconstruction/` 档案推进。

## 合规声明

本仓库仅包含：自行编写的代码、格式与协议的技术描述文档、自行开发的工具。
不包含原始可执行文件、原始二进制资源或由其派生的解密/提取产物；
所有逆向分析在本地进行，仅用于互操作性研究与历史软件 preservation。
如权利人认为本项目存在问题，请提 issue，将立即处理。
