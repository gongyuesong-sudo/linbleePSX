/*
 * bleem_native.h —— 原生移植公共定义 (demo-v1_4.EXE 逆向知识的 Linux 表达)
 *
 * 来源: /home/gill/bleem/rewrite (REA/Ghidra 12.1.4 逆向重建, Evidence 台账
 * docs/evidence_ledger.md)。本文件只含**静态可恢复**的常量与语义。
 *
 * 移植策略 (用户裁定):
 *   NATIVE-ONLY  —— 不运行 Wine, 不执行任何原始 Windows/x86-32 代码
 *   FULL-FEATURE —— 端口不继承 demo 功能限制, 一律按完整版语义
 */
#ifndef BLEEM_NATIVE_H
#define BLEEM_NATIVE_H

#include <stddef.h>
#include <stdint.h>

/* ---- 原文件布局 (证据: PE 头解析 + REA binary_overview) ---- */
#define BLEEM_APPEND_START   0xFA00ull    /* 追加 payload 起始文件偏移     */
#define BLEEM_APPEND_SIZE    287732ull    /* 追加区字节数                  */
#define BLEEM_STUB_OFF       0x55338ull   /* 明文运行时桩文件偏移           */
#define BLEEM_STUB_END       0x55DF4ull   /* 桩结束(=EOF)                   */
#define BLEEM_MAIN_ENT_OFF   0xFA60ull    /* 桩调用的 *(view+64000) 目标;
                                             位于加密区, 原生侧不可达       */
#define BLEEM_RAND_SEED      0x52414E44u  /* "RAND" 完整性种子              */
#define BLEEM_API_TABLE_OFF  0x4022A4u    /* 原桩 API 派发表偏移(仅存档)    */
#define BLEEM_MODULE_COUNT   5            /* 原桩模块槽位数                 */
#define BLEEM_DESC_STRIDE    0x10         /* 块描述符条目步长               */
#define BLEEM_STUB_MAGIC     0x000005B9u  /* 桩首 dword (观察: b9 05 00 00) */

/* ---- 错误码语义 (原错误表 0x4011C4[1..6]; 端口重映射为原生含义) ---- */
enum bleem_err {
    BLEEM_OK = 0,
    BLEEM_ERR_GENERIC,        /* 原1: Error loading program!        */
    BLEEM_ERR_PLATFORM,       /* 原2: Win95/98 门 —— 端口替换为
                                 "Linux/X11|Wayland/Vulkan 支持检查" */
    BLEEM_ERR_NOMEM,
    BLEEM_ERR_CORRUPT,        /* 原4: reinstall 语义 -> 数据完整性   */
    BLEEM_ERR_SYSDEBUGGER,    /* 原5/6: 端口按用户裁定**移除**该等
                                 限制(反调试属 demo 保护的一部分),
                                 仅作协议兼容占位, 永不触发          */
    BLEEM_ERR_DEBUGGER,
};

/* ---- 完整版功能集 (readme.txt 声明 demo 禁用; 端口全部开放) ---- */
typedef struct {
    int sound;             /* 1 = 全音效/音乐 (原 demo: 禁用)         */
    int dynamic_sound;     /* 1 = 动态音源                            */
    int memcard;           /* 1 = 记忆卡 (原 demo: 禁用)              */
    int gpu_accel;         /* 1 = 硬件加速 (原 Direct3D -> Vulkan)    */
    int color_fmv;         /* 1 = 彩色电影 (原 demo: 仅黑白)          */
    int cpu_ext;           /* 1 = 现代 CPU 扩展 (原 MMX/3DNow 禁用)   */
    int time_limit_sec;    /* 0 = 无时限 (原 demo: 限时自动退出)      */
} bleem_features_t;

static inline bleem_features_t bleem_features_full(void)
{
    bleem_features_t f = { 1, 1, 1, 1, 1, 1, 0 };
    return f;
}

/* ---- 原生"引导器"对象 (替代原 Win9x 自映射 CreateFileMapping 链) ---- */
typedef struct {
    const char *path;
    uint8_t    *map;       /* mmap(PROT_READ, MAP_PRIVATE) 自身镜像    */
    size_t      len;
    int         ok_layout; /* 布局校验通过                             */
    uint32_t    rand_fold_stub; /* 明文桩区的 RAND 折叠(诊断值)        */
    bleem_features_t feat;
} bleem_image_t;

int  bleem_boot_open(bleem_image_t *img, const char *path);
int  bleem_boot_verify(bleem_image_t *img);   /* 返回 bleem_err        */
void bleem_boot_close(bleem_image_t *img);

/* "RAND" XOR 折叠 (原 FUN_000553ca 内层循环, 逐 dword) */
uint32_t bleem_rand_fold(const uint8_t *p, size_t nbytes, uint32_t start_rva);

/* ---- payload 格式引擎 (task-005) ---- */
typedef struct bleem_lz {
    const uint32_t *src; uint32_t buf; int cnt;
} bleem_bitreader;
void bleem_br_init(bleem_bitreader *r, const uint32_t *dwords);
int  bleem_br_get1(bleem_bitreader *r);
uint32_t bleem_br_getn(bleem_bitreader *r, int n);

/* CLI 用 */
int cmd_selftest(int argc, char **argv);      /* vk_host.c  (task-003) */
int cmd_render_ui(int argc, char **argv);     /* resources.c(task-004) */
int cmd_window(int argc, char **argv);        /* resources.c 窗口模式  */
int cmd_payload_selftest(int argc, char **argv); /* lzfmt.c  (task-005)*/

#endif
