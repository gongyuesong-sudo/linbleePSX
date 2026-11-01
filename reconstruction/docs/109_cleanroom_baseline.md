# 109 — 清洁房战役底数盘点 + 同步管线定案（task-128，SESSION-139）

用户裁定终态 = **linbleePSX 在 Linux 原生运行 PSX 游戏，零 BIOS、零 payload.bin、
零任何版权工件**；每楔进展强制同步 GitHub。本文 = 战役底数：三面现状、HLE 需求面、
镜像审计与同步管线。此前 ②（BIOS 马拉松）/③（payload Wine）两线的资产按
"参考语义、不携字节" 的清洁房纪律收编。

## 1. port 四面现状底数（无 BIOS 基线，实证 @44db760）

`BLEEM_BIOS` 不设置时全链重建 + 四面 selftest：

| 面 | selftest | 结果 |
|---|---|---|
| psx_core（内存映射/翻译/分派表） | `--psx-selftest` | 31 PASS / 0 FAIL, rc=0 |
| psx_cpu（R3000A 整数核+最小 cop0） | `--cpu-selftest` | 16 PASS / 0 FAIL, rc=0 |
| psx_dev（MMIO 设备，含 CDROM minimal face） | `--dev-selftest` | 10 PASS / 0 FAIL, rc=0 |
| eng（资源/payload 格式 dry-run） | `--eng-selftest`* | 9 PASS / 0 FAIL, rc=0 |

*eng 面是 payload 格式引擎——清洁房战役中降级为**纯格式知识源**（LZ/位流语义已有
文献价值），运行期不再是依赖面对象。

已就位可复用：guest 地址翻译三窗（RAM/ROM/MMIO 原址 MAP_FIXED）、CPU 延迟槽/异常簿记、
GPU 端口族 MMIO 窗（0x1F801000 子窗）、CDROM 寄存器 minimal face（0x1820-27
CDREAD/CDSTAT/SEL + 上电 ACK）、vsync 中断位（task-122 定案 0x1）。

缺口（= task-129..133 工作面）：HLE 内核 syscall 分派（现在无 BIOS = 无处可跳）、
BIN/CUE+ISO9660 读取、冷启链（0xBFC00000 向量 → HLE 装载 PSX.EXE）、GPU 命令消费者 +
软件帧缓冲 → vk_host 出画、SPU/pad 后补。

## 2. HLE 需求面（自研内核契约，清洁房纪律）

**纪律**：不复用 SCPH1001.BIN 任何字节、不执行之；A/B/C rootcall 的**命令号与参数
ABI** 按公开 SDK 契约 + 本仓库既有语义账（docs/14 表几何、docs/41 cmd 语义、
docs/47 归名锚）自研实现。金标准 RAM dump（docs/18/23-25）只作**行为对拍参考**，
不入库、不拷贝进 HLE 镜像。

首批实现集（task-129 验收面，全表号段几何占位、未实现=-1 哨兵）：
- 分派骨架：syscall 异常 → a0=cmd → 分派表 → handler 返回 `pc=r[31]`（rootcall 桩语义）；
- 首批 handler：InitHeap/AllocMem 族（2MB RAM 伙伴堆）、LoadImage（PSX-EXE 头解析+段灌入）、
  Execute(first)、AddRC/DelRC/InsertRC/FlushRC（事件环）、AddIntCue/VSync 回调、
  PutC/Printf（TTY→宿主 stdout）、dev boot（devsw 表：tty:/cdrom: 注册）；
- B/C rootcall：B(0x10-0x1F) 内存分配环、C(0x02/0x03) open/close 文件 API（对接 task-130 ISO 面）。

依赖的公开契约文档锚 = PSYQ SDK rootcall 表 + 本仓库 docs/14/22/41/47/91/92。

## 3. 镜像审计 + 同步管线（tools/sync_to_github.sh）

- 审计发现①：镜像本地曾**积压 13 个未 push 提交**（SESSION-124..137 的 sync 提交只
  commit 未 push）——"每进展同步"此前未闭环。本楔首发 sync 已推平（c2fabc1，
  mirror == origin/main）。
- 审计发现②：镜像 336+18 个受管文件中**零二进制版权工件**（grep 命中均为文档/工具
  文件名文本，如 `docs/05_payload.md`）；`.gitignore` 已禁 `*.EXE/*.bin/data//dumps/`。
- 管线定案：`rewrite/tools/sync_to_github.sh "<msg>"` =
  rsync(port→mirror/port, rewrite→mirror/reconstruction，排除 build/data/dumps/
  decrypted/sandbox/analysis + 全部二进制版权扩展名) → `git add -A` →
  **BANNED_RE 暂存区扫描**（命中即 reset 回滚 rc=2）→ harness 署名 commit →
  push origin main（失败保留 commit rc=3）。`--check-staged` 独立模式供验收门负测试。
- 每楔强制：validate_host12x 门内必须含"sync PASS + mirror==origin/main"判据。

## 4. 验收锚（战役终态，task-133）

自研 homebrew HBEXE（本仓库 tools 生成，非版权件）打包进自造 ISO →
冷跳 0xBFC00000 → HLE kernel → A/B rootcall 装载 Execute → GPU 出画 →
全链 `grep -r BLEEM_BIOS` 仅存可选调试位。用户自有盘仅本地装载测试，永不入库。
