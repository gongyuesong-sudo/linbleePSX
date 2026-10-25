# LinBleePSX — bleem! PSX 引擎的 Linux 原生移植与逆向重建

GPLv3。在原生 Ubuntu Linux 上重建经典 bleem! PlayStation 模拟器的引导、载荷解包与显示链路：
不使用 Wine，X11 与 Wayland 双显示后端，Vulkan 显示加速。

## 项目构成

```
port/             原生 Ubuntu 移植工程（CMake，纯 C）
├── src/          引导器、Vulkan 窗口层（X11/Wayland）、UI 合成、载荷格式引擎
├── tools/        资源提取、图像校验、一键 final check
└── docs/         移植设计与验证记录
reconstruction/   demo-v1_4.EXE 的逆向重建工程
├── src/          PE 引导器重建、明文桩语义还原、LZSS 解压、模块装配
├── include/      数据结构与 API 声明
├── docs/         01~06 号逆向报告（镜像布局/loader/桩/UI/载荷/解密格式）+ 证据账本
└── tools/        payload 解包工具链（native_unpack：C 解码器、金标准对拍、扫描器、Python 转写版）
```

## 已实现能力

- 引导链路原生重建：PE 追加区解析、自解压桩语义、Win95 loader 判定路径、模块装配顺序
- bleem! 私有自解压格式完整还原：递降 dword 位流、EBX 位缓冲、持久 CF、RCR 进位链、
  幻影位（shr 清零 → load+rcr，判决位取 rcr 后 CF、丢失位经 bit31 延迟重演）、
  get8 双位序与 0x5599F 全字节特例、六叶令牌树（1-8B 字面量 / 2B / 3B / 4B 匹配 /
  9-264B 长字面量 / len=get8+5 长匹配，gated 快取 9/10/12 位 + 短链续读）
- 独立 C 解压器 `reconstruction/tools/native_unpack/unpack_lib.c`（自含，无原码执行）
  + 与硬件执行金标准逐字节对拍的 selftest 框架
- 原生 Ubuntu 可执行程序 `bleem_port`：
  - `--window` 常驻窗口（X11/Wayland 驱动，Vulkan 呈现，约 30fps）
  - `--selftest --driver x11|wayland` 帧级显示自检
  - `--render-ui` 资源 UI 合成；`--payload-selftest` 载荷格式引擎干跑
- demo 功能限制按完整版本语义开放（无时间限制；sound / dynamic_sound / memcard /
  color_fmv / GPU accel 均不继承 demo 限制）

## 构建与运行（Ubuntu 22.04+）

```bash
sudo apt install build-essential cmake libvulkan-dev libx11-dev libwayland-dev libegl1-mesa-dev
cd port
cmake -B build && cmake --build build -j
./build/bleem_port --window                 # 窗口模式（ESC 退出）
./build/bleem_port --selftest --driver x11  # 显示链路自检
bash tools/final_check.sh                   # 全量回归
```

资源图标需在本地从原 exe 提取一次：`python3 port/tools/dump_resources.py <exe路径> port/data`。

解包工具构建：

```bash
cd reconstruction/tools/native_unpack
gcc -O2 -o selftest unpack_lib.c selftest.c && ./selftest   # 需自备 payload 参考件
```

## 功能状态与边界

本项目当前完成的是引导 / 载荷解包 / 显示 / 资源链路的原生重建。
PSX CPU（GTE）、GPU 光栅与 CDROM 仿真层仍在开发路线上（见 reconstruction/docs）。
项目不绑定、不内置任何 PlayStation 游戏镜像。

## 合规声明

本仓库仅包含：本项目自行编写的代码、格式与协议的技术描述文档、自行开发的工具。
不包含原始可执行文件、原始二进制资源或由其派生的解密/提取产物；
所有逆向分析在本地进行，仅用于互操作性研究与历史软件 preservation。
如权利人认为本项目存在问题，请提 issue，将立即处理。
