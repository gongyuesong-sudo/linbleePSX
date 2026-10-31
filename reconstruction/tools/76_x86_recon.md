# 76 — x86 战线楔1：demo-v1_4.EXE 静态侦察基线——宿主主体在 overlay（task-093，SESSION-102）

## 1. PE 结构基线（stdlib 手解，与金标准 sha `b61ee13e…` 同文件）
```
size 351,732B · PE@0x270 · i386 · 4 sections · image_base 0x400000 · entry RVA 0x1000
.text  VA 0x1000  VSize 0x4A0(1,184B)  ← 仅 loader stub
.idata VA 0x2000  VSize 0xC74
.rsrc  VA 0x3000  VSize 0xDCE4(56KB)
.reloc VA 0x11000 VSize 0x54
节表覆盖至 0xFA00 → OVERLAY = 287,732B(0x463F4)
```
**判定**：入口 `.text` 只有 1,184 字节——EXE 明文段不含宿主主体；
**overlay 287,732B 是主体容器**（三流 473,417B 解压态 > overlay → 压缩载荷自洽，
与 payload.bin 提取管线一致）。x86 宿主码要么在 stub 之后的解压流里，
要么是 overlay 中另一路未知载荷。

## 2. 导入面 = 宿主能力图第一稿
6 DLL / 114 API（`tools/x86_imports.json` 落盘，含 ordinal 噪声名待修）：
`KERNEL32×43 · USER32×55 · ADVAPI32×7 · GDI32×7 · SHELL32×1 · COMCTL32×1`。
- **无静态导入 DirectDraw/DSound/WinG**（1999 时代特征）——显示/声音走
  `LoadLibrary/GetProcAddress` 运行时解析（KERNEL32 族内含）或 GDI 直绘兜底；
- USER32 消息族 55 个 = 标准 Win32 消息循环宿主；
- 声音 0 静态导入 → DSOUND/waveOut 全动态。
→ 静态导入面薄，**宿主能力真实面藏在动态解析**——这正是 A3 终判"唤醒协议在宿主码"的
文件级佐证。

## 3. 时间戳异常
PE timestamp=523020（1970-01-06）vs 文件 mtime 1999——打包器伪造/清零旁证。

## 4. 楔2 定靶
① REA `open_binary`（PE 原生支持）反汇编 1,184B stub 全量：追控制流出口
（解压例程入口 + overlay 读取偏移 = 载荷布局钥匙）；
② overlay 结构扫描：已知三流头特征 + x86 函数序言密度图（找"第二路 x86 载荷"）；
③ ordinal 名表修复（x86_imports.json 补全）。
