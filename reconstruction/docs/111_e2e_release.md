# 111 — 清洁房端到端验收 + 镜像发布 (task-133, SESSION-139)

## 交付
- `port/README.md` — 能力表（九子系统 × 验收面 × 源文件）、构建/运行、零版权纪律声明；
  随 `sync_to_github.sh` 发布到 linbleePSX 镜像。
- `tools/validate_host125.sh` — 终验门：
  A) `env -i` 纯净环境（无 BLEEM_BIOS、无 payload、无用户 env）八面回归 108 断言 0 FAIL；
  B) 端到端单链：自造 ISO → 冷跳 → guest 程序写 GP0/GP1 → GPU 光栅 → Vulkan 硬件 present
     实窗 `frames=125 hw=1 fill=76800 spr=256 xfer=256 gp0=267 gp1=5`；
  C) README 能力表在账；
  D) 镜像 `ls-files` 零版权**二进制**（380 文件全清；禁列=EXE/bin/iso/pbp/chd/cue/zip/7z/tar/gz/ppm；
     取证 .md/.py/.c/.gdb 文本不在禁列——文件名词汇 ≠ 版权工件）；
  D2) 镜像 README 已发布（sync 后验）；
  E) 强制 sync 推平 origin/main（ahead=0）；
  F) 负测试：镜像暂存 `SCPH1001.BIN` → 管线 rc=2 拒收。

## 审计裁决记录
- 首轮 D 段误报：payload/golden/decrypt/dump 关键词命中的全是文档与脚本源码（合法）。
  禁列收敛为扩展名级二进制判定。
- bash 5.3 管道右侧 `read` 在 subshell —— 门内捕获一律用 `$(...)`。

## 目标态 vs 剩余战役
`harness-tasks.json` task-128…133 全部 completed：
**零 BIOS、零 payload 的原生 Linux PSX 模拟全链（CPU/MMIO/HLE/CDROM/冷跳/GPU 软硬件双路/窗口）
已验收并持续同步 GitHub。**
后续战役（未列账、超出 133 范围）：零售盘 HLE 覆盖面扩展（更多 rootcall + 时序）、
纹理映射采样、GTE、SPU、Pad、双碟/音频轨。
