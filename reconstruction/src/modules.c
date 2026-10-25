/*
 * modules.c —— payload 桩的 5 模块运行时链接器 (FUN_0005546d / 0x55597 /
 *              0x5556d / 0x55716 / 0x55459) 的重建
 *
 * 证据: analysis/stub_pseudocode.txt。原码存在两份几乎逐指令相同的拷贝
 * (0x5546d 与 0x55597, 后者含前导块复制), Ghidra 把同一段体切碎成多个
 * “函数”; 下面合并为单一转写。
 *
 * 观察到的完整流程 [高置信]:
 *   A. 汇总 5 个模块的 size 字段 (desc->+8), 掩掉符号位;
 *   B. 经 API 表两次分配 RWX 区 (FUN_00055716:
 *      VirtualAlloc(0x10000, size_pages<<12, MEM_COMMIT, PAGE_EXECUTE_READWRITE[0x400])
 *      + VirtualProtect 0x60008 页; 失败路径用 0x1000a 号调用释放) ——
 *      调用号->系统 API 的对应为 [推断], 参数形态与 VirtualAlloc 吻合;
 *   C. 对每个模块: 取头字段, bit0 则先 CALL 间接头函数, 若 (v&2):
 *      以第二分配区为目标调 FUN_0005577a 解压 (lzss.c);
 *   D. IAT 绑定 (FUN_0005556d): 读 word 流 (val>>1=槽号, val&1=是否终止),
 *      *(import_table + slot*4) = image + dword; 置位则 *(api_table + dword);
 *   E. 两遍重定位 (0x5546d 后半 / 0x55597 后半):
 *      流元素 (val>>1 = 模块/表索引, bit0 选择模块基 or API 基;
 *      内层 word: addr = val>>2, bit1=相对修正):
 *        绝对:   *target += base
 *        相对:   *target += base - (DWORD)target
 *   F. 每模块 (desc->+34 & 2): CALL 入口 (*(ptr & ~3))();
 *   G. 收尾以 0x1000a 号调用释放临时区 (FUN_00055459)。
 *
 * 头字段偏移 (模块描述符数组, 元素为 DWORD 索引, 步长 4; 描述符本体
 * 由 视图内相对偏移 +0x24/0x28/0x2C/0x30/0x34/0x3C 引用 —— 原栈槽,
 * 具体常量 [未知], 以 ctx 字段名保留结构)。
 */
#include "bleem.h"
#include "internal.h"

/* 块描述符表在视图内的 RVA —— [未知] 常量, 只随加密 payload 解密后
 * 才可确认; 集中于此, 便于动态验证阶段回填。 */
DWORD g_desc_table_rva = 0;

typedef struct {
    char   *image;        /* 映射视图基址 (原 EDI 上下文)                 */
    DWORD   total;        /* uStack00000018: size 合计                    */
    void   *region_a;     /* p1: RWX 主区                                 */
    void   *region_b;     /* p2: RWX 解压目标区                           */
    DWORD   mod[BLEEM_MODULE_COUNT];   /* 原栈 seen/指针槽 0x38..        */
    DWORD   mod_size[BLEEM_MODULE_COUNT]; /* piVar8[5] 暂存 (+20)         */
} bleem_ctx;

/* ---- 分配原语 (FUN_00055716) ----------------------------------------- */
static void *bleem_rwx_alloc(const bleem_ctx *ctx, DWORD bytes)
{
    bleem_api_dispatch_t api = bleem_api_table_at(ctx->image);
    DWORD pages = (bytes + 0xFFF) >> 12;
    void *p = (void *)api(BLEEM_API_ALLOC, 0x10000, pages << 12,
                          /*flProtect*/ pages, /*flAlloc*/ 8, pages);
    if ((int)p == -1) return NULL;
    if (api(BLEEM_API_PROTECT, (DWORD)p >> 12, 0x10000, 4, 0, 0x60008) == 0) {
        api(BLEEM_API_FREE, (DWORD)p, 0);        /* 原码: 0x1000a, 尾参 0 */
        return NULL;
    }
    return p;
}

