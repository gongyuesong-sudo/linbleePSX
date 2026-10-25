/*
 * bleem! demo v1.4 (demo-v1_4.EXE) 逆向重建 —— 公共定义
 *
 * 目标二进制: PE32 GUI i386, 351,732 字节
 *   SHA-256 b61ee13e2377e8305ede4e8be9752f83163368fadb19f0b3e40774d64e7e09da
 * 分析工具: REA 6.5.0 (Ghidra 12.1.4 headless)
 *   主镜像 Evidence: ev_0b04f95b67376780d700a9dd09e5e93fc8b346c992df93416b5f57b2ea5d2617
 *
 * 布局 (证据: REA binary_overview + 手工 PE 头解析):
 *   .text   VA 0x401000  1536B   引导桩 (entry + FUN_00401021)
 *   .idata  VA 0x402000  3584B   110 个导入 (绝大多数由 payload 时代遗留)
 *   .rsrc   VA 0x403000  56KB    25 图标 / 20 对话框 / 版本信息
 *   .reloc  VA 0x411000
 *   [文件尾追加] 0x00FA00..EOF  287,732B  压缩/加密的多模块 payload,
 *             其明文桩位于 0x55338..0x55DF3 (2,748B)
 */
#ifndef BLEEM_H
#define BLEEM_H

#include <windows.h>

/* ------------------------------------------------------------------ */
/* 引导桩常量 (0x401021, Evidence: fn_401021.json 反汇编)              */
/* ------------------------------------------------------------------ */

/* 引导器把自身文件 MapViewOfFile 后, 以“文件偏移当偏移量”调用 payload 桩:
 *   LEA EDX,[EAX+0x55338] ; CALL EDX   (EAX = 映射视图基址)
 * 桩入口约定: EAX = 映射视图基址; 返回值 EAX = 错误码 (0=成功)。      */
#define PAYLOAD_STUB_FILE_OFF   0x55338u

/* 桩内部再调用 *(view + 0xFA60)() —— 压缩 payload 的解压后主入口。
 * 0xFA60 落在被加密区 [0xFA00,0x55338) 内, 运行时才有效。              */
#define PAYLOAD_MAIN_OFF        0xFA60u   /* = 64000, 见 entry 反编译 */

/* 错误码 -> 消息表 (原表位于 0x4011C4, 槽位 [code], 表初值见 0x4011C8). */
#define BLEEM_ERR_BASE          0x4011C4u
/* g_error_code 初值 = 2 (0x401124 处的数据), 平台检查失败即返回 2。   */
#define BLEEM_ERR_INITIAL       2

enum bleem_error {
    BLEEM_OK              = 0,
    /* 桩自身 (引导器置位): */
    BLEEM_ERR_GENERIC     = 1,  /* "Error loading program!"          */
    BLEEM_ERR_NOT_WIN9X   = 2,  /* "This program requires Windows95
                                   or Windows98!"                     */
    /* payload 返回码 (1..6 由桩的错误表索引, 语义为推断):             */
    BLEEM_ERR_NOMEM       = 3,  /* "Not enough memory to load
                                   program!\n\nClose other
                                   applications and try again."        */
    BLEEM_ERR_REINSTALL   = 4,  /* "Error loading program!\n\nPlease
                                   reinstall and try again."           */
    BLEEM_ERR_SYSDEBUGGER = 5,  /* "...not designed to operate with a
                                   system debugger!..."               */
    BLEEM_ERR_DEBUGGER    = 6,  /* "...not designed to operate with a
                                   debugger!..."                      */
};

/* 消息文案与原版逐字一致 (字符串地址 0x4012F1/0x40131F/0x401336/
 * 0x401382/0x4013BA/0x40142D, Evidence: strs.json 154 条全量搜索)。    */
#define BLEEM_TITLE_TEXT        "bleem, llc."
#define BLEEM_MSG_GENERIC       "Error loading program!"
#define BLEEM_MSG_NOT_WIN9X     "This program requires Windows95 or Windows98!"
#define BLEEM_MSG_NOMEM         "Not enough memory to load program!\n\nClose other applications and try again."
#define BLEEM_MSG_REINSTALL     "Error loading program!\n\nPlease reinstall and try again."
#define BLEEM_MSG_SYSDEBUGGER   "This program is not designed to operate with a system debugger!\n\nPlease disable the system debugger and try again."
#define BLEEM_MSG_DEBUGGER      "This program is not designed to operate with a debugger!\n\nPlease disable or exit the debugger and try again."

/* 作者彩蛋: 紧跟 entry 跳转的明文字符串 (0x401002) */
#define BLEEM_CREDIT_TEXT       "bleem! written by Randy Linden"

