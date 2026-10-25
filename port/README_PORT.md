# bleem! demo v1.4 → Ubuntu 原生移植报告

日期: 2026-10-25 · 工具链: REA 6.5.0 / Ghidra 12.1.4 (headless) · 语言: C11
运行环境: Ubuntu (x86-64) · 窗口: **X11 + Wayland (SDL2)** · 显示加速: **Vulkan**

## 用户裁定的三条硬约束

1. **NATIVE-ONLY** —— 不使用 Wine、不模拟 Windows、不执行任何原始 x86-32
   代码；全部可恢复逻辑以 Linux 原生 C 重写。
2. **去除原 demo 功能限制** —— 端口一律按完整版语义:

   | 原 demo 限制 (readme.txt + 静态证据) | 端口状态 |
   |---|---|
   | 限时自动退出 | **无时限** (`time_limit_sec=0`) |
   | 音效/音乐禁用 | 功能集开放 (`sound=1`) |
   | 记忆卡禁用 | 开放 (`memcard=1`) |
   | Direct3D 禁用 | **Vulkan 硬件加速** (`gpu_accel=1`) |
   | 彩色电影禁用(仅黑白) | 开放 (`color_fmv=1`) |
   | MMX/3DNow 禁用 | 原生 CPU (无需旧扩展开关) |
   | 调试器检测 (错误码 5/6) | **不复存在** (保护不属移植范围, 端口无自检门) |

3. **X11 与 Wayland 双显示模式 + Vulkan 加速** —— `--selftest --driver
   x11/wayland` 双路径均在本机真 GPU 上 3 帧 present 通过。

## 架构 (证据 → 代码)

| 端口模块 | 对应原始部件 | 证据 |
|---|---|---|
| `src/boot.c` | 引导桩 FUN_00401021 (Win9x 门→自映射→CALL 桩) | rewrite/docs/02 (逐指令台账) |
| `src/vk_host.c` | DirectDraw/D3D 面 (demo 中禁用) | Direct3D 替代品即本层 |
| `src/resources.c` | .rsrc: 25 图标组 + 20 对话框模板 + 版本串 | 逐字节提取, DIB/模板解析器原生重写 |
| `src/lzfmt.c` | 追加区桩 0x55338: 位流/LZ/RAND/模块描述符 | rewrite/docs/03 (17 函数反编译) |
| `src/bleem_native.h` | 布局常量/错误码/功能集 | sha256 b61ee13e…09da |

原 Win9x 错误码 1..6 在端口中重映射为 Linux 语义 (平台门→显示/
Vulkan 可用性; reinstall→文件完整性; 调试器类→**移除**)。

## 运行

```sh
make || (cmake -S . -B build && cmake --build build -j8)
./build/bleem_port                      # 引导校验 + 全功能横幅
./build/bleem_port --window               # 窗口模式: 常驻交互窗口 (ESC/关窗退出)
./build/bleem_port --window --seconds 10  # 限时运行
./build/bleem_port --selftest --driver wayland
./build/bleem_port --render-ui out.ppm  # 解析原图标/对话框并渲染 UI
./build/bleem_port --payload-selftest ../rewrite/data/payload.bin
```

## 诚实边界

追加区 287,732B 为压缩/加密载荷（熵 7.32），其中 `view+0xFA60` 之后的
PSX 引擎本体在无 Wine/无执行约束下**静态不可恢复**（见
`../rewrite/docs/05_payload.md` 与 `build/payload_report.txt`）。本端口
交付的是全部静态可恢复系统的原生实现 + 已记录格式边界的格式引擎。

## 验证记录 (harness 台账)

task-001..005 全部 PASS: 环境 / booter 校验(布局+magic+RAND 折叠) /
x11+wayland 双驱动真 GPU / UI 渲染 76 色采样 / 格式引擎 dry-run。
`tools/final_check.sh` 一键复验全链。全程零执行原代码（NATIVE-ONLY 守卫
含自动检查）。
