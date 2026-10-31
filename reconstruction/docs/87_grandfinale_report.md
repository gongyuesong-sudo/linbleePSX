# 87 — bleem! demo v1.4 逆向与移植工程 · 总收官报告 / Grand Finale Report

> 面向人类读者的全景总结（2026-10-31，harness 会话 116，任务账 107 = 92✓/13✗/2 排队(本报告关账点)，
> git 提交 231，文档 87 篇，验证门 host7..host95）。
> 细节永远以 `docs/01-86` 与对应 validator 为准；本篇是地图，不是领土。

---

## 0. Executive Summary (EN)

We reverse-engineered `demo-v1_4.EXE` — the 1999 bleem! PSX-emulator demo
(SHA-256 `b61ee13e…7e09da`, PE32 i386) — to a level where **every layer of its
self-extractor and host engine is understood, verified and re-runnable**:

- **Crypto/unpack**: the three-stream container (473,417 B) is decrypted to a
  bit-exact golden; its Elias-style bit engine is recovered instruction-by-
  instruction (docs/06), re-verified twice (native selftest + scan2 golden),
  with a 22-anchor audit showing zero drift (docs/80).
- **Loader chain fully broken** (docs/76-85): 1,184 B stage-1 stub → stage-2
  bootstrap at file-offset 0x55338 → block table @0xFA0C → stream unpack →
  `s3` = a 466 KB complete PSX host engine (DDRAW/DSOUND/DINPUT, `:\PSX.EXE`)
  → hand-off `call [base+0xFA00]`. The EXE directly imports **seven Win16-era
  APIs** (FreeLibrary16, MapLS, QT_Thunk…), which is what breaks Wine-10.
- **Execution model settled** (A3, docs/67-75): BLEEM runs as *host x86 driving
  each frame + guest MIPS executing 3-4 block fragments per beat*; a pure long
  guest-side main run is semantically impossible. The guest low-page service
  slots are a projection of the host's `0x4022a4` dispatch table (docs/81).
- **Deliverables**: a native Linux port (`port/`, X11+Wayland, Vulkan, demo
  limits removed, HW-verified on RTX 5080) and a public research mirror
  (linbleePSX) carrying no copyrighted artifacts.
- **Boundaries honestly recorded**: the last ~20 % of the host main loop needs
  a real DX/OLE runtime; static criteria (docs/83), Wine (docs/84) and native
  replay (docs/85) each hit explicit, documented walls. Game execution remains
  DuckStation's job by design.

---

## 1. 工程全景（五条战线 + 两大交付）

| 战线 | 会话区间 | 文档 | 终态 | 一句话结论 |
|---|---|---|---|---|
| H-01..H-24 宿主链机制 | S16-68 | 01-45 | 封线 | 七道墙→C2 双向契约是冷跑到 Execute! 的全部缺口（重写级），逐墙关门 |
| H-25/26/27 兼容层工程 | S69-91 | 46-66 | 封线 | fwd API 26/26；Rafix 令 0x19/0x3d 首次 rc=0；设备面六件套全真装仍收敛到切换墙——剩余=真线程运行期（重写级） |
| A3 主运行战役 | S92-101 | 67-75 | 封线 | **混合执行模型定案**：宿主驱动+guest 每拍 3-4 块；纯 guest 长程不存在；唤醒协议只在宿主 x86 码 |
| x86 宿主码战役 | S102-112 | 76-85 | 封线 | 启动链三段式全破；s3=宿主引擎本体；服务表 ABI 与 A3 闭环；三路线边界落账 |
| AH harness 抢救 | S115- | 86 | 进行中→②③④排队 | 金 harness 37 文件入 git+manifest+门 |
| **交付·port** | S54-64 | 22/30 等 | 达成 | 原生 `bleem_port`：X11+Wayland 双后端、Vulkan 呈现（RTX5080 双代实证）、demo 限制全解除，`final_check.sh` 全绿 |
| **交付·解密金标准** | S18-30 | 06 | 达成 | `decrypted/stream_*.bin` 473,417B 位级金标准 + 解压器对拍门永久在 |

## 2. 技术时间线（阶段压缩版）

