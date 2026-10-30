# 12 — H-8b 实盘 BIOS 深执行观测：首次越过 0x4002b7（SESSION-19/20）

ROM：用户提供合法自有 SCPH1001（524288B，
SHA-256 `71af94d1e47a68c11e8fdb9f8368040601514a42a5a399cda48c7d3bff1e99d3`，
登记于 `host/build/bios_sha256.txt`（本地生成），与 htrace.log `BIOS-LOADED real ROM @0xBFC00000` 双向印证）。
完整 trace：`host/build/htrace_deep.log`（190,409 行，本地生成，不分发）。validator：`tools/validate_host11.sh` PASS rc=0。

复现：
```bash
gcc -m32 -O0 -g -no-pie -o host/build/htrace host/htrace.c host/tramp32.s
H_SLED_CAP=3000 BLEEM_BIOS=/path/to/own/SCPH1001.BIN \
  timeout 280 ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin 0x400000 0x472000
```

## 一、实测数据（相对 H-6a 无 ROM 基线的突破）

| 指标 | 无 ROM（H-6a） | 实盘 ROM（本轮） |
|---|---|---|
| 唯一 img eip | 131 | **159** |
| img 段最大 eip | 0x4002b7 | **0x4002cf** |
| 深段（>0x4002b7）eip | 0 | **4**（0x4002bc/0x4002c1/0x4002c7/0x4002cf） |
| BIOS 向量进入 | 0 | **1**（0xBFC00000 → 0x4002b8） |

## 二、机制发现

1. **img+0x2b7 是尾派发 thunk（`ret`）**：弹出 BIOS 指针后 x86 直接滑进 MIPS 字节（SEGV@0xBFC00002）——
   这就是 H-6a 之前"0x400300 盲障"的真相：引擎从不"调用" BIOS，而是 ret 滑入。
2. htrace 修复：实盘 ROM 时 ontrap 早捕获整个 0xBFC00000..0xBFC80000 窗；恢复语义 =
   栈上找 img 返回址，否则 ret-site+1（0x4002b8，stdcall arg1←cb 约定修正）；空服务槽调用（eip==0）走 svc 恢复。
   老 HLE ret-sled 路径未动（validate_host10 legacy 回归 131/0x4002b7 仍 PASS）。
3. **深段终点死因**：引擎回到 init 序后在 0x400002 向 `EAX=0xffffff64` 存数——该 VA 派生自流尺寸 0x71cbf，
   宿主缺"按引擎请求供给 VA"的语义。页供给实验已证伪（把引擎活锁在宿主码里，已回滚并留注释）。
4. 二次 fatal 风暴用 die_armed 防护（首个 longjmp，次个 _exit(5)）。

## 三、VERDICT（路线判定，实证闭合）

**PSX 执行要求 BIOS 向量处存在 MIPS 解释器/JIT 生成的 x86；x86 侧 HLE 无法代跑 ROM**——
引擎向宿主索取的正是"把 MIPS 编成 x86 回填 BIOS 窗"这一步（docs/10/11 的 JIT 角色），
明文 img 内无该驱动（docs/11 布局测绘 + 本轮深段仅 4 eip 再证）。
BIOS 输入接线本身已全链路可用（task-017/018 双 validator）。

## 四、后续候选

- 补宿主 VA 供给语义，观察深段能否突破 0x4002cf（预期仍撞 MIPS 执行墙）
- 若要真正跑通 PSX：需自研 BIOS-MIPS→x86 JIT（工程量=重写引擎核心，超出"移植"范畴，归路线 C 评估）
