# 03 · 追加区明文桩: 私有运行时链接器 (0x55338..0x55DF3)

Evidence: `analysis/stub_fns/` 17 函数伪代码全文, 汇总
`analysis/stub_pseudocode.txt`; raw 镜像快照 `analysis/stub-snapshot.json`。

> 注: Ghidra 对 raw 镜像把同一段体切成了多份重叠“函数”
> (0x553ca/0x553da/0x553e2 是同一段调度代码的 3 个入口;
> 0x5546d 与 0x55597 是模块加载主循环的两份近孪生拷贝)。
> 本文件按**单一程序流**合并解读。

## 1. 入口 (0x55338)

```
清零 5 槽模块表 (栈)
FUN_00055586: return *(fs:[0x20])            ; TEB->PEB (不用 GetModuleHandle)
FUN_00055895: api_table(0x4F, 写 0x002A0000 缓冲) -> *(0x2A0000)(0x2A002A|0x2A0021)
              ; [未知] 自检/查询类; 结果 +0xC7A 丢弃
CALL *(view + 64000=0xFA60)                  ; 加密区解压引导
块调度 (0x553ca 群): 递归遍历块链表, RAND 校验 + 间接初始化调用
条件满足 -> 模块加载主循环 (0x5546d/0x55597)
```

自定位证据: 区内存在对立即数 `0x4010C4`(=引导器 CALL 后一条指令 VA,
即桩的**返回地址**) 的 `sub eax,0x4010c4` 运算 —— 从返回地址反推
image_base/view_base, 配合 PEB 读取, 全程零 API。

## 2. API 编号派发 (0x4022A4 间接表)

调用形态 `(**(code **)(image + 0x4022A4))(id, ...)`。
观察到的编号与参数形态 (0x55716/0x55459):

| id | 参数形态 | 解读 [推断] |
|---|---|---|
| 0x10000 | (0, size_pages<<12, flProtect=0x400, MEM, pages) | VirtualAlloc RWX |
| 0x10001 | (addr>>12, 0x10000, 4, 0, 0x60008) | VirtualProtect |
| 0x1000a | (addr, size, 0) | VirtualFree |
| 0x004F  | (0x2A0000 缓冲, 表, ...) | [未知] 查询 |

## 3. 完整性: "RAND" XOR 折叠

块描述符 (步长 0x10): `{+00 rva(bit0=间接), +04 依赖表, +08 size, +0C checksum, +010 end}`

```
fold  = (rva & ~3) ^ 0x52414E44        ; 'RAND' —— Randy Linden 签名
for dword in [rva&~3, end): fold ^= dword
checksum == *(desc+0C) ?  : 放弃该块
seen[] 栈表防环; 块号 <0 终止链表
```

校验失败的块被静默跳过 —— 篡改检测行为 (与 demo 反破解定位一致)。

## 4. 模块加载主循环 (0x5546d/0x55597 合并)

```
A. total = Σ desc(+8) (5 模块尺寸), &= 0x7FFFFFFF
B. region_a = rwx_alloc(total); region_b = rwx_alloc(...)     ; 0x55716
C. 每模块: 头字段 bit0=1 -> CALL 间接头函数取实际值;
   若 (v & 2): FUN_0005577a(目标=region_b, 源, 参数)           ; LZ 解压
D. IAT 绑定 (0x5556d): word 流 (slot=v>>1, 终止=slot==0, last=v&1):
     import_table[slot] = image + dword        ; 内部导入
     last: import_table[slot] = sys_table[dword] ; 系统导入
E. 双遍重定位 (外层 sel=v>>1, bit0 选模块基/API 基):
     内层 word w: target = base + (w>>2)
     w&2: *target += delta - (DWORD)target      ; 相对 (R_X86_32PAIR 风格)
     否则: *target += delta                     ; 绝对
   两遍: 第一遍处理 seen 槽模块, 第二遍处理 (hdr&1)==0 的全部模块
F. 每模块 (desc(+34) & 2): CALL *(&~3)          ; 模块入口/DllMain
G. 收尾 (0x55459): api(0x1000a) 释放临时区 (1..2 次)
```

## 5. 位流解压器 (0x5577a + 读取器群)

位读取器群是同一模式的 1/2/3/4/8 位展开 (0x55998→0x55960):
EBX=缓冲 LSB 先消费, ESI 指向 dword 并**向低地址**回退重装,
结果 `acc = acc*2 | bit` —— 字节内低位先存、拼出高位先出。

解压器输出游标**从高地址向低地址**写 (就地展开); 令牌树在本工程
`src/lzss.c` 按伪代码逐支转写 (字面量串/2/3/4 字节匹配/长匹配,
距离窗口 0x200/0x400/0x1000 三档 + `+5` 超长形态) [中置信, 需动态对拍]。

## 6. 桩的角色总结

这是一个自制的 1999 年 packer/保护壳: 压缩(自定义 LZ) + 运行期链接
(重定位/IAT) + 反篡改(RAND 校验) + 反 API 静态面(编号派发 + PEB 自定位)
+ 反分析(错误码 5/6 调试器门在加密区实现)。
