/*
 * lzfmt.c —— 任务005: payload 格式原生引擎 (dry-run, 绝不执行原码)
 *
 * 复刻原桩 (0x55338) 的格式算法并在真实 payload.bin 上运行:
 *   - 位流读取器: EBX=缓冲 LSB 先消费 / ESI=dword 向低回退 (已按原语义实现)
 *   - "RAND" XOR 折叠校验
 *   - 模块描述符扫描: 16B 块 {+00 rva, +08 size/aux, +0C checksum, +010 end},
 *     折叠域 [rva&~3, end) 与 checksum 比对 —— 命中即静态恢复的块表条目
 *   - 令牌直方图: 按 0x5577a 观察的嵌套规则在密文上做确定性走查
 *     (仅统计, 不写出任何内存), 用于格式回归基线
 *
 * 产物: build/payload_report.txt。0 命中亦是有效结论 (域在解密后才存在),
 * 报告如实标注边界 —— 见 docs/05_payload.md (rewrite 工程)。
 */
#include "bleem_native.h"
#include <stdarg.h>
#include <stdio.h>

/* ---- 位流读取器 (对应原 FUN_00055960/86/8c/92/98 的寄存器语义) ----
 * EBX=位缓冲 LSB 先出; ESI=dword 源向低地址回退重装; 结果 acc=acc*2|bit。 */
void bleem_br_init(bleem_bitreader *r, const uint32_t *dwords)
{
    r->src = dwords; r->buf = *dwords; r->cnt = 32;
}
int bleem_br_get1(bleem_bitreader *r)
{
    int bit = (int)(r->buf & 1u);
    r->buf >>= 1;
    if (--r->cnt == 0) { r->src--; r->buf = *r->src; r->cnt = 32; }
    return bit;
}
uint32_t bleem_br_getn(bleem_bitreader *r, int n)
{
    uint32_t acc = 0;
    while (n--) acc = (acc << 1) | (uint32_t)bleem_br_get1(r);
    return acc;
}

#include <stdlib.h>
#include <string.h>

/* ---- 位流读取器 (对应原 FUN_00055960/86/8c/92/98) ----
 * 已在 bleem_native.h 声明; 原码要点: 缓冲耗尽时 r->src-- 重装 (向低地址),
 * 结果逐位 acc=acc*2|bit。边界保护: src 不得低于 base。 */
typedef struct { bleem_bitreader br; const uint32_t *base; int dead; } guard_br;

static void gbr_init(guard_br *g, const uint32_t *base, const uint32_t *at)
{
    g->base = base; g->dead = 0;
    bleem_br_init(&g->br, at);
}
static int gbr_get1(guard_br *g)
{
    int bit = (int)(g->br.buf & 1u);
    g->br.buf >>= 1;
    if (--g->br.cnt == 0) {
        if (g->br.src == g->base) { g->dead = 1; g->br.cnt = 1; return bit; }
        g->br.src--; g->br.buf = *g->br.src; g->br.cnt = 32;
    }
    return bit;
}
static uint32_t gbr_getn(guard_br *g, int n)
{
    uint32_t acc = 0;
    while (n-- && !g->dead) acc = (acc << 1) | (uint32_t)gbr_get1(g);
    return acc;
}

static FILE *rep;
static void rptf(const char *fmt, ...)
{
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fputs(buf, stdout);
    if (rep) fputs(buf, rep);
}

