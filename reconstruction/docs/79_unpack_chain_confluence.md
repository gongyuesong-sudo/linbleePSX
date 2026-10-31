# 79 — x86 战线楔4：解包链与 docs/06 会师——x86 战役大半早已完成（task-096，SESSION-105）

## 1. 本楔最大发现：会师
`docs/06_decryption.md` 早已把 EXE 明文区 **[0x55338,0x55DF4) 的位引擎逆向到指令级**
(引用锚 0x55787/0x557a4/0x55960/0x5599F... 共 22 个，与楔3 函数图谱同一区)——
即 x86 战役在 H 时代已顺带完成一半。docs/45-78 的 x86 战线是**重新踩上旧图**。

## 2. 块表 16 字节条目 ×4（0xFA0C 起，本楔实读复核）
```
+1c fab7  → dst 0x8e8        chk 5A53CB18   (s1, comp 1,688B)
+2c 1014f → dst 0xe18|0x80000000 chk 8A369FFD (s2, comp 2,303B)
+3c 10a4e → dst 0x5a244|0x80000000 chk 4D709ECA (s3, comp 280,810B)
+4c 55338 → 0x44 域, chk@58 4D24AF3D, end@5c 55DF4  ← 校验和实证在码
```
区域链 fab7→1014f→10a4e→55338→55df4 与三流解压尺寸(0xfacc/0x1015c/0x10a4c)账目闭合；
**s3 区尾 = 交权行所在函数起点**，语义贯通：三流解包(块表驱动)→跳 base+0xFA00 桩续链。

## 3. DX 动态面实证（补全楔1"无静态导入"之谜）
stream_10a4c 明文串直证：`DDRAW.DLL / DSOUND.DLL / DINPUT.DLL / DirectDrawCreate /
DirectSoundCreate / DirectDrawEnumerateExA / USER32.DLL`——
**宿主图形/输入/声音 = 运行时 LoadLibrary 动态 DX**，一级导入面只留壳。

## 4. 0x55960 复核注记（诚实勘正）
0x55960 在**静态文件视图**呈现为描述符表构建序言(同 0x55338 型)，docs/06 称其为
get8——两视图差异在**运行时寄存器语境**(EBX 位缓存态)。位引擎同源性以
docs/06 已验证引擎(`shr1: cf=ebx&1; ebx>>=1` + je-tail RCR)为准，本楔不推翻。

## 5. 否证/勘正
- 楔3 "二段内 rep movsb=LZ 核" **修正**：LZ 解包核在**解包后的 s2/s3** 中；
  二段仅块表驱动+VirtualAlloc 落点管理。
- `0x2A002A` 位缓存 magic + `push 0x2a0000`(RW|SEC_RESERVE) 与 PSX 侧 scratch 页
  同址同源(桩协议文档已用 0x2a0000)。

## 6. 楔5 定靶
① 逐条比对 unpack_lib.c 引擎与 docs/06 锚 22 地址的指令语义(审计轮, 防历史漂移)；
② s1/s2 流(dst 0x8e8/0xe18)解压产物身份判定(模块数据表 0xe/0x9e)；
③ s3(466KB, dst 0x5a244=PSX 引擎本体)的宿主主循环入口定位——**真·宿主主体**。
