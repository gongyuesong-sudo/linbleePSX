# 99 — BIOS 战役楔7b：CMEXP 转正 = cache 维护写弃默认 ON（task-118，SESSION-129）

## 1. 转正语义（从实验开关到默认行为）
- 引擎面：`psx_cm_enabled = 1` 默认开；`BIOSRUN_NOCM=1`（或程序化置 0）显式关。
- patch 本体不变：store 且 pc∈[0xBFC01940,0xBFC01A40]（SCPH1001 维护循环
  码窗）且目标 RAM off<0x1000 → 写落 blackhole。

## 2. 转正论证（三腿）
1. **特征签名**：该循环以 16B 步距扫 [0,0xF80) = PSX D-cache index 空间
   （256 线 × 16B）全遍历 = 教科书 cache 维护形态；同 ROM 两份互证、
   主循环周期性调用（cm_skip 随步数线性增长）与"运行期定期维护"一致。
2. **golden 反证**：真机运行中快照 RAM[0xB0]=lui/addiu/beq stub 存活且与
   本引擎 patch 态装载值逐字一致 = 该写在真机 RAM 层不可见。
3. **行为收敛**：patch ON 后 BIOS 走完 A(24)/A(7) rootcall、XA 组写、
   堆 memset、事件轮询（docs/98 全判读）——CPU 侧行为与真机语义对位；
   patch OFF 保留为历史取证面（80910 现场原位，host104-108 锚此面）。

## 3. 泛化不可行性（为何保 pc 精确窗，不做通用 cache 模型）
- 同一低页地址空间存在**两类合法写**：维护循环写 0（该弃）与
  InstallExceptionHandlers 表拷贝（含**值为 0 的模板字** 0xAC/0xBC/0xCC——
  该留）。按"值=0 + off<0x1000"泛化会毁后者（实测表拷贝含零字）。
- 真解 = 完整 D-cache 写回模型（写进 cache、fetch 读 RAM、维护循环仅
  触 tag）——那是 GPU 同量级工程（楔7-a 档），且薄引擎面收益=0
  （BIOS 行为已全对位）。**结论：BIOS 特定码窗 patch = 当前性价比最优**，
  patch 适用边界（SCPH1001）与 H 时代 host 补丁同型诚实登记。

## 4. 双态自测 + 门链适配
- eng E4c 新断言（装载态）：ON 90k 步 cm_skip≥256 且 RAM[0xB0]==3c080000
  （stub 存活）；OFF 面 cm_skip==0 且 RAM[0xB0]==0（历史毁表面复现）——
  一断言锁双态。无 ROM 面跳过守恒。eng 8→9，四面 62 断言双态全绿。
- 历史门适配（锚"该楔当时现场"原则）：host104-108 的 bios-run 断言统一加
  BIOSRUN_NOCM=1（楔1-5 证据面 = 无 patch 历史态，80910/74089/10M 零异常
  等锚点原位保持）；host109 带 CMEXP=1 无害（现 = 默认态）。
  host108 D 语义改"NOCM 显式面零回归"。
- 转正回归：host104/105/106/107/108/109 六门全绿（E 链含 final_check）。

## 5. 战役状态
BIOS 战役七楔收束（长跑/勘正/诊断/侦察/突破/全判读/转正）——
**CPU 侧可观测语义完整+实验债清偿**。开放路线只剩楔7-a GPU 呈现面
（大工程：GPU 命令队列+VRAM+显示输出，产出=BIOS 画面）或收线。
