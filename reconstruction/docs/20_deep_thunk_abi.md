# 20 — img 引擎深段模板 ABI 第一批（H-14，SESSION-32）

对象：`decrypted/stream_10a4c_L71cbf_relocated.bin`（img 基址 0x400000）。
深段 4 eips（004002bc/c1/c7/cf）= **同一个分发 thunk**，全新解码：

## 1. 深停机 thunk（0x4002b8-0x4002d2）
```
4002b8: mov 0x4(%esp),%edi        ; arg1 = 引擎上下文对象 ctx
4002bc: mov $0x328400,%ebx        ; PSX RAM 堆指针常量(0x80328400→RAM 0x128400, root2 邻域; img 内 4 处引用: 40018d/4002bd/41076a/45a7a3)
4002c1: mov 0x44(%edi),%eax       ; ctx+0x44 = 唯一实参 → EAX
4002c7: call *0x174(%edi)         ; ★ctx+0x174 = 分发槽(宿主运行时填充, img 内无静态写者)
4002cd: push $0xffffffff          ; 返回后 continuation=-1
4002cf: jmp 0x400108              ; tail dispatcher(docs/12 结论同源)
```
- **语义定案**：该 thunk = 引擎对"被编译目标函数"的调用桩。槽 `ctx+0x174` 应由
  **BIOS MIPS→x86 编译产物**填充；task-024 观测的槽值 `BFC60008/BFC50000` = 我们宿主
  把 0xBFC00000 窗当 ROM 数据映射所致——**bleem! 真语义 = 0xBFC00000 是 x86 侧编译码区 VA**。
  深段推进 = 宿主必须 (a) 在 0xBFC00000 生成编译 BIOS x86 码并 (b) 把入口写进 ctx+0x174。
- thunk 前导（0x400280-0x4002b0）：`cmp $0x104` 魔数分支 + 参数三元组
  `0x2a0020/0x2a0021/0x2a0010/0x2a000d → ctx+0x254/5c/58`（scratch 池常量，img 各仅 1 处
  = 专用协议），`popa; lea 0x2d2(edx); ret` = 模板序言安装器。
  **与 host/shims.s 的 0x2a0000 scratch 页协议同源互证**。

## 2. tail dispatcher（0x400108-0x40016e）
```
edi=[esp+8]; 对 ctx+0x40 / +0x34 / +0x3c / +0x38 四槽依次:
    push 0; push 槽值; push 0x1000a; call *[0x4022a4]
pop/ret 路径 → jmp 0x4002b0 收尾
```
- `0x1000a` = API 常量（img 内 12 处），`[0x4022a4]` = 全局 API 函数指针（img 静态值 0x10189
  = 未重定位占位/内部偏移；运行时由宿主 API 表接管——docs/08 api_dispatch 的落点）。
- 四槽清理 = continuation 协议（释放 ctx 内 4 个句柄后收尾）。

## 3. 后续（H-14b+ 待做）
- 反汇编 0x41076a/0x45a7a3 两处 0x328400 引用（堆管理器候选）。
- ctx 对象字段表：+0x34/0x38/0x3c/0x40（句柄）、+0x44（实参）、+0x174（分发槽）、
  +0x254/0x258/0x25c（模板参数三元组）—— 与 0x5AC4C slot 表(14574 项)+0x7680 步进 16 对齐验证。
- 最小深段推进实验：宿主 JIT 编译 BIOS 入口 → 写 ctx+0x174 → 观察 thunk 调用成功。

## 4. 复验
- `bash tools/validate_host24.sh`（thunk/dispatcher 反汇编断言 + 文档事实）。
