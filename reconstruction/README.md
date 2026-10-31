# bleem! demo v1.4 —— REA 逆向重建工程

目标: `demo-v1_4.EXE` (bleem! PlayStation 模拟器 1999 试用版引导器, PE32 GUI i386)
SHA-256: `b61ee13e2377e8305ede4e8be9752f83163368fadb19f0b3e40774d64e7e09da`
工具链: REA 6.5.0 + Ghidra 12.1.4 (headless), 全部结论挂在 Evidence 上。

## 这是什么

`demo-v1_4.EXE` 不是模拟器本体。它是一个 **自映射引导器**:

1. 只允许 Win9x (`GetVersionExA`, `dwPlatformId==1`)
2. `CreateFileA + CreateFileMappingA(PAGE_READONLY) + MapViewOfFile` 把**自己的文件镜像**映射进内存
3. `CALL view+0x55338` —— 跳转进尾部追加区的明文运行时桩
4. 桩 (2,748B 明文) 是一个完整的**私有运行时加载器**: 自定义位流 LZ 解压器 +
   5 模块重定位器 + `RAND` XOR 完整性校验 + API 编号间接派发
5. 被解压/解密的追加区 (287,732B, 熵 7.32) 才是 demo 引擎本体 —— 含配置对话框、
   防调试器、限时退出、CD 检查等。**2026-10-25 更新：已完整解密**——原生 Linux 上
   用其自身 2.7KB 明文解压器解出全部 3 个流（473,417B 明文，含 466KB PSX 引擎），
   位引擎语义逐指令还原（RCR 进位链 + 幻影位 + 双位序）。见 `docs/06_decryption.md`
   与 `decrypted/`；未执行任何加密区代码。

## 目录

| 路径 | 内容 | 完成度 |
|---|---|---|
| `src/loader.c` | 引导桩 (原 0x401000/0x401021) 逐指令重建 | ★ 完整, 经 REA 反编译对拍同构 |
| `src/stub_entry.c` | 追加桩入口 (原 0x55338) | 高置信骨架; 块表基址等 [未知] 已隔离 |
| `src/lzss.c` | 位流解压器 (原 0x5577a + 0x55960..0x55998) | 位读取器完整; 令牌树逐支转写 [中置信] |
| `src/modules.c` | 5 模块运行时链接器 (原 0x5546d/0x55597/0x5556d) | 流程完整转写; 字段常量部分 [未知] |
| `include/bleem.h` | 全部常量/错误表/ABI 约定 | 逐字对照原版 |
| `rc/bleem.rc` | 版本信息资源 (与原版逐字一致) | 完整 |
| `data/` | 原始 .rsrc 叶子 68 项 + payload.bin + stub.bin | 逐字节提取 |
| `tools/` | 提取/拼接/资源 dump 脚本 | 可重复运行 |
| `docs/` | 102 篇阶段报告(索引见下)+ Evidence 台账 | 全部与 validator 锚定 |
| `build/demo_rewrite.exe` | 交叉编译产物 (PE32 GUI, 与原版同类) | `make` 可复现 |

## 构建

```sh
make            # 需要 i686 mingw-w64 (默认 ~/.local/mingw, 或 make CC=i686-w64-mingw32-gcc)
make payload    # 实验: 重建 loader + 原版追加区拼接 (布局对照, 不可发布)
```

## 验证方式(已执行)

- `build/demo_rewrite.exe` 用**同一套 REA 反编译**, 伪代码与原 `entry` 的
  API 序列、错误计数逻辑、`0x55338` 调用、`0x2`/`0x11010` 常量全部一致。
- 未运行任何原版/重建代码 —— 桩语义全部来自静态证据, 推断处均标注 [推断]/[未知]。

## 法律提示

原版是 bleem, LLC 的商业软件(公司 2001 年已结业)。本工程仅为互操作/研究性
重建; `data/payload.bin` 是原样转储, 请勿再分发。

## 项目终局状态(2026-10-31 全工程 FINAL-SEAL-ALL 停线, 115 个 harness 会话, 107+1 任务全终态 95✓/13✗; 六战役全封线+薄引擎续行楔见下)