- **S16-49 地基**：PE/容器解剖 → 位引擎逐指令还原+三流解密金标准 → MIPS 块 JIT
  （30M 块零失败）→ BIOS SCPH1001 全像反编译（REA/Ghidra LE-ELF 包装法）→
  设备层 v1 → 金标准 RAM 无头采样（RetroArch）。显示承诺"X11+Wayland+Vulkan"立项。
- **S50-68 H-21..24**：rootcall 语义表 44cmd/50stub/3 族定量关门；X11/Vulkan
  selftest 双硬件实证；port `final_check` 终跑链成型。
- **S69-91 H-25..27**：BIOS 侧 50/50 handler 映射→可转发核 26/50→fwd API 交付→
  ctx24 WAITER 定性→TCB/创建面/设备真值三层阻力→Timer 时基真装+0x3d 生命周期闭环→
  magic 否证+C2 绿色线程+私有 VM 否证三连。三战役终封（host72 七判）。
- **S92-101 A3**：FWDMAIN 主运行→0x10C 服务槽墙→H_SVCLOW 桥亿块"跑穿"→**诚实
  勘正为僵尸三角环**→活现场(0x5A 运行时补丁码/0x79D00 服务表)→ra 归零全链→
  H_CPUGOLD 真起点(gstate.hdr)→首墙 0x80083C58→H_HOSTRET 首个 rc=0 段→
  H_SEGP 段间注入全否 → **混合执行模型终判**（host83 封线）。
- **S102-112 x86**：PE 侦察(stub+overlay)→两级 stub 破解(交权 0x55338)→二段图谱
  (交权行 add $0xfa00)→与 docs/06 会师(块表复核)→22 锚零漂移+s3 真身→服务表 ABI
  (索引 call+位引擎状态块)→映射构造器(0x1F800400 出生地)→静态三判据穷尽封顶→
  Wine 战役(Win16 七 API/krnl386 坑/副本 40s 存活)→native 终验 PASS → FINAL-SEAL-X86
  （host94 七判）。
- **S113-116 转向**：双巡检确认全账→用户选定 1+2+3+4→AH 抢救交付→总报告(本篇)。

## 3. 定案清单（正面发现，可当场复跑）

1. **容器格式全规格**（docs/06/79/85）：overlay 287,732B；块表 @0xFA0C 16B 条目×4；
   区域链 0xfab7→0x1014f→0x10a4e→0x55338；校验和 4D24AF3D 实证；三流解压尺寸
   2,979/4,327/466,111 双通道复现（unpack_lib 对拍 + scan2 golden）。
2. **位引擎**（docs/06/79/80/81）：树位 shr-then-je+rcr、get8 快慢路、0x2A002A
   位缓存、状态块 @0x2A0000 字段族；22 指令锚零漂移审计通过。
3. **启动链三段式**（docs/76-79/83/85）：一级 stub(自文件映射+交权 0x55338)→
   二段块表驱动(s3 dst bit0=call，init=0x10A4E 无静态被引)→base+0xFA00 置表桩。
4. **s3=宿主引擎本体**（docs/80/81）：466KB/15.5 万指令；SYSTEM.CNF/PSX.EXE/
   KERNEL.EXE/DDRAW/DSOUND/DINPUT 直证；对外 Win32=服务分派表(0x4022a4 索引 call)
   单 ABI，静态面 table[0]×11+动态 call*reg×37。
5. **混合执行模型**（docs/67-75）：宿主每拍驱动、guest 片段执行、唤醒协议纯宿主；
   guest 低页服务槽=宿主服务表投影（两侧同构"表基址+索引"ABI）。
6. **PSX 内存映射构造器**（docs/82）：0x1F800400/0x1F800000/0xBFC00000/3.2MB RAM/
   0x9400 池——H 时代全部 MMIO 常数的 native 出生地（docs/39 契约的实现码）。
7. **Win16 API 七处**（docs/84）：ord#1 FreeLibrary16/#35/#36/#37+MapLS/UnMapLS/
   QT_Thunk；屏蔽补丁取证副本（3b3e110e）wine-10 40s 存活可重放。
8. **port 达成**：X11/Wayland/Vulkan 硬件实证链（host44/46），final_check 每次封线必跑。

## 4. 边界与否定（同样可复跑，防止后人重烧）

