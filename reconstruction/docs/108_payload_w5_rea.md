# 108 — ③战役楔5：payload.bin 内容面 REA 全量反编译（task-127，SESSION-138）

日期：2026-11-01（北京时间）。对象：`data/payload.bin`（=demo-v1_4.EXE 追加区 287,732B）的
**全部内容面**：明文桩 + 三解密流。手段：REA(Ghidra 12.1.4) x86 lane + ELF 封装喂符号。

## 1. 管线（可复现，全部入 git）

- `tools/engine_entries_scan.py` — 三源入口候选：docs/11 代码区（0x4..0x22A4、0x40560..0x5AC4C）
  序言特征 + `call rel32`(E8) 目标收集 + 172 记录区(0x2CB80..0x2D640)深码指针 24/24 命中。
  引擎候选=519（旧 154 ∪ 新 365）。mod1/mod2/stub 同法（桩以 docs/06 位引擎地址
  0x5577A/0x55960 等做种子）。
- `tools/wrap_engine_elf.py` — ELF32-i386 封装 + `.symtab` 入口表（**Ghidra 对裸 x86 自动分析
  只建 154 函数=此前账面 exhaustion 的真因；batch_decompile 只认已存在函数**——MIPS BIOS
  实证过的喂符号管线同构移植到 x86 lane）。base 参可配：引擎=0（地址=流偏移旧约定），
  桩=0x55338（与 docs/06 绝对址一致）。
- REA 会话：`open_binary(elf)` → `batch_decompile(≤40/批)` → `export_evidence_bundle`。
  468 函数 0 失败；bundle=权威存档（含全部 inline+spillover 批次，免转录）。

## 2. 覆盖结果（存档 `decrypted/decompiled/`，索引 `index.md`，统计 `w127_stats.json`）

| 目标 | 文件数 | 真码 | halt_baddata 占位 |
|---|---|---|---|
| 引擎流 eng_all（旧154+新365） | 519 | 316 | 203 |
| 明文桩 stub/（base 0x55338） | 20 | 20 | 0 |
| mod1（引导辅助） | 55 | 7 | 48（=数据表体，docs/06 定性再证） |
| mod2（控制器） | 28 | 28 | 0 |

B 区（宿主支撑库）候选 331→真码约半数：余数多为**数据区误入/半函数入口**，
已按 halt_baddata 判据分类留账（文件在盘，索引不列）。

## 3. 检测链定位（errcode 研究推进，二墙静态侧）

明文桩入口链完整反编译（`stub/stub_0x5533c/0x55384/0x55895`）：

- `0x5533c`（桩入口）：帧编排 `delta=retaddr-0x4010c4`（与 docs/07 装载协议逐项吻合）；
- **`0x55384`（stage-2 驱动）：先调 `0x55586`=`return *(fs:[0x20])`（TEB→PEB 指针直读）并以其
  标志位分支**——docs/106 收窄的「PEB 直读」候选在明文桩中命中实码；
- **`0x55895`（检测函数本体）：经宿主服务表 `0x4022a4` 以 `0x4f` 号查询把函数指针写入
  scratch 页 `0x2a0000`（AGENTS 记载的必 mmap 页），随后 `(*0x2a0000)(0x2a002a|0x2a0021)`
  间接调用该指针，返回值+0xc7a 截 short 返回**——检测走"宿主代取系统信息再回调"路径，
  值面由服务表槽 0x4f 的实现决定（Wine 差异点候选）；
- `0x55459/0x55716`：服务表 VirtualAlloc/MEM_RELEASE 编排；`0x553ca/0x553da/0x553e2`：
  块表 RAND(`0x52414e44`) 折叠校验 + bit0 调用位派发（docs/06 §块表逐行实码化）；
- 引擎侧 `eng_cand_552d4` 携 `"bleem!"` 串；`eng_cand_53374/533a3` 含同族 RAND 折叠环 +
  `'CD'` 签名比较（CD 介质特征面，后续楔可追）。

**负结果再证**：全 519 引擎伪码零静态 `return 6;`——与 SESSION-137 rwx 区 xref 负结果一致；
errcode 6 是运行期合成值，静态反汇编面不存在简单返回点。

## 4. 战略③（port 宿主）直接可用资产

- 宿主支撑库 0x40560.. 真码 164+ 函数伪码 = port 侧宿主服务仿真面的行为规格
  （VRAM 回绕拷贝/字符工具/JIT 落点 `0x5185E→0x56254` 序言族等 docs/11 锚点全部实码化）；
- 服务表 0x4022a4 消费面（eng_0x4 内 11 处直引 + 桩 4 处）= docs/81 ABI 的调用方证据集。

## 5. 坑与教训

- Ghidra 裸 raw-binary x86 的自动分析面=入口可达集（154），**"反编译完了"是假象**；
  符号喂入后同流建出 595 过程。对任何 headerless 载荷，先封 ELF+symtab 再谈覆盖。
- `batch_decompile` 单批 40 稳定；大结果自动 spillover 落盘，
  `export_evidence_bundle` 是免二次转录的权威存档通道（records 会话级累积）。
- 序言启发式在数据竞技场区假阳率高（~60%）——判据 `halt_baddata 且体<80B` 分类即可，
  不必返工入口扫描。

## 6. 复现

```bash
python3 tools/engine_entries_scan.py
python3 tools/wrap_engine_elf.py decrypted/stream_10a4c_L71cbf.bin \
        decrypted/decompiled/engine_entries.json /tmp/engine.elf   # sha256 e87330f7…
# REA: open_binary(/tmp/engine.elf) → batch_decompile(eng_all 新址) → export_evidence_bundle
bash tools/validate_host119.sh
```