### 三条主线结果
- **原生移植达成**: 同级 `../port/` = Linux 原生 `bleem_port`(X11+Wayland 双后端, Vulkan 呈现, demo 限制全解除)。
  显示面**双代实证+Vulkan 三面**: Wayland selftest PASS(frames=3) + X11 selftest PASS(经 X0 Xwayland cookie, validate_host44 四判),
  Vulkan 呈现链硬件实证(RTX5080 swapchain 双代直证, validate_host46),
  `bash tools/final_check.sh` 带 DISPLAY 全绿。
- **引擎完整解密**: `decrypted/` 三流 473,417B 位级金标准(0xfacc/0x1015c/0x10a4c), 位引擎逐指令语义还原(docs/06)。
- **PSX 宿主链研究(H 系列)已关门**: BIOS SCPH1001 全像反编译、MIPS 块 JIT(TICK 30M 块零失败)、
  金标准 RAM/CPU 采样-重放、homebrew 端到端、CDROM/C2 HLE、根call 16-cmd 语义表+可运转 handler 全部落地;
  冷跳链达 `Execute!` 被三重实验排除收敛至**完整 kernel+C2 双向契约**(重写级), 判定关门:
  七道墙 `docs/33` → 设备/状态词面 `docs/35/36` → 帧槽/中断面 `docs/37/38` → C2 契约面 `docs/39-42` → H-24 契约完整化 `docs/43-45`(关门)。
  **游戏内容运行既定走 DuckStation**(`~/Applications/duckstation-x64.AppImage`)。
- **引擎兼容层 H-25 工程交付完成(SESSION-69..81, docs/46-58)**: BIOS 侧 50/50 rootcall handler 映射与活性实证 →
  可转发核 26/50 双态零分歧 → fwd API(`host/fwd.c`: fwd_init/fwd_entry/fwd_call2)核 26/26 全通 →
  ctx24 阻因逐级钉死(WAITER 定性→TCB 回收语境→填表三层阻力→设备真值零增益→**墙收敛为切换出口钩**) →
  `H_RAFIX` 出口钩使 0x19 调度器/0x3d 打印族**孤跑首次转化 rc=0**。批次态剩余=真线程运行期构建(重写级, 不在本仓库范围);
  全部能力带回归门沉淀(host49-62)。
- **H-26/H-27 终判封线(SESSION-83..90, docs/59-66)**: 静态注入六类口穷尽(host64-66) →
  真设备实装六件套全数落地且**全部出尽**(I_MASK 总根因/I_STAT 写清/事件流/0x1F801040-4F 握手/状态机/Timer 时基;
  0x3d 死墙→16M 块活循环→生命周期全景真执行至切换墙, host67-69) →
  **身份三勘**(非 BIOS 线程: magic 全源 0 命中; 非私有 VM: 五"栈"=C2 运行时自生成 MIPS 分派码 37/40, host70-71)。
  四类"最后缺口"假说(A/A2/静态/设备)全部实验拆除——**机制理解完备, 真完成仅剩"完整 C2 执行环境"
  (docs/45 量级, 月-季度级), 属工程量而非认知问题**。全线以"机制完备、边界定量"终封; 后续路线: 游戏=DuckStation,
  本仓库=完整逆向知识资产+可复用能力钩(port/HLE/JIT/fwd 全套)。
- **A3 主运行战役终判(SESSION-92..100, docs/67-75)**: 九楔把"到 Execute! 还差什么"答到底——
  真起点(H_CPUGOLD/gstate.hdr) → 混合执行模型双证(服务表窗 0x100 + 宿主 trampoline 段 0x044xxxxx) →
  首个 rc=0 完成段(H_HOSTRET) → 多段回放(每拍 3-4 块"交还宿主"= 每帧正常出口) →
  段间事件注入三候选全零差异(H_SEGP)。**终判: BLEEM 主运行 = 宿主驱动 + guest 每拍片段;
  帧分发与唤醒协议只存在于宿主 x86 码——纯 guest 语境长程主运行语义上不存在,
  guest 侧可达边界已实测穷尽**。再进一步 = x86 宿主码战线(反编译原 EXE, 新战役级)。
