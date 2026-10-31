# bleem! demo v1.4 —— REA 逆向重建工程

目标: `demo-v1_4.EXE` (bleem! PlayStation 模拟器 1999 试用版引导器, PE32 GUI i386)
SHA-256: `b61ee13e2377e8305ede4e8be9752f83163368fadb19f0b3e40774d64e7e09da`
工具链: REA 6.5.0 + Ghidra 12.1.4 (headless), 全部结论挂在 Evidence 上。

## 这是什么

`demo-v1_4.EXE` 不是模拟器本体。它是一个 **自映射引导器**:

1. 只允许 Win9x (`GetVersionExA`, `dwPlatformId==1`)
2. `CreateFileA + CreateFileMappingA(PAGE_READONLY) + MapViewOfFile` 把**自己的文件镜像**映射进内存
3. `CALL view+0x55338` —— 跳转进尾部追加区的明文运行时桩
4. 桩 (2,748B 明文) 是一个完整的**私有运行时加载器**: 自定义位流 LZ 解压器 +
   5 模块重定位器 + `RAND` XOR 完整性校验 + API 编号间接派发
5. 被解压/解密的追加区 (287,732B, 熵 7.32) 才是 demo 引擎本体 —— 含配置对话框、
   防调试器、限时退出、CD 检查等。**2026-10-25 更新：已完整解密**——原生 Linux 上
   用其自身 2.7KB 明文解压器解出全部 3 个流（473,417B 明文，含 466KB PSX 引擎），
   位引擎语义逐指令还原（RCR 进位链 + 幻影位 + 双位序）。见 `docs/06_decryption.md`
   与 `decrypted/`；未执行任何加密区代码。

## 目录

| 路径 | 内容 | 完成度 |
|---|---|---|
| `src/loader.c` | 引导桩 (原 0x401000/0x401021) 逐指令重建 | ★ 完整, 经 REA 反编译对拍同构 |
| `src/stub_entry.c` | 追加桩入口 (原 0x55338) | 高置信骨架; 块表基址等 [未知] 已隔离 |
| `src/lzss.c` | 位流解压器 (原 0x5577a + 0x55960..0x55998) | 位读取器完整; 令牌树逐支转写 [中置信] |
| `src/modules.c` | 5 模块运行时链接器 (原 0x5546d/0x55597/0x5556d) | 流程完整转写; 字段常量部分 [未知] |
| `include/bleem.h` | 全部常量/错误表/ABI 约定 | 逐字对照原版 |
| `rc/bleem.rc` | 版本信息资源 (与原版逐字一致) | 完整 |
| `data/` | 原始 .rsrc 叶子 68 项 + payload.bin + stub.bin | 逐字节提取 |
| `tools/` | 提取/拼接/资源 dump 脚本 | 可重复运行 |
| `docs/` | 5 篇分析报告 + Evidence 台账 | — |
| `build/demo_rewrite.exe` | 交叉编译产物 (PE32 GUI, 与原版同类) | `make` 可复现 |

## 构建

```sh
make            # 需要 i686 mingw-w64 (默认 ~/.local/mingw, 或 make CC=i686-w64-mingw32-gcc)
make payload    # 实验: 重建 loader + 原版追加区拼接 (布局对照, 不可发布)
```

## 验证方式(已执行)

- `build/demo_rewrite.exe` 用**同一套 REA 反编译**, 伪代码与原 `entry` 的
  API 序列、错误计数逻辑、`0x55338` 调用、`0x2`/`0x11010` 常量全部一致。
- 未运行任何原版/重建代码 —— 桩语义全部来自静态证据, 推断处均标注 [推断]/[未知]。

## 法律提示

原版是 bleem, LLC 的商业软件(公司 2001 年已结业)。本工程仅为互操作/研究性
重建; `data/payload.bin` 是原样转储, 请勿再分发。

## 项目终局状态(2026-10-31, 47 个 harness 会话)

- **原生移植已达成**: 见同级 `../port/` —— Linux 原生 `bleem_port`
  (X11+Wayland 后端, Vulkan 呈现, demo 限制全解除); Wayland selftest
  在真合成器现场 PASS。
- **引擎已完整解密**: `decrypted/` 三流 473,417B 逐字节金标准。
- **PSX BIOS/宿主链研究(H 系列)已触边界**: BIOS ROM 反编译全像、
  MIPS 块级 JIT(单元+TICK 30M 块零失败)、金标准 RAM/CPU 采样-重放管线、
  第一方 homebrew 端到端、CDROM/C2 HLE 命令环闭环均已落地;
  冷跳链达 Execute! 的剩余钥匙 = 完整设备驱动面(GPU/CDROM/DMA 真值),
  七道墙实证与两案成本对比见 `docs/33_final_route.md`, 归档/继续决定
  提请用户。游戏内容运行既定走 DuckStation。
