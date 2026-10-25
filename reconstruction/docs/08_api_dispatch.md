# 08 · 引擎 API 派发机制（实证定稿）

对象：`decrypted/stream_10a4c_L71cbf.bin`（PSX 引擎体，0x71CBF=466,111 B）。
方法：全镜像机器码扫描（FF 15 / FF /2 modrm）+ 桩代码 objdump 交叉验证。

## 一、派发机制（两条通道）

1. **slot0 直址**：`FF 15 A4 22 40 00`（`call dword ptr [0x4022a4]`）共 **11 处**
   （0x27/0x55/0x83/0xac/0xd8/0xfd/0x118/0x12f/0x146/0x15d/…）。
   `[0x4022a4]` = 模块坐标槽 0 = 装载器回填的第一个 API = **VirtualAlloc**
   （`eng_0x4` 入口三连分配 640 KB RAM 等直接经此槽，docs/06 §引擎身份证据）。
2. **寄存器间接表**：`FF 40 disp8`（`call dword ptr [eax+disp8]`）**91 处、24 个不同槽**
   ——EAX = **引擎入口时由装载器传入的 API 表指针**（非编译期常量）。
   注意：部分站点被 Ghidra 线性反汇编吞掉操作数（0x53/0x8b/0xdb/0xff 类 disp8 是
   对齐噪声），真实独立槽数 ≤24。

| 槽偏移 | 槽号 | 调用点 | 判定 |
|---|---|---|---|
| +0x00 | 0 | 11×直址+表 | VirtualAlloc（eng_0x4）|
| +0x08 | 2 | 19 | 高频——内存/句柄类（候选 VirtualFree/GlobalFree）|
| +0x10 | 4 | 5 | |
| +0x18 | 6 | 9 | |
| +0x20 | 8 | 6 | |
| +0x2c | 11 | 5 | |
| +0x34 | 13 | 5 | |
| +0x50/+0x54 | 20/21 | 4+7 | |
| +0xe8 | 58 | 6 | |
| 其余 (0x05,0x0c,0x14,0x1c,0x24,0x30,0x38,0x64,0x68,0x6c,0x70,0x8b,0xdb,0xff) | — | 1-4 | 低频/噪声混合 |

## 二、79 项表声明链（闭合 task-004「Win32 API 依赖」缺口）

- 桩 `FUN_00055895`（0x455895）call1 = `[table+0](0x2a0000, 0x4011e0, 0x4f)`，
  **0x4f=79 = 表长**；第二参 = `0x4011e0`（= `0x4022a4-0x10c4`）——**名称池区**。
- boot32 内存实证（SESSION-4）：`0x4011e0` 区内容 = ASCII 名池
  `…ShellExecuteA\0KERNEL32.DLL\0CloseHandle\01\0CreateFileA\02\0CreateFile…`。
- **结论**：装载器按 79 名池 + 模块名解析并回填指针数组，作为 EAX 传入引擎入口。
  **静态无法整表还原**——`.data` 区磁盘原始影像含内嵌零（boot32 读到
  `ShellExecuteA→KERNEL32.DLL` 之间的 0x00 间隔），运行时由桩重组，
  完整名池以「boot32 内存 dump + 主 EXE .idata 119 项」为权威参考（docs/07 §二）。

## 三、证伪记录（防再犯）

- **`img+0x10068 指针表` 说法作废**：扫描证实该区域为均匀指令码
  `8b 01 01 00`（mov eax,[ecx] 区段），不存在 {槽索引,偏移} 指针表。
  docs/06 相应行已改写为本机制引用。
- **`add esi,[esi-2]` 重定位链不存在**：引擎流头 `dword[1]`=J=0x448e8 为位源跳距，
  非重定位表偏移（SESSION-5 反汇编复核）。
- **重定位机制 = `FUN_000553CA`（0x4553ca）**：块表驱动——
  `edi`=块表（VIEW+0xfa0c，16 B/条），循环条目：
  `test [reg+0x34],-1` 已重定位自愈跳过；`add eax,[ebp+0x30]` delta；
  `and ebx,0xfffffffc`+`xor eax,'RAND'`+逐 dword XOR fold 与 `blk[+0xc]` 校验和比对
  （docs/06 §RAND fold 同源）；失败则 `lea esp,[ebp-4]` 报错。

## 四、可复核扫描命令

```bash
python3 - <<'EOF'
import struct, collections
d=open('decrypted/stream_10a4c_L71cbf.bin','rb').read()
s=collections.Counter(); f=[i for i in range(len(d)-6) if d[i:i+2]==b'\xff\x15'
   and struct.unpack_from('<I',d,i+2)[0]==0x4022a4]
print('FF15[slot0]:',len(f))
for i in range(len(d)-3):
    if d[i]==0xff and (d[i+1]&0xc7)==0x40: s[d[i+2]]+=1
print('call[eax+disp8] slots:',dict(sorted(s.items())))
EOF
```
