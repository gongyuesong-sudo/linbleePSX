# 10 — H-6a 覆盖实证：img 深层从未执行（154 函数前提被证伪）

日期：SESSION-16。证据：`host/build/htrace.log`（全 img 无界 TF trace，RANGE 0x400000..0x472000）。

## 一、实测数据

| 指标 | 值 |
|---|---|
| trace 总行数 | 162,659 |
| img 地址段（0x400000-0x472000）唯一 eip | **131** |
| img 段最大 eip | **0x4002b7**（入口 init 出口 `ret`） |
| img 段最小 eip | 0x400004 |
| img 深段（>0x4002b7）唯一 eip | **0** |
| 引擎可见指令总数（img 段行数） | 275 |

trace 处理器对**任何** eip 都记录并重设 TF（仅离开 img 时停步）——覆盖统计无范围盲区。

## 二、结论（推翻此前假设）

1. **入口 init（0x4..0x2b7）不调用 img 深层任何代码**：全部可见行为 = 服务调用循环（VirtualAlloc/svc/BIOS 窗）+ 跳 `0xBFC00000`
2. **`docs/07` 的"154 个引擎函数"前提被证伪**：Ghidra 把 **JIT 模板字节码**当反汇编成"函数"。交叉佐证：172 张 dispatch 表 @img+0x5AC4C（表驱动 JIT）+ 引擎要求 `call [alloc块]`/跳 BIOS 向量（期待**生成码**而非静态码）
3. img 的真实身份：**PSX→x86 JIT 模板库 + 薄驱动**（275 条指令）。运行时链 = 宿主交付 BIOS/游戏 MIPS 码 → 引擎按模板 JIT 生成 x86 到分配块 → 跳入执行

## 三、移植路线影响（重大，需用户决策）

- **纯 HLE 路线（无 ROM）不可达**：JIT 驱动主体不在 img 明文中（在已证伪的"154 函数"= 模板区，或由主 EXE stub 侧逻辑驱动），无 PSX 码输入则整条链不启动
- **路线 B'（提供合法 BIOS ROM）**：可验证 JIT 链（引擎是否有完整驱动仍需运行时观察）——法律合规前提是用户自有 ROM
- **路线 C（务实收敛）**：img 逆向成果作为文档/研究价值保留；游戏可运行性走 **DuckStation**（已下载 `/home/gill/Applications/duckstation-x64.AppImage`）
- 本 harness 的 host/validator 基础设施对任一路线均可复用

## 四、复现命令

```bash
gcc -m32 -O0 -g -no-pie -o host/build/htrace host/htrace.c host/tramp32.s
timeout 280 ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin 0x400000 0x472000
grep '^004' host/build/htrace.log | cut -c1-8 | sort -u | wc -l   # 131
grep '^004' host/build/htrace.log | cut -c1-8 | sort -u | tail -1 # 004002b7
```