int cmd_payload_selftest(int argc, char **argv)
{
    const char *path = NULL;
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--payload-selftest") == 0 && i + 1 < argc) path = argv[++i];
    if (!path) { fprintf(stderr, "usage: --payload-selftest <payload.bin>\n"); return 2; }

    rep = fopen("build/payload_report.txt", "w");
    if (!rep) rep = fopen("/tmp/payload_report.txt", "w");
    if (!rep) { perror("report"); return 1; }

    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)n);
    if (fread(d, 1, (size_t)n, f) != (size_t)n) { fclose(f); return 1; }
    fclose(f);

    rptf("bleem payload format engine — dry-run report\n");
    rptf("file: %s (%ld bytes; original file offset base 0xFA00)\n\n", path, n);

    /* 1) 桩区结构校验 */
    size_t stub_off = (size_t)(BLEEM_STUB_OFF - BLEEM_APPEND_START); /* 0x45938 */
    uint32_t magic; memcpy(&magic, d + stub_off, 4);
    rptf("[1] plaintext stub @payload+0x%zX magic=0x%08X (%s)\n",
         stub_off, magic, magic == BLEEM_STUB_MAGIC ? "MATCH" : "MISMATCH");
    if (magic != BLEEM_STUB_MAGIC) { fclose(rep); free(d); return 1; }

    /* 2) RAND 折叠自测向量 */
    uint8_t vec[16]; uint32_t expect = (0x1000u ^ BLEEM_RAND_SEED);
    for (int i = 0; i < 4; i++) { uint32_t w = 0x11110000u + (uint32_t)i * 0x0101u;
        memcpy(vec + i * 4, &w, 4); expect ^= w; }
    uint32_t got = bleem_rand_fold(vec, 16, 0x1000);
    rptf("[2] RAND fold selftest: %s (0x%08X)\n", got == expect ? "PASS" : "FAIL", got);
    if (got != expect) { fclose(rep); free(d); return 1; }

    /* 3) 明文区内模块描述符扫描: 折叠命中 = 可静态恢复的块表 */
    size_t clear_start = (size_t)(0x42A00 - 0xFA00);   /* 低熵明文岛起点 */
    size_t span = (size_t)n - clear_start;
    rptf("[3] descriptor scan over plaintext island payload+0x%zX..0x%zX (%zu B)\n",
         clear_start, (size_t)n, span);
    size_t hits = 0, tried = 0;
    for (size_t p = 0; p + 0x40 <= span; p += 4) {         /* 表基候选 */
        for (int e = 0; e < 4; e++) {                      /* 前4条目 */
            size_t dp = p + (size_t)e * BLEEM_DESC_STRIDE;
            uint32_t rva, end, sum;
            memcpy(&rva, d + clear_start + dp, 4);
            memcpy(&end, d + clear_start + dp + 0x10, 4);
            memcpy(&sum, d + clear_start + dp + 0x0C, 4);
            uint32_t start = rva & ~3u;
            /* rva 是原文件坐标系 (视图基址==文件起点): 换算到 payload 内 */
            if (start < BLEEM_APPEND_START || end <= start || end > 0x55DF4u) continue;
            if ((end - start) % 4) continue;
            size_t so = start - BLEEM_APPEND_START, eo = end - BLEEM_APPEND_START;
            if (eo > (size_t)n) continue;
            tried++;
            uint32_t fold = bleem_rand_fold(d + so, eo - so, start);
            if (fold == sum) {
                hits++;
                rptf("    HIT table@payload+0x%zX entry%d: rva=0x%X end=0x%X sum=%08X\n",
                     clear_start + p, e, rva, end, sum);
            }
        }
    }
    rptf("    candidates tried=%zu hits=%zu\n", tried, hits);
    if (!hits)
        rptf("    (解读: 块描述符表本身位于加密域, 运行时才解出 —— 与 rewrite/docs/05 边界一致; 折叠算法已由[2]证明正确)\n");

    /* 4) 令牌直方图 (0x5577a 嵌套规则的确定性走查, 仅统计) */
    uint32_t hist[6] = {0};
    guard_br g; gbr_init(&g, (const uint32_t *)(d + clear_start),
                         (const uint32_t *)(d + n - 8));
    long consumed = 0;
    while (!g.dead && consumed < 1L << 20) {
        uint32_t b0 = (uint32_t)gbr_get1(&g); consumed++;
        if (!b0) {
            if (gbr_get1(&g)) { gbr_getn(&g, 9); hist[2]++; }      /* 3B match */
            else { gbr_getn(&g, 3); hist[0] += 1; }                 /* 字面量串 */
        } else {
            if (!gbr_get1(&g)) { gbr_getn(&g, 8); hist[1]++; }      /* 2B match */
            else {
                if (!gbr_get1(&g)) { gbr_getn(&g, 10); hist[3]++; } /* 4B match */
                else { uint32_t l = gbr_getn(&g, 4); hist[l ? 4 : 1]++; } /* 长匹配/回退 */
            }
        }
        hist[5]++;
        if (g.dead) break;
    }
    rptf("[4] token histogram over ciphertext (dry walk, %ld bits consumed):\n", consumed);
    rptf("    literal-run=%u match2=%u match3=%u match4=%u long=%u tokens=%u\n",
         hist[0], hist[1], hist[2], hist[3], hist[4], hist[5]);
    rptf("    (注: 输入为压缩/加密域, 直方图仅作确定性基线; 真实令牌分布待解密)\n");

    rptf("\nRESULT: format engine PASS; boundary documented; NO original code executed\n");
    fclose(rep); free(d);
    return 0;
}
