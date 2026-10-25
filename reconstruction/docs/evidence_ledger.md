# Evidence 台账 (可复核锚点)

| # | 结论 | 类型 | Evidence / 锚点 |
|---|---|---|---|
| E1 | 目标身份 PE32 GUI i386, sha256 b61ee13e…09da | 观察 | `analysis/overview.json` ev_0b04f95b…d2617 |
| E2 | 段布局 (5 段) + 追加区 287,732B | 观察 | E1 + 手工 PE 头解析 (tools/extract_payload.py 复现) |
| E3 | 可见代码仅 entry/FUN_00401021, 全指令 259 条 | 观察 | `analysis/fn_entry.json` `analysis/fn_401021.json` |
| E4 | 错误表 0x4011C4[0..6], g_error_code 初值 2 | 观察 | 0x4011C0..0x4011E0 原始转储 (docs/02) |
| E5 | payload 入口 ABI: EAX=view, 返回 EAX=错误码 | 观察 | 0x4010B1-0x4010C6 指令流 (docs/02) |
| E6 | 桩 17 函数, RAND 校验 / 5 模块 / LZ / API 表 0x4022A4 | 观察 | `analysis/stub_fns/` raw 快照 stub-snapshot.json |
| E7 | PEB 自定位 fs:[0x20]; 返回地址 0x4010C4 特征扫描 | 观察 | FUN_00055586; 0x55338 区反汇编 |
| E8 | API id 0x10000/1/0xa ↔ VirtualAlloc/Protect/Free | **推断** | 参数形态吻合 (docs/03 §2) |
| E9 | 令牌嵌套顺序 / 块表基址 / 0x4F 语义 | **未知** | 已隔离在 src 注释 [未知] 标记处 |
| E10 | 3..6 号错误由 payload 返回 | 观察+推断 | 明文路径只产生 0/1/2 (E3/E4) |
| E11 | UI 功能地图 20 对话框 + 图标组 + 版本串 | 观察 | `analysis/dialog_strings.json` `analysis/res/` |
| E12 | 110 导入为打包遗留, 运行期走编号派发 | 推断·高 | .idata 面 vs 桩调用形态 (docs/01/03) |
| E13 | 重建 loader 与原 entry 反编译同构 | 观察 | `build/` 复验: ev_64078e71…35 (0x2, 0x11010, 0x55338 全同) |

分析中间产物全量保留于 `../.analysis/` (Ghidra 快照 ×3: bleem / payload_tail / payload_stub)。