/* MessageBox 样式: 原码 push 0x11010 =
 *   MB_ICONHAND(0x10) | MB_DEFAULT_DESKTOP_ONLY(0x1000) | MB_SETFOREGROUND(0x10000) */
#define BLEEM_MSGBOX_STYLE      0x11010L

/* ------------------------------------------------------------------ */
/* payload 桩 (0x55338) 常量 (Evidence: stub_fns 目录 JSON)              */
/* ------------------------------------------------------------------ */

/* 完整性种子: 地址与 "RAND" (Randy Linden) 异或 —— 0x52414e44。
 * 校验: seed = (rva & ~3) ^ 'RAND'; XOR-fold 覆盖 [rva, rva+size)。   */
#define BLEEM_RAND_SEED         0x52414E44u

/* API 间接调用表位于 image_base + 0x4022A4 (桩以此解析系统 API,
 * 绕过自身导入表; 见 FUN_00055716 / FUN_00055895 / FUN_00055459)。    */
#define BLEEM_API_TABLE_OFF     0x4022A4u

/* 观察到的 API 表调用号 (推断, 见 0x55716):                            */
enum bleem_api_id {
    BLEEM_API_ALLOC    = 0x10000, /* VirtualAlloc 风格: 分配大小向上取整
                                     到页; 参数序与 VirtualAlloc 观察一致 */
    BLEEM_API_PROTECT  = 0x10001, /* VirtualProtect 风格 (0x60008 页)     */
    BLEEM_API_FREE     = 0x1000a, /* VirtualFree 风格                     */
    BLEEM_API_QUERY_X  = 0x0004f, /* FUN_00055895 中的查询调用 [未知语义] */
};

/* 模块表: 桩入口处清零 5 个槽位 (FUN_0005546d/FUN_00055597 循环上界=5) */
#define BLEEM_MODULE_COUNT      5

/* 模块描述符条目步长 0x10; 观察到的字段偏移:
 *   +0x00  目标 RVA (bit0=1 间接; 地址本身按 &~3 使用)
 *   +0x04  [未知, 非零判据在 entry 检查中]
 *   +0x08  尺寸/下一 RVA (校验长度 = (*(+0x10) - (rva&~3)) >> 2)
 *   +0x0C  XOR 校验和 (与 "RAND" 种子折叠比较)
 *   +0x10  区域结束 RVA
 */
#define BLEEM_DESC_STRIDE       0x10

/* 16 字节模块块头 (0x553ca 循环中 *piVar10 即块号): 记为块表          */

/* 桩扫描自身返回地址用的特征值: 引导器内 "sub eax,0x4010c4" 立即数,
 * 即 CALL view+0x55338 之后一条指令的 VA —— 用于反转储自定位。         */
#define BLEEM_RETURN_ADDR_HINT  0x4010C4u

#ifndef __cplusplus

/* ------------------------------------------------------------------ */
/* 位流读取器 (FUN_00055960/0x55986/0x5598c/0x55992/0x55998 重建)      */
/* 原始实现以 EBX=位缓冲(从 LSB 逐位消费), EAX=缓冲内剩余位数,          */
/* ESI= dword 源(向低地址回退 *--src) 手工维护; 结果逐位 acc=acc*2|bit  */
/* —— 即每字节内位序: 先读到的是字节高位。                               */
/* ------------------------------------------------------------------ */
typedef struct {
    const DWORD *src;    /* ESI: 压缩流当前 dword (向下消费)            */
    DWORD  buf;          /* EBX: 位缓冲 (LSB 先出)                      */
    int    cnt;          /* EAX: 缓冲内剩余位 (>0)                      */
} bleem_bitreader;

static __inline void bleem_br_init(bleem_bitreader *r, const DWORD *dwords) {
    r->src = dwords;
    r->buf = *dwords;
    r->cnt = 32;
}

/* 对应 FUN_00055998 (1 位) —— 缓冲耗尽时 (*--src) 重装。               */
static __inline int bleem_br_get1(bleem_bitreader *r) {
    int bit = (int)(r->buf & 1u);
    r->buf >>= 1;
    if (--r->cnt == 0) { r->src--; r->buf = *r->src; r->cnt = 32; }
    return bit;
}

/* 对应 FUN_00055992/0x5598c/0x55986/0x55960 (n=2/3/4/8 位, MSB 先入结果) */
static __inline DWORD bleem_br_getn(bleem_bitreader *r, int n) {
    DWORD acc = 0;
    while (n--) acc = (acc << 1) | (DWORD)bleem_br_get1(r);
    return acc;
}
#define bleem_br_get8(r) bleem_br_getn((r), 8)

#endif /* __cplusplus */

#endif /* BLEEM_H */
