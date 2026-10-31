# 50 — M-7 H-25 批1：host 侧转发器 API（task-064，SESSION-73）

## 1. API（`host/fwd.h` + `host/fwd.c`，单 TU 内嵌 JIT 库）
```
fwd_init(bios_path, golden_ram_path)          # ROM+金标 RAM 整幅装载, 0=OK
fwd_entry(vec, cmd, *pc)                      # 表解析(快照), 2=槽缺失
fwd_call(vec, cmd, a0a3[4], *v0, budget, *blk)
fwd_call2(..., *blk, *haltpc)   # 取证变体(task-065): 回读停机 pc
    rc=0 服务干净返回(v0 有效)  rc=1 需设备语境/超时
    rc=2 表槽缺失               rc=3 未初始化
```
vec∈{0xA0,0xB0,0xC0} → 表基 {0x200,0x874,0x674}，槽值相对偏移语义（<0x80000 → `0xBFC00000+off`）。

## 2. 本-session 两个 API 级发现（H-25 实施必读）
1. **真 handler 会清活表槽**：按清单顺序连调时，先前服务的 BIOS 自毁逻辑把后续 10+ 个
   0xB0 槽清零（cmd≥0x44 段）⇒ 转发器必须按 `fwd_init` 时刻的**安装态快照**解析入口
   （`tab_snap[3][256]`）；活表只可作观测不可作解析源。
2. **0xB0 族 cmd 上限 0x5B**：快照按 64 槽截断会假报 miss（本坑实测 21/26→26/26）。

## 3. 验收（`tools/fwd_test.c`，validate_host54 复算）
- 可转发核清单（docs/49 BREAK 26 项）：`FWDSUM ok=26 ctx=0 miss=0` — **26/26 全通**。
- 全量 50 项：`FWDSUM ok=26 ctx=24 miss=0` — 与 docs/49 分批完全一致，零漂移。
- 顺序无关性：ok 清单在任意子集顺序下仍 26/26（快照解析所保证）。

## 4. H-25 现状
批1 的 host 面已可交付：**引擎侧任何对 26 核服务的调用，一次 `fwd_call` 即达真 BIOS 实现**。
批2/批3（ctx=24）按 docs/49 §4 分解推进；本 API 是它们的公共底座。