- 纯 guest 长程主运行语义不存在（A3 终判）；100M"跑穿"=僵尸环（docs/69）。
- 设备面全真装+静态注入六类口穷尽仍到切换墙（docs/59-63）；create 序列不可复现
  （docs/64）；magic 0x27D54445 全源 0 命中（docs/65）；私有 VM 否证（docs/66）。
- 主循环静态判据穷尽（docs/83）；Wine-10 三坑（docs/84）；native 重放不覆盖主循环
  （docs/85）；0x5Axxx 段 RAM≠ROM（docs/70）；disc.state 无明文 CPU 段（docs/72）。
- 游戏内容运行归 DuckStation——本仓库是模拟器/宿主/引擎/金标准**知识工程**。

## 5. 可复现命令清单（全部现场可跑）

```bash
# 解密/解包金标准
cd rewrite && bash tools/native_unpack/build_selftest.sh          # 三流逐字节 PASS
# 抢救 harness
bash tools/analysis_harness/build_check.sh                        # 编译+i386+scan2 golden
bash tools/analysis_harness/build_check.sh --verify               # manifest 漂移检测
# 封线审计(近期代表)
bash tools/validate_host83.sh   # A3 终判七判
bash tools/validate_host94.sh   # x86 封线七判(含 port 终跑)
bash tools/validate_host95.sh   # AH 抢救门
bash tools/validate_host45.sh   # 全量文档/索引/计数锚
# 原生移植
cd ../port && bash tools/final_check.sh                           # FINAL CHECK PASS
./build/bleem_port --selftest --driver x11|wayland                # 双后端
# 文档校验
python3 tools/validate_decomp.py && bash tools/validate_map.sh
```

## 6. 工件索引

- **文档**：`docs/01-86`（87 篇含本篇）；索引=README「docs 索引」节；台账 `docs/evidence_ledger.md`。
- **金标准**：`decrypted/stream_*.bin`（473,417B）；`.analysis/golden/`（RAM/CPU 金样，不入 git）。
- **能力 harness**：`rewrite/host/`（JIT+fwd+全套 env 钩）、`tools/analysis_harness/`（抢救基线）、`.analysis/`（取证现场，⚠ 无版本控制，以 analysis_harness manifest 为准）。
- **移植**：`port/`（C11+CMake+SDL2+Vulkan，产物 `build/bleem_port`）。
- **发布镜像**：`linbleePSX`（gongyuesong-sudo/linbleePSX）——纯自研文档/源码/脚本；
  版权工件（EXE/payload/decrypted/串表 dumps）按 .gitignore 红线隔离。
- **任务账**：`harness-tasks.json`（107=91✓/13✗/3 排队）+ `harness-progress.txt`（759 行，SESSION-1..116）。

## 7. 失败任务账（13✗ 的意义）

全部集中在 H-09..H-23 早期尝试（VA 供给/BIOS A 函数激活/CDROM 真值时序/C2 上下文
自建/自研契约等）——每一条的"失败"都在后续战线被**定性为边界**（重写级缺口/环境墙），
无一被静默遗忘：这正是 harness 协议"failed 是终态也是答案"的账本体现。

## 8. 方法论沉淀（跨工程可迁移）

1. **harness 三件套**：progress.txt(append-only 现场)+tasks.json(结构化终态)+validator
   序列(每结论一个可复跑门)——上下文丢失后可从磁盘满血复活。
2. **铁律集**：validation.command 现场 rc=0 才许 completed；关账后独立进程重读磁盘
   （stdout 会撒谎）；金标准 harness 必须入 git（boot32 腐烂事故）；历史审计 validator
   禁写具体数字锚（host72 教训）；objdump 地址参带 0x；`pgrep -x` 勿 `-f`（自杀坑）；
   实验序列必须落盘（19-create 序列丢失事故）。
3. **诚实经济学**：假跑穿要主动勘正（僵尸环）、静态视图与运行态差异要双账（get8）、
   "没做到"与"做不到"分开写（docs/83 判据穷尽 vs docs/85 覆盖边界）。

---
*本报告由 SESSION-116 生成；封线审计链 host83/94/95；后续 task-106(port 薄引擎)/107(停线) 按队列推进。*
