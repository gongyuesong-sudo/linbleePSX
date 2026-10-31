# 86 — AH 战线楔1：.analysis 金 harness 抢救入 git（task-104，SESSION-115）

## 1. 动因
boot32 腐烂事故（docs/85 §2）：`.analysis/` 无版本控制，boot2.c 被后续实验改写后
**金时刻语义不可恢复**（重放 SEGV eip=0x400004）。用户 clarify 选定方向①=抢救。

## 2. 抢救结果（`rewrite/tools/analysis_harness/`，37 文件 + manifest + 门）
- **源码 15 件**：scan2.c/tramp.s(金·可复跑) boot2.c+四 .s(已知回归·化石)
  needseed.c/reloc.c/bleem_unpack.py(金标准同代物) boot.c(空文件按实录) 草稿 3 件。
- **取证元数据 22 件**：槽表/地址表/串表/运行日志（dialog_strings/engine_strings
  标**禁入发布镜像**版权红线；REA 快照 784K~35M 不搬，留 .analysis）。
- **manifest.sha256**：37 文件基线钉死；`build_check.sh --verify` 检测漂移。
- **排除项**：captured*/decoded.pkl/二进制/快照/日志——版权与体积双红线。

## 3. 可复跑门（host95 F 项现场跑）
```
bash build_check.sh          → scan2 编译+i386+三流尺寸 golden(2979/4327/466111)
                                + boot32 编译+i386(运行态不 assert, README 标注已知回归)
bash build_check.sh --verify → manifest 一致性
```
诚实设计：**不谎称 boot2 全家健康**——运行承诺只给 scan2/needseed/reloc/unpack；
boot2 以"源码化石+回归现场"入库，历史金职责由 native_unpack/ 位级门承接（在册）。

## 4. scan2 golden 复核亮点
默认全扫输出三行 STREAM，解压尺寸 2,979/4,327/466,111 与三流金标准**逐值一致**——
独立于 unpack_lib 的第二条可复跑证据链就此入册。
