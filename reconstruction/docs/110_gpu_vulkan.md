# 110 — GPU v1 + Vulkan 硬件扫描输出 (task-132, SESSION-139)

## 交付
- `port/src/psx_gpu.{h,c}` — GPU v1：1024×512 VRAM、RGB555、GP0/GP1 全家族、
  软件光栅(确定性 selftest 面) + `psx_gpu_attach_hw()` 硬件 present 钩子。
- `port/src/psx_gpu_scanout.comp` / `_spv.h` — Vulkan compute 扫描输出内核
  (glslc -mfmt=c 内嵌 SPIR-V，16×16×1 workgroup，push constants origin/size/words_row/disp_on)。
- `port/src/vk_host.c::vk_host_present_gpu16` — VRAM 16bpp 上载 device buffer →
  compute 设备端 5551→RGBA8 → buffer→image copy → blit/present；CPU 零逐像素、零读回。
- `--psx-window [--gpumode hw|soft]` — 图形 HBEXE(蓝填充+红 0xA0 块+绿 0x7C 精灵)
  经自研 ISO→冷跳→Execute 链在 X11 实窗出画；hw=1 Vulkan 通道 46–125 帧/2s，exc=0。
- `--gpu-selftest` 9 断言 (G1 显示区/G2 填充/G3 传输/G4 精灵/G5 平面三角/G6 RGBA 帧/G7 关显示黑/G8 Gouraud/G9 环境 E1-E6)。

## psx-spx 家族正字 (命令字 >>29 = family)
- 0x02 填充(3 字头，色 low15)；0x1F IRQ1；0xE0 复位。
- 0x20-0x3F 多边形：bit28 Gouraud、bit27 四顶点、bit26 纹理；word 数 = 1+n*(1+tex)+(gouraud?n-1:0)。
- 0x60-0x7F 矩形/精灵：size code (w>>27)&3 = 0 可变 / 1=1×1 / 2=8×8 / 3=16×16。
- 0xA0-0xBF CPU→VRAM 传输(3 字头：命令、目标 xy、尺寸；数据半字对低先)。
- 0xE1-0xE6 环境：绘制偏移/绘制区域/纹理页(x bits0-3, y bits8-9)/CLUT/窗/遮罩；E1 bit25 原始纹理模式。
- 顶点坐标：x bits0-10、y bits16-26 各 11 位有符号 —— `cy11(v) = (int)(int32_t)(v<<5)>>21`。

## 实证坑 (已钉死)
- CPU→VRAM 是 0xA0 族，不是 0x03。
- GLSL SSBO 必须实例名访问 `vram.px[]`/`fb.outp[]`。
- `xfer_exec` 数据计数用 `pk.wn - 2`（header 后净字数），`pk.data_words` 满包时已清零不可用。
- 夹具 ORI 宏必须带 rs=rt，否则 LUI+ORI 惯用序列把高位 OR 掉成 `0x0|imm`。
- 合成器 codebuf 必须是独立缓冲：builder 内部 `memset(img)` 先于取码 → 自拷贝清零 bug。
- RGB555→888 精度 = <<3 → 满值 248，测试期望勿写 255。

## 门
`bash tools/validate_host124.sh` → ALL PASS (GATE_RC=0)：
八面回归 psx=31 cpu=16 dev=10 eng=9 hle=20 cd=6 boot=7 gpu=9 全 0 FAIL；
hw 实窗 `frames=125 hw=1 fill=76800 spr=256 xfer=256 gp0=267 gp1=5`；soft 对照 PASS；
日志零 BIOS；GitHub 同步推平 origin/main。

## 边界 (留 task-133+)
- 纹理映射多边形采样面仅 selftest 级；GTE、SPU、Pad、CD-ROM 异步时序未接窗口链。
- 真零售盘(LEVEL-1 可执行)只读链已通(自造 ISO)，零售 EXE 头变体兼容性待更多夹具。