- **x86 宿主码战役终判(SESSION-102..112, docs/76-85)**: 十楔把 `demo-v1_4.EXE` 宿主侧打到静态/动态/native 三路线边界——启动链全破(一级 stub 交权 0x55338 → 二段块表驱动@0xFA0C → 三流解压位级终验 PASS → s3 init 0x10A4E 内含 PSX 内存映射构造器 → 交权 base+0xFA00 三段式); **s3(466KB)=完整宿主引擎**实证(SYSTEM.CNF/PSX.EXE/DDRAW/DSOUND/DINPUT); 服务分派表 0x4022a4 ABI 与 A3 guest 低页槽同构闭环; Win16 API 七处+imports 119 全名修正; docs/06 位引擎 22 锚零漂移复证。**终判: 宿主静态面穷尽可理解、解包链可位级验证; 主循环体最后 20% 依赖 DX/OLE 运行时(静态判据穷尽/Wine-10 三坑/native 不覆盖), 其语义已由 A3 混合执行模型+服务表投影给出等价物——继续=月级 DX/OLE 全栈重放战役, 收益比极低, 封线。**

- **全工程终局(FINAL-SEAL-ALL, SESSION-113..118)**: 四战役封线后经用户选定收尾三连——AH 抢救(docs/86: `.analysis/` 金 harness 37 文件入 git+manifest+build_check 门)、总收官报告(docs/87: 中英双语人类读者版)、PE 薄引擎楔1-5(docs/88-92: port 内 PSX 引擎栈——骨架/fixed 原址+BIOS/MIPS 解释核/设备联动/中断链+GTE 最小面, 60 断言双态, 真 BIOS 2000 步无崩溃)全部交付; host99 全量终审 PASS: 工程态=可理解、可验证、文档齐备、全部结论带现场复跑门; 游戏内容运行归 DuckStation; 任何后续推进(薄引擎楔2-5/新战线)需用户显式指令。

