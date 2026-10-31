# analysis_harness — .analysis/ 取证 harness 抢救入 git（SESSION-115, docs/86）

`gcc -m32` 原生 x86 取证 harness 的 **git 基线副本**（2026-10-31 从 `/home/gill/bleem/.analysis/`
抢救；原件无版本控制已发生 boot2 语义腐烂——本目录即教训的落地补救）。

## 文件状态标注（钉基线时刻的真实状态，诚实账）
| 文件 | 状态 | 说明 |
|---|---|---|
| scan2.c + tramp.s | **金·可复跑** | overlay 三流扫描；build_check 断言 2979/4327/466111 三尺寸 |
| needseed.c reloc.c bleem_unpack.py | 金标准同代物 | 位引擎/seed 推导/解包（docs/06 同语义） |
| boot2.c + runstub.s shims.s vshim.s trampolines.s | **已知回归** | 钉基线时刻重放 SEGV(eip=0x400004, slot=栈址 delta=0 非金语义)——**运行态不可信**，仅源码化石；金 harness 历史职责已由 `rewrite/tools/native_unpack/` 承接（位级对拍门在） |
| boot.c | 空文件(0B) | 原 .analysis 中即空，按实录入 |
| boot_raw.c.draft scan.c tramp_draft.s | 草稿/化石 | 无运行承诺 |
| *.json addresses.txt boot_out.txt | 取证元数据 | 地址表/槽表/串表/运行日志（`engine_strings.json`/`dialog_strings.json` **禁入发布镜像**——版权串表红线） |

## 复跑门
```bash
bash build_check.sh            # 编译+bitness+scan2 golden
bash build_check.sh --verify   # manifest.sha256 一致性(源码漂移检测)
```
漂移处理：源码有意修改→评审后重钉 `sha256sum <files> > manifest.sha256` 并同步 docs/86。

## 排除项（版权红线，一律不入 git）
captured/ captured_w10/（解密区转储）、decoded.pkl、boot/boot32/*.o 二进制、
*.pkl、REA 快照(bleem/engine*-snapshot.json 784K~35M)、analyze*.log。
