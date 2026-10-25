/*
 * stub_entry.c —— 追加 payload 明文桩 (文件偏移 0x55338..0x55DF3) 的重建
 *
 * 证据: REA raw-binary(Ghidra) 反编译 17 个函数,
 *       analysis/stub_fns/0x55338.json 等, 全文伪代码存 analysis/stub_pseudocode.txt
 *
 * 观察到的行为链 (entry @0x55338):
 *   1) 在栈上清零 5 个槽 (模块表指针数组; 循环 "iVar1=5..0, *p=0")
 *   2) FUN_00055586: 返回 fs:[0x20] —— TEB->ProcessEnvironmentBlock,
 *      即 PEB; 原码用它取 PEB->ImageBaseAddress(0x08)[推断] 以定位
 *      API 间接表 image+0x4022A4, 全程避开 GetModuleHandle。
 *   3) FUN_00055895: 通过 API 表调用号 0x4F 查询 (写入 0x002A0000 处的
 *      一个函数指针), 再 *(0x2A0000)(0x2A002A|0x2A0021) 并 +0xC7A —— 
 *      [未知] 疑似自检/定位辅助; 返回值在此未用 (仅影响后续分支?)。
 *   4) 调用 *(view + 0xFA60)()  —— 压缩区的“引导解压”函数, 它必须先把
 *      加密块解开/搬移 (0xFA60 本身位于被加密区, 只能运行时可解)。
 *   5) FUN_000553ca: 递归遍历块链表, 对每个块做 "RAND" 完整性校验,
 *      并调度块初始化 (bit0=1 => 目标是指针槽, 取内容再 CALL)。
 *   6) 若条件满足 (原码: 槽位==0), FUN_0005546d: 完整 5 模块加载循环。
 *
 * 入口 ABI 观察: 引导器调用时 EAX=映射视图基址; 本桩内多次以
 *   (in_EAX + 常量) 寻址 —— 即视图基址经 EAX 传入, 不经栈。
 */
#include "bleem.h"
#include "internal.h"

/* ---- PEB 图像基址 (FUN_00055586 重建) -------------------------------- */
static DWORD bleem_peb_image_base(void)
{
    /* fs:[0x20] = PEB; PEB->ImageBaseAddress 在 Win9x 偏移 0x08。
     * 原 FUN_00055586 仅返回 *(fs:[0x20]) (PEB 指针); 取 ImageBase 的
     * 0x08 偏移是 [推断], 与 0x4022a4 表寻址方式一致性吻合。           */
    DWORD *peb;
    __asm__ __volatile__ ("movl %%fs:0x20, %0" : "=r" (peb));
    return peb[2];   /* 0x08/4 */
}

/* ---- "RAND" 折叠校验 (FUN_000553ca 内层循环重建) --------------------- */
/*
 * 原循环:
 *   uVar7 = (rva & ~3) ^ 0x52414E44;
 *   count = (*(desc+0x10) - (rva & ~3)) >> 2;
 *   do { uVar7 ^= *ptr++; } while (--count);
 *   校验: uVar7 == *(desc + 0x0C)
 * 逐 dword XOR 折叠, 种子为 区域起址^"RAND"。不匹配 => 放弃该块。
 */
static BOOL bleem_rand_verify(const char *image, const DWORD *desc)
{
    DWORD start = desc[0] & ~3u;
    DWORD end   = desc[4];              /* +0x10 */
    const DWORD *p   = (const DWORD *)(image + start);
    DWORD      count = (end - start) >> 2;
    DWORD      fold  = start ^ BLEEM_RAND_SEED;

    while (count--) fold ^= *p++;
    return fold == desc[3];             /* +0x0C 存的校验和 */
}

/* ---- 块链表遍历 + 初始化调度 (FUN_000553ca/da/e2 重建) --------------- */
/*
 * 观察: 表头 *(EDI+4) 指向块号链表 (DWORD, 以负数终止, 即 entry<<1<0);
 * 块描述符 = EDI + id*0x10; bit0(desc[0])=1 => 间接 (先 CALL 取返回值),
 * 处理顺序: 先递归处理依赖 (*(desc+8) 指向的子链表), 再校验/调用本体,
 * 最后置位 seen[id] 防环 (栈上 5 槽数组即 seen 表)。
 * 返回值写回 (id*4 + seen) 槽 [原码 puVar5 语义], 供上层回收。
 *
 * [未知] 依赖表步长/终止条件在反编译里寄存器流混乱; 下方按最自洽
 * 解读实现, 与三处克隆体 (553ca/553da/553e2 是同一段代码的 3 个入口拷贝)
 * 的结构一致。运行时行为未验证 (需要解密 payload)。
 */
typedef struct {
    char  *image;        /* 映射视图基址 (原 EDI 上下文)                  */
    DWORD *seen;         /* 5 槽已处理标记 (原栈数组)                     */
    int    depth;
} bleem_block_walk;

static void bleem_schedule_block(bleem_block_walk *w, DWORD id)
{
    DWORD *desc  = (DWORD *)(w->image) + 0;   /* 占位: 真正基址见下 */
    DWORD base   = (DWORD)w->image;
    desc = (DWORD *)(base + id * BLEEM_DESC_STRIDE);
    /* 实际原码: 块表基址 = EDI (=视图内某固定偏移) —— 视图内偏移量
     * 未能静态锁定 [未知], 这里保留接口。 */
    (void)desc; (void)id; (void)w;
    /* 1. seen[id]!=0 -> 跳过
     * 2. seen[id] = id*4  (标记)
     * 3. 递归处理子依赖链表
     * 4. bleem_rand_verify(...) 失败 -> 放弃
     * 5. 若 addr&1: 间接 —— *(addr_ptr)() 取实际入口;
     *    CALL 入口; 返回值写回 seen 槽
     * 6. 递归调度尾链表下一块
     */
}

/* ---- 桩入口 (0x55338 entry 重建) ------------------------------------- */
__attribute__((used))
int bleem_payload_stub(void *view /* 经 EAX 传入, 见 loader.c */)
{
    DWORD seen[BLEEM_MODULE_COUNT];
    int  i;
    int  result = 0;
    BOOL flag = TRUE;

    for (i = 0; i < BLEEM_MODULE_COUNT; i++) seen[i] = 0;   /* 0x55338 循环 */

    /* FUN_00055586: 原码把 fs:[0x20] 的返回当成"存在性"使用 */
    (void)bleem_peb_image_base();

    /* FUN_00055895: [未知语义] 查询+间接调用, 结果+0xC7A 被丢弃于
     * 本路径 —— 原码同样未用其返回值。保留为桩函数。                    */
    /* (原: (**table)(buf@0x2a0000, table, 0x4f) ... ) */

    if (flag) {
        /* 4) 调用 *(view + 0xFA60)() —— 解压引导 */
        void (*bootstrap)(void) =
            (void (*)(void))((char *)view + PAYLOAD_MAIN_OFF);
        bootstrap();

        /* 5) 块调度 (若桩自身标志允许; 原码此处有对 EAX==0 的判定,
         *    EAX 在 CALL 后仍为视图基址 —— 判定实际由 CALL 副作用设置)  */
        /* bleem_block_walk w = { view, seen, 0 }; walk(&w); —— 见上 [未知] */

        /* 6) 5 模块完整加载 */
        /* result = bleem_module_loader(view, seen); */
        result = 0;
    }
    return result;
}
