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
| `docs/` | 76 篇阶段报告(索引见下)+ Evidence 台账 | 全部与 validator 锚定 |
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

## 项目终局状态(2026-10-31 A3 终判封线, 98 个 harness 会话, 92 任务全终态 79✓/13✗)

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

### docs 索引(76 篇=75 编号+台账, 按阶段)
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
| 台账 | evidence_ledger.md |

### 验证矩阵(全部可现场复跑)
- port 全量: `cd ../port && bash tools/final_check.sh` → FINAL CHECK PASS(需 DISPLAY 或走 validate_host44 自取 cookie)
- 解压器对拍: `bash tools/native_unpack/build_selftest.sh` 三流逐字节 PASS; 文档: `python3 tools/validate_decomp.py`, `bash tools/validate_map.sh`
- H 系列锚定 validator 序列 host7..host52(近期): host46(Vulkan 双代硬件实证), host47(帧 ABI+线程栈窗),
  host48(H_TFR 回灌+零回归), host49(契约 44cmd), host50(50/50 handler 完备), host51(A 表归名), host52(金标态直跑活性), host53(可转发核 26/50 双态一致), host54(fwd API 核 26/26 验收), host55(ctx 分型+批次态依赖实证), host56(早期窗同态+表补否证复算), host57(waiter 分族+深推进复测), host58(0x19 SPB 单事件+语境复算), host59(队列布局+suspend 字段词级复算+FWDTcb 恒定门), host60(毒值跟随+ra 钉死+TCB 回写三层阻力复算),  host61(设备三件零增益门+epilogue 墙), host62(出口钩转化+假阳性门+批次态恒定), host63(封账审计: 状态一致+port final_check+关键回归抽验), host64(H-26 零出口转化集+假阳性门+迁移回归), host65(深度门防腐+分型锚), host66(表槽否证四组合+0x10C 形态锚), host67(设备真装四段链+形态剧变+假阳性门), host68(Timer 热点+生命周期锚), host69(楔3 状态机/create 双否证锚), host70(magic 否证+绿色线程证据锚), host71(MIPS JIT 码判定锚), host72(终封七判审计), host73(A3 主运行停滞锚), host74(宿主桥+亿块跑穿里程碑锚), host76(僵尸环三柱+反指标锚), host77(活现场双源+中断服务表锚), host78(ra 归零全链+死帧根因锚), host79(真起点+首道设备墙锚), host80(HOSTRET 完成段+假阳性门锚), host81(多段回放节拍锚), host82(段间注入全否锚), host83(A3 终判封线审计)——注: host75 编号未用
- harness 状态: `harness-tasks.json`(92 任务全终态 79✓/13✗) + `harness-progress.txt`(SESSION-1..101); A3 终判封线审计=validate_host83(动态终态+docs=77+能力钩+A3 锚抽验+port 终跑); 历史审计=host72/63

### 发布镜像
`linbleePSX/`(GitHub gongyuesong-sudo/linbleePSX)只含**自研文档/脚本/源码**; 版权工件(*.EXE、payload.bin、
decrypted/、ROM/金标准 dump)由镜像 .gitignore 拒收, 每次同步有 forbidden-check 门禁。
