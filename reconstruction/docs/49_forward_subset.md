# 49 — M-6b 可转发子集终表（task-063，SESSION-72）

hprobe 双态 A/B（A=金标终态原样；B=PROBEEX 自建派发块+H_BIOSEXN 真异常分发），
全 50 handler、budget=200000：**两态 BREAK 集完全一致(26/50，零分歧)** ⇒ 可转发子集对安装态鲁棒。

## 1. 终判
- **可转发核 = 26 个服务**（任一态 BREAK）；其余 24 个需设备语境(CD/GPU 对象、事件环)——
  这是 H-25 的真分解：**先接 26 个纯服务，再按 HALT 原因逐个补设备语境**。
- A/B 零分歧同时证伪'B0 族需要特殊安装派发块'——金标表+jr 向量语义在两态下同果。

## 2. 逐 handler 终表（A 态=budget 200000）

| vec | cmd | handler | A态 | B态 | HALT 原因归类 | v0 |
|---|---|---|---|---|---|---|
| 0xa0 | 0x0e | bfc02918 | BREAK | BREAK |  | 00000000 |
| 0xa0 | 0x13 | bfc02240 | BREAK | BREAK |  | 00000000 |
| 0xa0 | 0x15 | bfc03190 | BREAK | BREAK |  | 00000000 |
| 0xa0 | 0x16 | bfc03200 | BREAK | BREAK |  | 00000000 |
| 0xa0 | 0x17 | bfc03288 | BREAK | BREAK |  | 00000000 |
| 0xa0 | 0x18 | bfc03310 | BREAK | BREAK |  | 00000000 |
| 0xa0 | 0x19 | bfc033c8 | BREAK | BREAK |  | 00000000 |
| 0xa0 | 0x1b | bfc03494 | BREAK | BREAK |  | 00000000 |
| 0xa0 | 0x28 | bfc01acc | BREAK | BREAK |  | 00000000 |
| 0xa0 | 0x2a | bfc02b50 | BREAK | BREAK |  | 00000000 |
| 0xa0 | 0x2f | bfc02200 | BREAK | BREAK |  | 00002282 |
| 0xa0 | 0x33 | bfc01e5c | BREAK | BREAK |  | 80140070 |
| 0xa0 | 0x34 | bfc020f0 | BREAK | BREAK |  | 00000000 |
| 0xa0 | 0x39 | bfc01e24 | BREAK | BREAK |  | 00000000 |
| 0xa0 | 0x3f | bfc018e0 | BREAK | BREAK |  | 00000000 |
| 0xa0 | 0x49 | bfc03fac | BREAK | BREAK |  | 00000000 |
| 0xa0 | 0x70 | bfc09914 | BREAK | BREAK |  | 00000000 |
| 0xa0 | 0x72 | bfc072b8 | HALT | HALT | ROM 内停机(设备语境缺失) | 00000000 |
| 0xa0 | 0xa1 | bfc0d950 | HALT | HALT | 预算耗尽自旋(等事件) | 00000000 |
| 0xa0 | 0xab | bfc0c0dc | BREAK | BREAK |  | 00000000 |
| 0xa0 | 0xad | bfc0c1dc | BREAK | BREAK |  | 00000000 |
| 0xb0 | 0x07 | bfc01b44 | BREAK | BREAK |  | 00000000 |
| 0xb0 | 0x08 | bfc01d8c | BREAK | BREAK |  | 00000000 |
| 0xb0 | 0x09 | bfc01e1c | BREAK | BREAK |  | 00000000 |
| 0xb0 | 0x0b | bfc01ec8 | HALT | HALT | serve 上限(嵌套深服务) | 00000000 |
| 0xb0 | 0x0c | bfc01f10 | HALT | HALT | 预算内自旋(等事件) | 00000000 |
| 0xb0 | 0x12 | bfc04ac8 | HALT | HALT | serve 上限(嵌套深服务) | 00000000 |
| 0xb0 | 0x13 | bfc04b90 | HALT | HALT | serve 上限(嵌套深服务) | 00000001 |
| 0xb0 | 0x14 | bfc04c10 | HALT | HALT | serve 上限(嵌套深服务) | 00000001 |
| 0xb0 | 0x15 | bfc0609c | HALT | HALT | serve 上限(嵌套深服务) | 00002000 |
| 0xb0 | 0x16 | bfc061c4 | HALT | HALT | serve 上限(嵌套深服务) | 00000000 |
| 0xb0 | 0x17 | bfc00f40 | HALT | HALT | serve 上限(嵌套深服务) | 00000001 |
| 0xb0 | 0x19 | bfc00f20 | HALT | HALT | serve 上限(嵌套深服务) | 00000002 |
| 0xb0 | 0x20 | bfc01c5c | BREAK | BREAK |  | 00000000 |
| 0xb0 | 0x32 | bfc02958 | BREAK | BREAK |  | 00000000 |
| 0xb0 | 0x34 | bfc02b28 | HALT | HALT | serve 上限(嵌套深服务) | bfc4c1b0 |
| 0xb0 | 0x35 | bfc02c94 | BREAK | BREAK |  | 00000000 |
| 0xb0 | 0x36 | bfc02e00 | HALT | HALT | serve 上限(嵌套深服务) | 00000000 |
| 0xb0 | 0x42 | bfc039a4 | BREAK | BREAK |  | 00000000 |
| 0xb0 | 0x43 | bfc03a70 | HALT | HALT | serve 上限(嵌套深服务) | 00000001 |
| 0xb0 | 0x44 | bfc03aac | HALT | HALT | ROM 内停机(设备语境缺失) | 00000000 |
| 0xb0 | 0x45 | bfc03744 | HALT | HALT | ROM 内停机(设备语境缺失) | 00000000 |
| 0xb0 | 0x46 | bfc03810 | HALT | HALT | ROM 内停机(设备语境缺失) | 00000000 |
| 0xb0 | 0x4a | bfc05da8 | HALT | HALT | ROM 内停机(设备语境缺失) | 00000000 |
| 0xb0 | 0x4b | bfc04c70 | HALT | HALT | ROM 内停机(设备语境缺失) | 00000000 |
| 0xb0 | 0x50 | bfc04d3c | HALT | HALT | ROM 内停机(设备语境缺失) | 00000000 |
| 0xb0 | 0x5b | bfc043d0 | HALT | HALT | ROM 内停机(设备语境缺失) | 00000000 |
| 0xc0 | 0x02 | bfc01420 | HALT | HALT | ROM 内停机(设备语境缺失) | 00000000 |
| 0xc0 | 0x03 | bfc01444 | HALT | HALT | ROM 内停机(设备语境缺失) | 00000000 |
| 0xc0 | 0x0a | bfc015d8 | HALT | HALT | ROM 内停机(设备语境缺失) | 00000000 |

BREAK 并集 = 26/50

## 3. 最小集核对(docs/41 {00,01,07,08,0C,12,1C}@0xB0)

- cmd 0x00: 不在 C2 发出面
- cmd 0x01: 不在 C2 发出面
- cmd 0x07: A=BREAK B=BREAK
- cmd 0x08: A=BREAK B=BREAK
- cmd 0x0c: A=HALT B=HALT
- cmd 0x12: A=HALT B=HALT
- cmd 0x1c: 不在 C2 发出面

## 4. H-25 分批建议
- 批1(纯转发, 无设备): 上表 BREAK 26 项——hprobe 骨架即参考实现。
- 批2(轻语境): '落零页/向外面'类——补向量页更多槽+serve 预算即可复测升级。
- 批3(重语境): 'ROM 内停机(设备语境缺失)'与'自旋等事件'——需 CD/GPU 对象建模, 归 H-25 主体。
