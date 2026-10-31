# 77 — x86 战线楔2：两级 stub 破解——宿主二段引导器定位（task-094，SESSION-103）

## 1. 一级 stub 全流程判读（objdump raw，1,184B）
```
0x401000  jmp 跳过 0x1002-0x1021 数据域(7 项 RVA 指针表 0x2a9e..0x2b06;
            文件含 "bleem!" 串; 早前 objdump 噪声勿当字符串读)
0x401021  GetModuleFileNameA(self) → CreateFileA(GENERIC_READ)
        → CreateFileMappingA(PAGE_READWRITE) → MapViewOfFile(FILE_MAP_WRITE)
        → lea 0x55338(%eax),%edx / call *%edx      ← 核心一行
        → 失败面: MessageBoxA(错误码表 0x4011c4[] 索引) → ExitProcess
```
**`call 映射基址+0x55338`** = 整个 EXE 文件映射后跳 **文件偏移 0x55338**（overlay 内，
overlay 相对 0x45938）——宿主执行权交接点。

## 2. 二段引导器验真（0x55338 起 2,780B 至文件尾）
反汇编**全程合法**：手工位置无关码——`sub $0x4010c4,%eax` 基址修正、
构建 5 组描述符表（`lea 0x9c(,%ecx,4)` 步长循环 + `rep stos` 清零）、call 链 0x55384→0x55586。
与 overlay 序言密度峰 0x45800/0x45C00（overlay 相对）互证——**overlay 尾部 ≈2.8KB 是二段 loader**，
非宿主主体（主体量级不符，仍在压缩载荷内）。

## 3. overlay 布局第一稿（287,732B）
```
[0x000000 - 0x45937]  压缩载荷区(含 zlib@0x19635; 内嵌 PE 候选 MZ@0x1b64f/0x29b3d)
[0x45938 - 0x463F3]  二段引导器(2,780B, 位置无关)
```
（gzip 2B magic 命中判为噪声；三流压缩体待二段解包例程确认后精确定位。）

## 4. 工具注记
capstone 不可用；`objdump -D -b binary -m i386 --adjust-vma` raw 反汇编全程可用（0 依赖）。
ordinal 名补全推迟（价值低，imports 71 命名已够能力面判读）。

## 5. 楔3 定靶
二段 2,780B 全量反汇编分函数 → 追它调用的**解压例程**（宿主侧解包器——
可能与 PSX 侧位引擎不同物）与解包落点/大小表（0x401124 域）→ 建立
"二段 → 解压 → 宿主主体 VA" 的完整启动链；内嵌 MZ×2 验真（拖出小 PE?）。