### docs 索引(102 篇=101 编号+台账, 按阶段)
| 阶段 | 文档 |
|---|---|
| 静态逆向与解密 01-12 | 概览/引导桩/追加桩/UI/载荷/解密(金标准)/引擎图/API 派发/宿主/JIT 模板/模板图/真 BIOS 深区 |
| BIOS·JIT·设备 13-22 | MIPS 墙/BIOS A-calls/设备层/REA 反编译/设备调用/金标准接线/root2 装载/deep thunk ABI/装载路线/HLE boot |
| 金标准管线 23-28 | golden RAM 采样/CPU 态/serve-inject/显示目标/shell 唤醒/homebrew 端到端 |
| CDROM·基线 29-31 | CD HLE/基线重锚/冷链 rootcall |
| H-20/21/22 墙链 32-38 | C2HOME 供给/终局路线/设备真值/C2SEC 态/C2main 修正/ra4 定案/H-22 关门判 |
| H-23 契约面 39-42 | 包装器地图/命令 census/cmd 语义表/cmdsem handler 终判 |
| H-24 完整化 `docs/43_frame_abi`→`docs/44_tfr_bounce`→`docs/45_h24_final` | 帧 ABI 三族+8 线程栈窗/H_TFR 回灌+四路注入终证/契约全集 44cmd 定量关门判 |
| M-4 映射 `docs/46_cmd_handlers` | 三族派发链(表基 0x200/0x874/0x674)+50/50 handler 完备性实证 |
| M-5 归名 `docs/47_rootcmd_names` | A 表锚 14/50+形态分类+三种安装视图终判 |
| M-6 活性 `docs/48_probe_semantics` | 金标态直跑 50 handler: 27 干净返回/23 深服务, BIOS 侧活性运行时实证 |
| M-6b 分解 `docs/49_forward_subset` | 双态零分歧可转发核 26/50 + HALT 三分类 + H-25 三批分解 |
| M-7 转发器 `docs/50_fwd_api` | fwd_init/fwd_entry/fwd_call(+call2) API: 核 26/26 全通; 表快照抗副作用+0xB0 域 0x5B 两发现 |
| M-8 批2 取证 `docs/51_batch2_ctx` | ctx=24 四桶分型+四环境恒定+批次态依赖发现(调用需定序) |
| M-9 批2 否证 `docs/52_batch2_falsified` | 早期窗同态+表补转化否证: ctx24 全体归批3; EXN2 宿主修正 |
| M-10 批3楔子 `docs/53_waiter_census` | ctx24=事件等待服务(WAITER)定性; FWDSERVE/H_IRQTICK 能力; 0x19/0x15 深探针 |
| M-11 调度器终证 `docs/54_0x19_tcb` | 0x19=线程调度器读回收语境(TCB 三件套验收探针); 45/4a 定序定性作废 |
| M-12 语境画像 `docs/55_tcb_layout` | suspend 函数字段清单(sp 高16=0x8800)+ready 队列 0x6CE0 布局; 三件套须运行期构建 |
| M-13 活线程阻力 `docs/56_livethread_resistance` | 填表三层阻力(sp 可填/ra 非内存/tcb 运行期回写); 实现规格定稿(顺序 2→1→3) |
| M-14 设备真值 `docs/57_devsem_truth` | I_STAT 写清/节拍/GPU ready 三件实现; 零增益终判: 墙收敛为切换出口钩(顺序修正 3→1) |
| M-15 出口钩 `docs/58_rafix_hook` | H_RAFIX 尸检 ra 改道: 0x19/0x3d 孤跑首次转化; 批次态终判=真线程运行期构建(重写级); H-25 工程交付完成 |
| H-26w1 零出口 `docs/59_create_devsw` | b0/00=printf 横幅链实证(ROM thunk 池 0xD880); H_ZEROFIX 值域式出口: 孤跑 6/24 转化; TTY/devsw 注入否证 |
| H-26w2 定版 `docs/60_zerofix_depth` | ZEROFIX 深度门定版(防语义腐蚀); ctx24 分型终判: 剩余 18=未装服务表槽(楔3 靶点=stub 注册) |
| H-26w3 封线 `docs/61_slot_falsified` | 表槽注册否证(296 槽零增益); 0x10C printf 劫持点; 六类静态注入口穷尽 → H-26 终判封线, 六面注入图谱=重写施工图 |
| H-27w1 真装 `docs/62_devreal_chain` | 设备真装四段链(MASK 总根因/写清/事件流/0x1F801040-4F 握手): 0x3d 死墙→16M 块活循环真执行; 楔2=CDROM 数据流 |
| H-27w2 会师 `docs/63_timer_lifecycle` | Timer 时基真装(0x1120 热点 499k): 0x3d 生命周期全景闭环, 设备面完备, 与 H-26 同一边界会师(只剩线程+事件源) |
| H-27w3 否证 `docs/64_wedge3_falsified` | 状态机/最小 create 集双否证: 设备面出尽, 缺口唯一化=金标准反推创建参数(周级); 三出口交裁决 |
| H-27w3A 重定义 `docs/65_c2_green_thread` | magic 全源 0 命中否证 + **C2 绿色线程发现**(双份工作区/哨兵/字节码帧): "线程"非 BIOS 线程, 缺口重定义=C2 VM 实装; docs/54-56 措辞勘正 |
| A2 否证 `docs/66_mips_jit_falsified_vm` | 五"栈"=C2 运行时自生成 MIPS 分派码(4 份/三流 0 命中; 0x93A20020=sb): 私有 VM 假说拆除; 机制完备, 剩余=工程量; 出口终版 B/C/A3 |
| A3w1 立项 `docs/67_mainrun_hostbridge` | FWDMAIN 主运行 harness: C2 0x400004 起步停滞 0x10C 宿主服务槽(jr t9, htrace cb 表同族)——楔2=MainRun 宿主桥 |
| A3w2 突破 `docs/68_mainrun_bridge` | **H_SVCLOW 宿主桥: 主运行 120 块→1 亿块跑穿(八数量级, 零死跳)**; 新墙=异常向量页黑洞(endpc=0x80), 楔3=异常分型 |
| A3w3 勘正 `docs/69_zombie_ring` | cop0 全零否证异常说; 真相=三块僵尸三角环(0/80/100 各 33M, ra=0 t9=80079D00); 亿块系虚高→反指标入账; 楔4=0x10C 语义核+handler 鉴定 |
| A3w4 活现场 `docs/70_livescene_patchcode` | golden BIOS 镜像=运行时改写(RAM≠ROM 五点全 DIFF): 0x5A=BLEEM 自适应补丁码区, 0x79D00=中断服务表("intr"/".vc"); 楔5=首跳 trace+服务族语义 |
| A3w5 根因 `docs/71_ra_zero_trace` | H_SVCT 24 跳全链: 两跳合法服务后第 3 跳经 0xFEE0 **死栈帧弹 ra=0** 入僵尸环——根因=MAINRUN 借 fwd 默认寄存器非 golden CPU 态; 状态词假说排除(H_POKE); 楔6=H_CPUGOLD 真起点 |
| A3w6 真起点 `docs/72_cpugold_firstwall` | **H_CPUGOLD(gstate.hdr)**: 真起点零死跳收敛 0x8003D6FC=状态词 0x80083C58 递减墙(POKE=0 过墙→堆指针二墙); 楔1-5 低页链定性为假起点产物 |
| A3w7 完成段 `docs/73_hostret_segment` | 混合模型双证(服务表窗+宿主 trampoline 段 0x044xxxxx); H_RAMDUMP 证 0x83C58 无自然递减者; **H_HOSTRET 首个 rc=0 完成段**(4 块, 边界声明入账); 楔8=多段回放 |
| A3w8 节拍 `docs/74_multiseg_beat` | **FWDSEGS 多段回放+宿主最小核: 3 段全通(4/3/3 块稳定节拍)="每拍交还宿主"=golden 空闲态本性; 纯 guest 长程主运行语义上不存在; 裁决点再临(w9 事件注入/终判/封线) |
| A3w9 边界穷尽 `docs/75_seginject_falsified` | H_SEGP 段间注入三候选全零差异: 入口=单状态词短路径非分发器, **唤醒协议只存在于宿主 x86 码**——guest 侧可达边界实测穷尽; 剩余=②终判封线/③x86 战线 |
| X86w1 侦察 `docs/76_x86_recon` | PE 基线: .text 仅 1184B stub+**overlay 287,732B 主体容器**; 6 DLL/71 命名 API 无静态 DX(动态解析时代特征); 楔2=stub 反汇编+overlay 扫描 |
| X86w2 两级 stub `docs/77_two_stage_stub` | 一级 stub=自文件映射+**call base+0x55338 交权**+错误壳; 二段引导器 2,780B 验真(位置无关+基址修正); overlay=[压缩载荷][二段 loader]; objdump raw 路线通 |
| X86w3 二段图谱 `docs/78_second_stage_map` | 1,026 指令/14 函数; **交权行 add $0xfa00+call *esi=解压落点跳板**; LZ 核 rep movsb×2@0x5586C/88C; 运行时 thunk(0x4022a4); MZ×2 验伪 |
| X86w4 解包链会师 `docs/79_unpack_chain_confluence` | **docs/06 早已逆向 [0x55338,0x55DF4) 位引擎到指令级(22 锚)——x86 战役大半已完成**; 块表 4 条目+4D24AF3D 校验实读复核; DDRAW/DSOUND/DINPUT 动态面直证; 二段仅块表驱动(LZ 核在 s2/s3)勘正 |
| X86w5 s3 真身 `docs/80_host_engine_s3` | 23 锚指令级审计**零漂移**(docs/06 金标准无腐); s1/s2=薄皮(表 0xe/0x9e); **s3(466KB/15.5万指令/SYSTEM.CNF/PSX.EXE/KERNEL.EXE)=完整宿主引擎**——A3"唤醒协议在宿主码"的宿主码即 s3; 主循环=楔6(thunk 表重建前置) |
| X86w6 服务表 ABI `docs/81_service_table_abi` | **0x4022a4=服务分派表**(0x55459 索引 call ABI); s3 对外面=table[0]×11 头壳聚簇+37 动态; **位引擎状态块 0x2A0000-2A002A 双侧互证**; A3 低页槽=表 guest 投影(闭环); 主循环未闭环(楔7 双判据) |
| X86w7 映射构造器+否证 `docs/82_mapbuilder_negation` | 双判据唯一交叉环=init 循环→**主循环静态 imm 原理性失明(诚实否证)**; **PSX 内存映射构造器现形**(0x1F800400/BIOS 窗/RAM 3.2MB/0x9400 池——docs/39 契约 native 代码在手); 楔8 换判据=大循环×状态块 disp 读簇 |
| X86w8 静态封顶 `docs/83_static_ceiling` | 三判据连续否证(disp 无判别力 181/shell 环 0/可达 0); **s3 入口=0x10A4E init(块表驱动回调, 无静态被引)**; 三段式启动定型; 主循环 Win32 全走 call*reg×37 动态面——**静态方法边界落账, 主循环归动态战役(Wine 断点)** |
| X86w9 Wine 边界 `docs/84_wine_boundary` | **EXE 直用 Win16 API 七处**(ord#1 FreeLibrary16/#35/#36/#37+MapLS/UnMapLS/QT_Thunk, imports 修正 119 全名); win16 屏蔽补丁副本 wine 40s 存活(3b3e110e); wine-10 krnl386 崩+ptrace_scope+COM 稳态三坑落账; 楔10=native -m32 重放 |
| X86w10 native 终验 `docs/85_native_reverify` | **三流位级 selftest 现场复跑全 PASS**(解包链终验闭环)+解包目的位图定版; **boot32 回归坑落账**(.analysis 无版本控制之祸→金 harness 必入 git); 三路线边界总账——**具备收官条件** |
| AHw1 抢救 `docs/86_analysis_harness_rescue` | `.analysis/` 金 harness 37 文件入 git(`tools/analysis_harness/`+manifest 基线+build_check 门); scan2 golden 三流尺寸复现=第二证据链; boot2 诚实标"已知回归化石"; 版权红线排除清单齐 |
| 总收官 `docs/87_grandfinale_report` | **人类读者版全景报告(中英双语)**: 五战线+两交付总表/技术时间线/定案 8 条+边界清单/可复现命令清单(抽检真跑)/工件索引/13✗ 失败账释义/方法论铁律集 |
| PEw1 薄引擎骨架 `docs/88_port_thin_engine_w1` | docs/81/82 施工图首次落成可跑代码(`port/src/psx_core`+`--psx-selftest` 17 断言): 状态块四槽平移常数/MAP_32BIT 32 位不变式/RAM 镜像+BIOS+MMIO 窗翻译/服务分派表 ABI; CHK 双 Eval 事故固化 |
| PEw2 原址直置 `docs/89_port_thin_engine_w2` | fixed 三窗 MAP_FIXED_NOREPLACE 原址(0x80000000/0xBFC00000/0x1F800000 恒等)+BLEEM_BIOS 装载(硬失败三态不降级)+手算 SHA-256 与登记值逐字符互证; selftest 双态 31/31; 0 别名窗 mmap_min_addr 边界+c 优先级两坑固化 |
| PEw3 MIPS 解释核 `docs/90_port_thin_engine_w3` | `port/src/psx_cpu` R3000A 整数核: 两段式延迟槽/地址错/syscall/eret/BEV 双向量 + 真 BIOS 0xBFC00000 首指令执行实证; 合成套件揪出槽后双跳环+lb 符号扩两核 bug; --cpu-selftest 14 断言双态 PASS |
| PEw4 设备联动 `docs/91_port_thin_engine_w4` | `port/src/psx_dev`: H 战役真值语义表直接平移(I_STAT 写 AND 清/EVTGEN 64 读/GPUOK 0xB0/Timer 读推进 0x400 16 位回绕/1044-104A 双相位握手)+CPU MMIO 钩+GPU GP0 FIFO4+docs/41 rootcall 0x600/0x674 契约链 CPU×RAM×设备三面联动; --dev-selftest 8 断言 |
| BMw8a GPU 前置侦察 `docs/100_bios_marathon_w8a` | **三反直觉实证**: GPU 命令端口零读写(a000b068=RAM 参数块指针非 MMIO)/CDROM 零读/vblank 静默运转; 呈现依赖链定位 |
| BMw8b1 精判楔 `docs/101_bios_marathon_w8b1` | **等待点再定位**: 30M 步正字窗(GPU+CDROM 0x1800-182F)零事件——楔8a"命令应答"表述修正为 **CDINT0 上电复位 ACK 中断缺失**(A(20) InitCD 已调已等; 主循环=转换+轮询+事件队列 drain 自洽; RAM dispatch/handler 表与 golden 逐字全等); CDROM 正字=0x1F801820-1827(0x1C00=expansion ROM 混称勘正); 外部核查: SCPH1001 无盘自动进 CD player 画面坐实; 楔8b-①实现清单定稿 | **三反直觉实证**(默认转正态 2M 步): GPU 命令端口零读写(a000b068=RAM 参数块指针非 MMIO)/CDROM 命令寄存器零读(vblank 实已在 irq_check 静默运转, 195 步=A() 节拍); **卡点终判: 呈现面第一道门 = CDROM 驱动器 ready 应答缺失致 0x703C 等待死锁**; 楔8b 三段清单(①CDROM 最小应答 1 楔 ②GPU 命令/VRAM 2-3 楔 ③呈现接线 1-2 楔) |
| BMw7b 转正楔 `docs/99_bios_marathon_w7b` | **CMEXP 转正**: cache 维护写弃默认 ON(psx_cm_enabled; BIOSRUN_NOCM=1 显式关=历史取证面 80910 原位) + 三腿论证(16B 步距 D-cache 签名/golden stub 反证/行为收敛) + 泛化不可行入账(表拷贝含零字, 完整 cache 模型=GPU 同级工程收益 0) + eng E4c 双态断言 + host104-108 门锚迁 NOCM 面; **BIOS 战役七楔收束** |
| BMw6a 全判读楔 `docs/98_bios_marathon_w6a` | **BIOS 健康主循环全判读+三勘正**: "0x2170 环"=正当堆 memset(非墙); "未触 CDROM"=混称勘正(主序 XA 0x1D80 系 x12 早已在账, 0x1C00 命令寄存器无盘态零访问=合法); "syscall 风暴"=A() 轮询正常载体(195 步节拍); **战役结论: CPU 侧可观测行为与真机语义对位, 再深=GPU 呈现(7-a)或收线** |
| BMw5 突破楔 `docs/97_bios_marathon_w5` | **REGIMM 真 bug 修**(rs/rt 字段互换, 自测盲区五楔未测=教训"分支族必按正字测"; cpu 16 断言)+ **CMEXP cache 维护写弃实验**(16B 步距=DS cache index 特征; blackhole 实现坑: mem_rw 只交指针, 弃写必须重定向)+ **BIOS 无盘 idle 主循环会聚**(20M 步 rom=2.73M/ramlo=16.8M/dev 活跃/exc=88736 线性 195 步周期, 无漂移) |
| BMw4 勘正 `docs/96_bios_marathon_w4` | **A(24) 漂移完整剧本(step 级)**: 42a0/42d0 InstallExceptionHandlers 装载正确(TR 逐词=golden)→BIOS 自身稀疏清零循环(0x1958)抹 stub→A(24) rootcall jr 0xB0 落空页; **等待条件清单 W1 stub 存活/W2 CDROM SetLocation/W3 vblank**; 排除假说(b)异常机制(ROM[0xB0]=0 实证); 装备=JUMPZERO 真目标/WATCH 生命周期/TR 单步窗/psx_rom_dis 判读器 |
| BMw3 诊断楔 `docs/95_bios_marathon_w3` | **两假说推翻+一负结果**: BIOS 冷 boot 不触 CDROM 0x1C00(DEV_TR 直方图: SPU/XA/RAM_SIZE 足迹); 0x100-0x3FF alias 当日回滚; 金标 0x30000+ 差=运行中快照含程序体(冷/中场分型教训); 交付 RAM_SIZE 回读+金标表/冷种子/2MB dump 三工具+J 判读澄清(核一直正确); 落点定盘=无盘冷终态 |
| BMw2 勘正 `docs/94_bios_marathon_w2` | **勘正 BMw1"健康 idle"**: 分段直方图+落点判读证 75% 时间在无盘空程序 nop 漂流(BIOS 走通 ROM 初始化→判无盘→跳 0x0); 方法论固化"零异常≠活着"三件套(分段/落点/跳零区侦测); 楔3 钉死=CDROM 0x1F801C00 系按 BIOS 真码 trace 补 |
| BMw1 BIOS 长跑 `docs/93_bios_marathon_w1` | 真 BIOS 金标准方法论: --bios-run 侦察工具+三修真值面(RAM 全镜像 2MB 折叠/rfe rootcall 返回/GTE 命令判定)——**SCPH1001 从 88 步推到 1000 万步零异常**(sp/ra 恒定=idle 环); 断言按真值改写非放松 |
| PEw5 收官 `docs/92_port_thin_engine_w5` | 中断自动进入链闭环(step 头 irq_check/IE·EXL 真位修正/IP8/eret 重执行)+GTE 最小面(mfc2 系+SFTRGB+AVZ3, 位布局定义面声明待真机对拍)+svc 表 16→37 全量+**真 BIOS 2000 步无崩溃烟测**(cycles=2000 pc=0x80001e5c); --eng-selftest 7 断言; 薄引擎五楔总账 60 断言双态 |
| 台账 | evidence_ledger.md |

### 验证矩阵(全部可现场复跑)
- port 全量: `cd ../port && bash tools/final_check.sh` → FINAL CHECK PASS(需 DISPLAY 或走 validate_host44 自取 cookie)
- 解压器对拍: `bash tools/native_unpack/build_selftest.sh` 三流逐字节 PASS; 文档: `python3 tools/validate_decomp.py`, `bash tools/validate_map.sh`
- H 系列锚定 validator 序列 host7..host52(近期): host46(Vulkan 双代硬件实证), host47(帧 ABI+线程栈窗),
  host48(H_TFR 回灌+零回归), host49(契约 44cmd), host50(50/50 handler 完备), host51(A 表归名), host52(金标态直跑活性), host53(可转发核 26/50 双态一致), host54(fwd API 核 26/26 验收), host55(ctx 分型+批次态依赖实证), host56(早期窗同态+表补否证复算), host57(waiter 分族+深推进复测), host58(0x19 SPB 单事件+语境复算), host59(队列布局+suspend 字段词级复算+FWDTcb 恒定门), host60(毒值跟随+ra 钉死+TCB 回写三层阻力复算),  host61(设备三件零增益门+epilogue 墙), host62(出口钩转化+假阳性门+批次态恒定), host63(封账审计: 状态一致+port final_check+关键回归抽验), host64(H-26 零出口转化集+假阳性门+迁移回归), host65(深度门防腐+分型锚), host66(表槽否证四组合+0x10C 形态锚), host67(设备真装四段链+形态剧变+假阳性门), host68(Timer 热点+生命周期锚), host69(楔3 状态机/create 双否证锚), host70(magic 否证+绿色线程证据锚), host71(MIPS JIT 码判定锚), host72(终封七判审计), host73(A3 主运行停滞锚), host74(宿主桥+亿块跑穿里程碑锚), host76(僵尸环三柱+反指标锚), host77(活现场双源+中断服务表锚), host78(ra 归零全链+死帧根因锚), host79(真起点+首道设备墙锚), host80(HOSTRET 完成段+假阳性门锚), host81(多段回放节拍锚), host82(段间注入全否锚), host83(A3 终判封线审计), host84(x86 侦察基线锚), host85(两级 stub 锚), host86(二段图谱锚), host87(解包链会师锚), host88(s3 真身+零漂移锚), host89(服务表 ABI 锚), host90(映射构造器+否证锚), host91(静态封顶锚), host92(Win16 API+wine 边界锚), host93(native 终验+收官条件锚), host94(x86 封线七判终审), host95(AH 抢救门), host97(总报告门; host96 编号未用), host98(薄引擎骨架门), host99(FINAL-SEAL-ALL 全量终审), host100(薄引擎楔2 门), host101(薄引擎楔3 MIPS 核门), host102(薄引擎楔4 设备联动门), host103(薄引擎楔5 收官门), host104(BIOS 长跑战役楔1 门), host105(BIOS 战役楔2 勘正门), host106(BIOS 战役楔3 诊断门), host107(BIOS 战役楔4 侦察门), host108(BIOS 战役楔5 突破门), host109(BIOS 战役楔6a 全判读门), host110(BIOS 战役楔7b 转正门), host111(BIOS 战役楔8a GPU 前置侦察门), host112(BIOS 战役楔8b1 精判门)——注: host75 编号未用
- harness 状态: `harness-tasks.json`(103 任务全终态 90✓/13✗, AH 战线进行中另记) + `harness-progress.txt`(SESSION-1..115); x86 封线审计=validate_host94(七判: 动态终态/docs/取证工件/抽验/port 终跑); AH 抢救门=host95; 历史审计=host83/72/63

### 发布镜像
`linbleePSX/`(GitHub gongyuesong-sudo/linbleePSX)只含**自研文档/脚本/源码**; 版权工件(*.EXE、payload.bin、
decrypted/、ROM/金标准 dump)由镜像 .gitignore 拒收, 每次同步有 forbidden-check 门禁。
