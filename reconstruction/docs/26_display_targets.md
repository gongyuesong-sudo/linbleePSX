# 26 — 显示层目标：X11 + Wayland + Vulkan（用户明语）

用户原话（verbatim，SESSION-34/35）：**"本项目显示要支持x11和wayland、vulkan加速"**。

## 约束与含义
1. linbleePSX 宿主显示层必须**同时支持 X11 与 Wayland** 两类会话（SDL2 双后端或原生
   xcb/wayland-client），合成/呈现路径走 **Vulkan 硬件加速**，不做纯软件 blit 终态。
2. 现有 `port/`（SDL2+Vulkan，X11+Wayland selftest，AGENTS.md 已验证
   `bash tools/final_check.sh`）即为显示层载体：任何引擎/BIOS 链落地都必须挂到该
   Vulkan 呈现面上，窗口自测 `./build/bleem_port --window`（ESC 退出）、
   `--selftest --driver x11|wayland` 保持全绿。
3. 金标准取证期间使用 RetroArch null video 仅是**无头取态手段**，不构成显示层降级；
   最终运行面以 port/ X11/Wayland+Vulkan 为准。
4. PSX framebuffer（VRAM→显存）上传应走 Vulkan staging→texture，VSync 时基与
   port/ 帧循环对齐（与 H-16 事件时基域一致）。

## 验收挂点
显示层回归以 port/ `final_check.sh` 为权威；本仓库（rewrite/）host 探针无 GUI 依赖，
不冲突。
