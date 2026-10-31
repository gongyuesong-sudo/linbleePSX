# 107 — ③战役楔4：落地全量取证（task-126，SESSION-137）

## 1. 时序修正（本楔第一个硬发现）
`MSGBOX@0x401117` 时刻 **booter 清理三连（UnmapViewOfFile/CloseHandle）已跑完**
——上一楔 dump"前 4KB 全零"的真因=**view 已被 unmap 后该区被回收**，
不是"原地重映射"（上楔推断更正）。正确取证时机 = **`0x4010c4`**
（CALL EDX 刚返回、MOV 错误码未执行、view 仍在位）：
gdb 实测 `POST_CALL eax=6 errcode=0` = **EAX 已由 payload 带回 6**，完美窗口。

## 2. 落地区完整布局（winedbg 断住 → 本楔经 gdb dump 通道取得）
`/proc/<pid>/maps`（wineserver 侧读被 ptrace 权限墙拒=mem 不可直读，
gdb `dump binary memory` 是唯一批量通道）：
| 区 | 权限 | 大小 | 判读 |
|---|---|---|---|
| 0x01060000-0x01090000 | rwx | 192KB | 解压落地 A（nonzero 47%）|
| 0x01160000-0x012e0000 | rwx | **1.5MB** | **解压落地 B=引擎主体**（nonzero 58%）|
| 0x012e0000-0x01336000 | r-x**s** | 344KB | demo_v2 文件只读共享映射（view）|
dump 资产（不入 git，`.analysis/w9/`）：
`mem_rwx_a.bin` sha89753627 / `mem_rwx_b.bin` shad702d7f8 / `mem_view.bin` sha3f0a66cb。

## 3. xref 判读与负结果（诚实入账）
- 两 rwx 落地区对 booter 关键常量（0x401124 错误码/0x401130 view/
  0x4013BA/0x40142D 文案指针）**零直接引用** = payload 检测走寄存器/
  自有数据面，无"直读 booter 全局"捷径可循；
- view 区命中 0x401124/0x401130 = **booter .text 自身拷贝**（映射文件
  0x63x-0x70x 区），非 payload 代码——首个 xref 假阳性已辨明；
- `mov eax,{3..6}; ret` 简单返回模式在两落地区**零命中** = 返回码经
  混淆/运算产生，或检测函数不在本次 dump 覆盖的两区内（可能存在
  运行中临时 map→free 的第三区）。

## 4. ③W5 定案（静态→动态回马枪）
静态反汇编 1.75MB 混淆体性价比已低。下一楔回动态：
在 `0x4010c4` 断点**反推最后执行痕迹**——对 CALL 内执行尾段用
"stepi 尾段录制"或**硬件执行断点分批采样**（rwx_b 首页 `lb` 探
引擎控制流入口），定位 EAX:=6 的实际写点=检测函数本体；
或退一步按 docs/02 槽位表**语义实验判别**：给 payload 喂不同环境
变量（真 ISO/无 fakecd/DX 组件增减）观察 errcode 变化谱，
反推 6 号槽实际检测面（低成本先跑）。

## 5. 工程坑两条
- `pkill -9 -f demo_v2` **匹配自身 cmdline 自杀**（AGENTS 已有铁律，
  本楔再犯再证）——wine 目标一律 `pgrep -x 'demo_v2.EXE'` 精确杀；
- `gdb detach 不杀 inferior` + `shell sleep` 挂桥 = /proc maps 可查但
  mem 被 ptracer 独占——批量 dump 须在断点会话内一次完成。