/* ---- IAT 绑定 (FUN_0005556d) ----------------------------------------- */
/*
 * word 流 (unaff_ESI): v = *p; last = v&1; slot = v>>1; slot==0 -> 结束;
 *   *(tbl + slot*4) = image + *(p+1)          ; 普通导入
 *   last 时: *(tbl + slot*4) = api_table[...]; 系统导入 (in_EDX 表)      */
static void bleem_bind_iat(char *image, const WORD *stream,
                           DWORD *import_table, const DWORD *sys_table)
{
    for (;;) {
        WORD v = *stream;
        BOOL last = (v & 1) != 0;
        DWORD slot = v >> 1;
        if (slot == 0) return;
        stream += 2;
        import_table[slot] = (DWORD)image + *stream;
        if (last) import_table[slot] = sys_table[*stream];
    }
}

/* ---- 重定位流 (两遍共用) --------------------------------------------- */
/*
 * 外层 word: sel = *p>>1; bit0: sel 索引 API 基表(in_stack_10), 否则模块
 *   索引表(in_stack_2c/34); 内层 word: addr = v>>2 (字节地址), bit1 = 相对。 */
static void bleem_apply_relocs(DWORD mod_base, const WORD *reloc_stream,
                               DWORD base_for_fixup)
{
    const WORD *p = reloc_stream;
    for (;;) {
        WORD outer = *p;
        DWORD idx = outer >> 1;
        if (idx == 0 && !(outer & 1)) break;     /* 终止: 0 号且非 API     */
        /* 选择修正基址: 模块基址 or API 表基址 (原码两索引表) [结构推断] */
        DWORD delta = base_for_fixup;
        WORD w;
        do {
            w = *++p;
            DWORD *target = (DWORD *)(mod_base + (w >> 2));
            if ((w >> 1) & 1) *target += delta - (DWORD)target;  /* 相对 */
            else              *target += delta;                  /* 绝对 */
        } while (!(w & 1));
        p++;
    }
}

/* ---- 模块加载主循环 (FUN_0005546d) ------------------------------------ */
int bleem_module_load(bleem_ctx *ctx)
{
    int i, rc = 0;
    ctx->total = 0;

    /* A. 汇总尺寸 */
    for (i = 0; i < BLEEM_MODULE_COUNT; i++) {
        if (ctx->mod[i]) {
            DWORD *desc = bleem_module_desc(ctx->image, ctx->mod[i]);
            ctx->mod_size[i] = desc[2];               /* +8: size 字段    */
            ctx->total += ctx->mod_size[i];
        }
    }
    ctx->total &= 0x7FFFFFFFu;                        /* 0x5546d 掩码     */

    /* B. 双区分配 (失败 -> 错误码递减路径, 原 in_stack_24 计数) */
    if (ctx->total != 0) {
        ctx->region_a = bleem_rwx_alloc(ctx, ctx->total);
        ctx->region_b = bleem_rwx_alloc(ctx, /*第二尺寸*/ 0);
        if (!ctx->region_a || !ctx->region_b) return BLEEM_ERR_NOMEM; /*[推断]*/

        /* C/E. 头解引用 + 解压 + IAT + 双遍重定位: 原码按 seen 槽逐模块
         * 串联执行 (见文件头注释 C..E), 各步对应:
         *   FUN_0005577a -> bleem_lz_decompress (lzss.c)
         *   FUN_0005556d -> bleem_bind_iat
         *   两遍 word 流 -> bleem_apply_relocs                      */

        /* F. 模块入口调度: (desc_field & 2) -> CALL *(field & ~3) */
        for (i = 0; i < BLEEM_MODULE_COUNT; i++) {
            if (ctx->mod[i]) {
                DWORD *desc = bleem_module_desc(ctx->image, ctx->mod[i]);
                if (desc[0] & 2) {
                    void (*entry)(void) =
                        (void (*)(void))((DWORD)ctx->image + (desc[0] & ~3u));
                    entry();
                }
            }
        }
        /* G. 临时区释放 (FUN_00055459: api(0x1000a)) */
        bleem_api_table_at(ctx->image)(BLEEM_API_FREE, 0x1000a);
    }
    /* 引用静态参考实现, 保留编译期验证 (真实调用点见上方注释 C/E) */
    (void)bleem_bind_iat; (void)bleem_apply_relocs;
    return rc;
}
