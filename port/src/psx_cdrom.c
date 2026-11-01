/*
 * psx_cdrom.c —— 清洁房战役楔3: 光盘读取器 + 自造 ISO 构建器 (task-130)
 */
#include "psx_cdrom.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static uint32_t rd_le32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static int probe_seek_read(psx_disc_t *d, long byte_off, uint8_t *buf, size_t n)
{
    if (fseek(d->f, byte_off, SEEK_SET) != 0) return -1;
    return fread(buf, 1, n, d->f) == n ? 0 : -1;
}

/* 读一帧并解出 user data 到 dst(2048)。mode 已知。 */
static int frame_data(psx_disc_t *d, uint32_t frame, uint8_t *dst)
{
    if (d->sector_size == 2048)
        return probe_seek_read(d, (long)frame * 2048, dst, 2048);
    if (probe_seek_read(d, (long)frame * 2352, d->buf, 2352) != 0) return -1;
    /* 帧完整性诚实面: sync 前导 0x00 + 10×FF (全零 pregap 帧放行=全零数据) */
    int zeroed = 1, sync_ok = (d->buf[0] == 0x00);
    for (int i = 1; i < 11 && sync_ok; i++) if (d->buf[i] != 0xFF) sync_ok = 0;
    for (int i = 0; i < 2352; i++) if (d->buf[i]) { zeroed = 0; break; }
    if (!sync_ok && !zeroed) return -2;
    memcpy(dst, d->buf + (uint32_t)(d->data_off ? d->data_off : 24), 2048);
    return 0;
}

int psx_disc_read(psx_disc_t *d, uint32_t lba, uint32_t nsec, uint8_t *dst)
{
    if (!d || !d->f) return -1;
    for (uint32_t i = 0; i < nsec; i++)
        if (frame_data(d, d->data_frame0 + lba + i, dst + i * 2048u) != 0) return -2;
    return 0;
}

void psx_disc_close(psx_disc_t *d)
{ if (d) { if (d->f) fclose(d->f); memset(d, 0, sizeof *d); } }

/* ---- CUE 解析 (只取首条数据轨) ---- */
static int cue_parse(const char *cue_path, char *bin_out, size_t cap,
                     int *sector_size, uint32_t *index_frame)
{
    FILE *f = fopen(cue_path, "r");
    if (!f) return -1;
    char line[512];
    char dir[448];
    snprintf(dir, sizeof dir, "%s", cue_path);
    char *slash = strrchr(dir, '/'); if (slash) slash[1] = 0; else strcpy(dir, "./");
    int ss = 0; uint32_t idx = 150; int got = 0;
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncasecmp(p, "FILE ", 5) == 0) {
            char *q = strchr(p + 5, '"');
            if (!q) continue;
            char *e = strchr(q + 1, '"');
            if (!e) continue;
            *e = 0;
            snprintf(bin_out, cap, "%s%s", dir, q + 1);
        } else if (strncasecmp(p, "TRACK ", 6) == 0 && !got) {
            if (strstr(p, "2048")) ss = 2048;
            else if (strstr(p, "2352")) ss = 2352;
        } else if (strncasecmp(p, "INDEX ", 6) == 0 && ss && !got) {
            unsigned n, m, s, fr;
            if (sscanf(p, "INDEX %u %u:%u:%u", &n, &m, &s, &fr) == 4) {
                idx = (m * 60u + s) * 75u + fr;
                got = 1;
            }
        }
    }
    fclose(f);
    if (!got || !ss || !bin_out[0]) return -2;
    *sector_size = ss; *index_frame = idx;
    return 0;
}

/* PVD 候选探针: 在 frame 帧、帧内偏移 off 处验 0x01+'CD001' */
static int pvd_at(psx_disc_t *d, uint32_t frame, int stride, int off)
{
    uint8_t p[2360];
    if (probe_seek_read(d, (long)frame * stride, p, (size_t)(off + 6)) != 0) return 0;
    return p[off] == 0x01 && memcmp(p + off + 1, "CD001", 5) == 0;
}

int psx_disc_open(psx_disc_t *d, const char *path)
{
    memset(d, 0, sizeof *d);
    char bin[512] = "";
    int ss = 0; uint32_t frame0 = 0; int from_cue = 0;
    size_t plen = strlen(path);
    if (plen > 4 && strcasecmp(path + plen - 4, ".cue") == 0) {
        if (cue_parse(path, bin, sizeof bin, &ss, &frame0) != 0) return -1;
        d->f = fopen(bin, "rb");
        from_cue = 1;
    } else {
        d->f = fopen(path, "rb");
    }
    if (!d->f) return -2;
    long sz = (fseek(d->f, 0, SEEK_END), ftell(d->f));
    fseek(d->f, 0, SEEK_SET);
    if (!ss) { /* 裸盘: 尺寸候选 2352 优先(真实盘主流), PVD 探针定案 */
        int cand[2] = { 2352, 2048 };
        for (int i = 0; i < 2 && !d->data_off; i++) {
            int stride = cand[i];
            if (sz % stride != 0) continue;
            if (stride == 2048) {
                for (uint32_t f0 = 0; f0 <= 150; f0 = f0 ? 0 : 150) {
                    if (pvd_at(d, f0 + 16, 2048, 0)) { d->data_frame0 = f0; d->data_off = 0; break; }
                    if (f0) break;
                }
            } else {
                static const int offs[3] = { 24, 16, 32 }; /* 零售 PSX 主流先 */
                for (int k = 0; k < 3 && !d->data_off; k++)
                    for (uint32_t f0 = 0; f0 <= 150; f0 = f0 ? 0 : 150) {
                        if (pvd_at(d, f0 + 16, stride, offs[k])) {
                            d->data_frame0 = f0; d->data_off = offs[k]; break;
                        }
                        if (f0) break;
                    }
            }
        }
        if (!d->data_off) {
            if (sz % 2352 == 0 && sz % 2048 != 0) { ss = 2352; d->data_off = 24; d->data_frame0 = 0; }
            else if (sz % 2048 == 0) { ss = 2048; d->data_off = 0; d->data_frame0 = 0; }
            else { fclose(d->f); memset(d, 0, sizeof *d); return -3; }
        } else ss = d->data_off ? 2352 : 2048;
    } else {
        /* cue 给了 stride/frame0: PVD 探针定 data_off */
        d->data_frame0 = frame0;
        static const int offs[3] = { 24, 16, 32 };
        for (int k = 0; k < 3 && !d->data_off; k++)
            if (pvd_at(d, frame0 + 16, ss, offs[k])) d->data_off = offs[k];
        if (!d->data_off) d->data_off = ss == 2352 ? (from_cue ? 24 : 24) : 0;
    }
    d->sector_size = ss;
    d->mode2 = (d->data_off == 32);
    if (psx_disc_scan_root(d) == 0) d->have_iso = 1;
    return 0;
}

/* ---- ISO9660 ---- */
static int dir_add(psx_disc_t *d, uint32_t lba, uint32_t size, const char *name, int is_dir)
{
    if (d->nfiles >= 1024) return -1;
    cd_file_t *e = &d->files[d->nfiles++];
    e->lba = lba; e->size = size; e->is_dir = (uint8_t)is_dir;
    snprintf(e->name, sizeof e->name, "%s", name);
    return 0;
}
static void norm_name(const uint8_t *raw, int len, char *out, size_t cap)
{
    int j = 0;
    for (int i = 0; i < len && (size_t)j + 1 < cap; i++) {
        char c = (char)toupper(raw[i]);
        if (!c || c == ';') break;
        out[j++] = c;
    }
    out[j] = 0;
}
static int scan_dir(psx_disc_t *d, uint32_t dlba, uint32_t dsize, int depth);

static int scan_dir(psx_disc_t *d, uint32_t dlba, uint32_t dsize, int depth)
{
    uint32_t nsec = (dsize + 2047u) / 2048u;
    if (nsec == 0 || nsec > 4096u) return -1;
    uint8_t *sec = calloc(1, 2048);   /* 每层独立缓冲(递归安全) */
    if (!sec) return -3;
    uint32_t done = 0;
    for (uint32_t s = 0; s < nsec; s++) {
        if (psx_disc_read(d, dlba + s, 1, sec) != 0) return -2;
        uint32_t p = 0;
        while (p < 2048u) {
            uint8_t rl = sec[p];
            if (rl == 0) { p = (p & ~2047u) + 2048u; break; } /* 对齐到扇区界 */
            if (p + rl > 2048u) break;
            uint32_t lba = rd_le32(sec + p + 2);
            uint32_t size = rd_le32(sec + p + 10);
            uint8_t flags = sec[p + 25];
            uint8_t nlen = sec[p + 32];
            char name[64];
            norm_name(sec + p + 33, nlen, name, sizeof name);
            /* '.'(0x00)/'..'(0x01) 伪项过滤 (ECMA: nlen<=2 且裸值<=1) */
            if (!(nlen <= 2 && sec[p + 33] <= 1)) {
                dir_add(d, lba, size, name, (flags & 2) ? 1 : 0);
                if ((flags & 2) && depth < 2 && size <= 512u * 1024u)
                    scan_dir(d, lba, size, depth + 1);
            }
            p += ((uint32_t)rl + 1u) & ~1u;   /* ECMA 9.1: 记录 2B 对齐边界 */
        }
        done += 2048u;
        if (done >= dsize) break;
    }
    free(sec);
    return 0;
}

int psx_disc_scan_root(psx_disc_t *d)
{
    uint8_t pvd[2352];
    if (psx_disc_read(d, 16, 1, pvd) != 0) return -1;
    if (pvd[0] != 0x01 || memcmp(pvd + 1, "CD001", 5) != 0) return -2;
    /* ECMA-119 66.6: 根目录记录 @PVD+0x9C(34B): ext LE@0x9E, size LE@0xA6 */
    d->root_lba = rd_le32(pvd + 0x9E);
    d->root_size = rd_le32(pvd + 0xA6);
    memcpy(d->volume_id, pvd + 40, 32); d->volume_id[32] = 0;
    d->nfiles = 0;
    return scan_dir(d, d->root_lba, d->root_size, 0);
}

int psx_disc_find(psx_disc_t *d, const char *name)
{
    char up[64]; int j = 0;
    for (; name[j] && (size_t)j + 1 < sizeof up; j++) up[j] = (char)toupper(name[j]);
    up[j] = 0;
    char *semi = strchr(up, ';'); if (semi) *semi = 0;
    for (int i = 0; i < d->nfiles; i++)
        if (strcmp(d->files[i].name, up) == 0) return i;
    return -1;
}

/* SYSTEM.CNF BOOT= 解析 (公开 PSX 盘约定; 文本格式见 psx-spx/PCGUIX) */
static const char *casestr(const char *h_, const char *n_)
{
    size_t nl = strlen(n_);
    for (const char *p = h_; *p; p++)
        if (strncasecmp(p, n_, nl) == 0) return p;
    return NULL;
}

/* CNF 行解析: "KEY = value" 宽容空白 (真盘 PsyQ 变体实证 "BOOT = cdrom:\X;1") */
static const char *cnf_value(const char *text, const char *key)
{
    for (const char *p = text; p && *p; ) {
        size_t kl = strlen(key);
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (strncasecmp(p, key, kl) != 0) { p = strchr(p, '\n'); if (!p) return NULL; p++; continue; }
        p += kl;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '=') { p = strchr(p, '\n'); if (!p) return NULL; p++; continue; }
        p++;
        while (*p == ' ' || *p == '\t') p++;
        return p;
    }
    return NULL;
}

int psx_disc_boot_exe(psx_disc_t *d, char *name_out, unsigned cap)
{
    if (!d || !d->have_iso) return -1;
    d->cnf_stack = 0;
    int i = psx_disc_find(d, "SYSTEM.CNF");
    if (i >= 0 && d->files[i].size && d->files[i].size <= 4096u) {
        uint8_t buf[4096 + 1];
        uint32_t nsec = (d->files[i].size + 2047u) / 2048u;
        if (nsec * 2048u <= sizeof buf &&
            psx_disc_read(d, d->files[i].lba, nsec, buf) == 0) {
            buf[d->files[i].size] = 0;
            const char *b = cnf_value((const char *)buf, "BOOT");
            if (!b) b = cnf_value((const char *)buf, "BOOT2");
            const char *st = cnf_value((const char *)buf, "STACK");
            if (st) d->cnf_stack = (uint32_t)strtoul(st, NULL, 16);
            if (b) {
                const char *cd = casestr(b, "cdrom:");
                if (cd == b) b = cd + 6;
                while (*b == '\\' || *b == '/') b++;
                char nm[64]; unsigned j = 0;
                for (; *b && *b != '\r' && *b != '\n' && *b != ';' && j + 1 < sizeof nm; b++, j++)
                    nm[j] = (char)toupper((unsigned char)*b);
                nm[j] = 0;
                char *sl = strrchr(nm, '\\'); if (!sl) sl = strrchr(nm, '/');
                int idx = psx_disc_find(d, sl ? sl + 1 : nm);
                if (idx >= 0) {
                    if (name_out && cap) snprintf(name_out, cap, "%s", d->files[idx].name);
                    return idx;
                }
            }
        }
    }
    i = psx_disc_find(d, "PSX.EXE");
    if (i >= 0 && name_out && cap) snprintf(name_out, cap, "%s", d->files[i].name);
    return i;
}

/* ---------------- 自造 ISO 构建器 ---------------- */
static void put_both32(uint8_t *p, uint32_t v)
{ /* ECMA 双字节序: LE 4B + BE 4B */
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = v >> 24;
    p[4] = v >> 24; p[5] = (v >> 16) & 0xFF; p[6] = (v >> 8) & 0xFF; p[7] = v & 0xFF;
}
/* 帧封装: data_off ∈ {16=真MODE1, 24=零售/MODE1-XA头, 32=MODE2 FORM2} */
static void wrap_frame2(uint8_t *sect2048, uint8_t out2352[2352], uint32_t frame_lba, int data_off)
{
    memset(out2352, 0, 2352);
    out2352[0] = 0x00;
    for (int i = 1; i < 11; i++) out2352[i] = 0xFF;
    out2352[11] = (uint8_t)(data_off == 16 ? 0x01 : (data_off == 32 ? 0x02 : 0x00));
    if (data_off == 16) {                    /* 真 MODE1/2352: 12 sync + 4 LBA(BE) */
        out2352[12] = (uint8_t)(frame_lba >> 24); out2352[13] = (uint8_t)(frame_lba >> 16);
        out2352[14] = (uint8_t)(frame_lba >> 8);  out2352[15] = (uint8_t)frame_lba;
    } else {                                 /* @24 零售头型 / @32 XA: MSF BCD + 模式字 */
        int32_t l = (int32_t)frame_lba - 150;
        uint32_t m = (uint32_t)((l + 150) / 75 / 60), sp = (uint32_t)((l + 150) / 75 % 60), fr = (uint32_t)((l + 150) % 75);
        out2352[12] = (uint8_t)(((m / 10) << 4) | (m % 10));
        out2352[13] = (uint8_t)(((sp / 10) << 4) | (sp % 10));
        out2352[14] = (uint8_t)(((fr / 10) << 4) | (fr % 10));
        out2352[15] = data_off == 32 ? 0x02 : 0x01;
        if (data_off == 32) { out2352[16] = 0x00; out2352[17] = 0x00;
                              out2352[18] = 0x01; out2352[19] = 0x00; }
    }
    memcpy(out2352 + (data_off ? data_off : 24), sect2048, 2048);
}

/* 布局: [150 pregap 可选] LBA0-15 零, 16 PVD, 17 SVD, 18 terminator,
 *       19 根目录, [20 SYSTEM.CNF 若 cnf_text], FILE_LBA=(cnf?21:20).. exe */
int psx_iso_build2(const char *out_path, const uint8_t *exe_data, uint32_t n,
                   const char *exe_name, int stride, int data_off,
                   int pregap150, const char *cnf_text)
{
    int has_cnf = cnf_text && cnf_text[0];
    const uint32_t ROOT_LBA = 19, CNF_LBA = 20, FILE_LBA = has_cnf ? 21 : 20;
    uint32_t cnf_len = has_cnf ? (uint32_t)strlen(cnf_text) : 0u;
    uint32_t exe_sect = (n + 2047u) / 2048u;
    uint32_t base = (stride == 2352 && pregap150) ? 150u : 0u;
    uint32_t total_lba = FILE_LBA + exe_sect;
    FILE *f = fopen(out_path, "wb");
    if (!f) return -1;
    uint8_t z2048[2048]; memset(z2048, 0, sizeof z2048);
    uint8_t w2352[2352];
    for (uint32_t i = 0; i < total_lba + base; i++) {
        uint32_t lba = i >= base ? i - base : 0u;
        uint8_t *sp = z2048;
        static uint8_t sect[2048];
        memset(sect, 0, sizeof sect);
        if (i < base) {                       /* pregap 空帧 */
            if (stride == 2352) { wrap_frame2(z2048, w2352, i, data_off); fwrite(w2352, 1, 2352, f); }
            else fwrite(z2048, 1, 2048, f);
            continue;
        }
        if (lba == 16 || lba == 17) {         /* PVD / SVD */
            sect[0] = lba == 16 ? 1 : 2; memcpy(sect + 1, "CD001", 5); sect[6] = 1;
            memcpy(sect + 40, "LINBLEE HBTEST  ", 16);
            put_both32(sect + 0x50, total_lba);
            put_both32(sect + 0x58, total_lba);
            sect[0x7C] = 0x01; sect[0x7E] = 0x01;
            sect[0x80] = 0x01; sect[0x82] = 0x01;
            put_both32(sect + 0x82, 2048);
            sect[0x9C] = 34; sect[0x9D] = 0;
            put_both32(sect + 0x9E, ROOT_LBA);
            put_both32(sect + 0xA6, 2048);
            sect[0x9C + 25] = 0x02;
            sect[0x9C + 32] = 1; sect[0x9C + 33] = 0x00;
            sp = sect;
        } else if (lba == 18) {               /* 终止符 */
            sect[0] = 0xFF; memcpy(sect + 1, "CD001", 5); sect[6] = 1;
            sp = sect;
        } else if (lba == ROOT_LBA) {         /* 根目录: . .. [CNF] EXE */
            uint32_t q = 0;
            sect[q + 0] = 34; put_both32(sect + q + 2, ROOT_LBA); put_both32(sect + q + 10, 2048);
            sect[q + 25] = 0x02; sect[q + 32] = 1; sect[q + 33] = 0x00;
            q += 34;
            sect[q + 0] = 34; put_both32(sect + q + 2, ROOT_LBA); put_both32(sect + q + 10, 2048);
            sect[q + 25] = 0x02; sect[q + 32] = 1; sect[q + 33] = 0x01;
            q += 34;
            if (has_cnf) {
                const char *cn = "SYSTEM.CNF;1"; size_t nl = strlen(cn);
                uint32_t rec = 33u + (uint32_t)nl;
                sect[q + 0] = (uint8_t)rec; put_both32(sect + q + 2, CNF_LBA); put_both32(sect + q + 10, cnf_len);
                sect[q + 25] = 0x00; sect[q + 32] = (uint8_t)nl;
                memcpy(sect + q + 33, cn, nl);
                q += (rec + 1u) & ~1u;
            }
            char xn[48]; snprintf(xn, sizeof xn, "%s;1", exe_name);
            uint32_t rec = 33u + (uint32_t)strlen(xn);
            sect[q + 0] = (uint8_t)rec; put_both32(sect + q + 2, FILE_LBA); put_both32(sect + q + 10, n);
            sect[q + 25] = 0x00; sect[q + 32] = (uint8_t)strlen(xn);
            memcpy(sect + q + 33, xn, strlen(xn));
            q += (rec + 1u) & ~1u;
            sp = sect;
        } else if (has_cnf && lba == CNF_LBA) {
            memcpy(sect, cnf_text, cnf_len); sp = sect;
        } else if (lba >= FILE_LBA) {
            uint32_t off = (lba - FILE_LBA) * 2048u;
            uint32_t len = off < n ? (n - off < 2048u ? n - off : 2048u) : 0;
            memcpy(sect, exe_data + off, len); sp = sect;
        }
        if (stride == 2352) { wrap_frame2(sp, w2352, i, data_off); fwrite(w2352, 1, 2352, f); }
        else fwrite(sp, 1, 2048, f);
    }
    fclose(f);
    if (stride == 2352) {
        char cue[600]; snprintf(cue, sizeof cue, "%s.cue", out_path);
        FILE *c = fopen(cue, "w");
        if (c) { fprintf(c, "FILE \"%s\" BINARY\nTRACK 01 MODE%d/2352\nINDEX 01 00:02:00\n",
                         strrchr(out_path, '/') ? strrchr(out_path, '/') + 1 : out_path,
                         data_off == 32 ? 2 : 1); fclose(c); }
    }
    return 0;
}

int psx_iso_build(const char *out_path, const uint8_t *exe_data, uint32_t n,
                  const char *exe_name, int cue_mode)
{   /* 兼容薄壳: 0=裸2048, 1=MODE1/2352(pregap, data@24), 2=MODE2/2352(@32) */
    switch (cue_mode) {
    case 0:  return psx_iso_build2(out_path, exe_data, n, exe_name, 2048, 0, 0, NULL);
    case 2:  return psx_iso_build2(out_path, exe_data, n, exe_name, 2352, 32, 1, NULL);
    default: return psx_iso_build2(out_path, exe_data, n, exe_name, 2352, 24, 1, NULL);
    }
}

/* ---------------- --cd-selftest ---------------- */
#define CDCHK(cond, name) do { printf("CD_TEST %s: %s\n", name, (cond) ? "PASS" : "FAIL"); \
    if (!(cond)) bad++; } while (0)

void psx_cd_synth_exe_full(uint8_t *img, uint32_t n, const uint32_t *code, uint32_t code_bytes)
{   /* 真 Psy-Q PSX-EXE 布局 (task-134 四枚零售盘交叉钉字):
     *   "PS-X EXE" | pc@0x10 | gp@0x14 | 数据块 vaddr@0x18 | 块长@0x1C | SP基@0x30;
     *   块= [0x100 data][code] 连续, 文件偏移 0x800 起; 入口 = vaddr+0x100 */
    memset(img, 0, n);
    memcpy(img, "PS-X EXE", 8);
    uint32_t *h = (uint32_t *)img;
    h[0x10 / 4] = 0x80040100u;               /* initial pc = 代码入口 */
    h[0x14 / 4] = 0x80040000u;               /* initial gp */
    h[0x18 / 4] = 0x80040000u;               /* 数据块 vaddr */
    h[0x1C / 4] = 0x100u + code_bytes;       /* 数据块总长 */
    h[0x30 / 4] = 0x80200000u;               /* initial SP base (真机同款) */
    uint32_t *dat = (uint32_t *)(img + 0x800);
    for (int i = 0; i < 0x100 / 4; i++) dat[i] = 0xABCD0000u + (uint32_t)i;
    if (code && code_bytes) memcpy(img + 0x800 + 0x100, code, code_bytes);
}
void psx_cd_synth_exe(uint8_t *img, uint32_t n)   /* 默认夹具: addi $2=0x333; jr $ra */
{
    uint32_t ci[2] = { (8u << 26) | (2u << 16) | 0x333u, 0x03E00008u };
    psx_cd_synth_exe_full(img, n, ci, sizeof ci);
}

int cmd_cd_selftest(int argc, char **argv)
{
    (void)argc; (void)argv;
    int bad = 0;
    char path[256];
    snprintf(path, sizeof path, "%s", "/tmp/hb_cd_selftest.iso");
    uint32_t n = 0xA00u; uint8_t *img = calloc(1, n);
    psx_cd_synth_exe(img, n);

    /* T1 三模式构建→开盘→找文件→读 extent 逐字节 */
    const int modes[3] = { 0, 1, 2 };
    const char *exts[3] = { ".iso", "_m1.bin", "_m2.bin" };
    for (int m = 0; m < 3; m++) {
        char p[288]; snprintf(p, sizeof p, "/tmp/hb_cd_selftest%s", exts[m]);
        int r = psx_iso_build(p, img, n, "PSX.EXE", modes[m]);
        psx_disc_t d; char openp[300];
        snprintf(openp, sizeof openp, "%s", p);
        if (modes[m] != 0) { /* 走 .cue 装载 */ strcat(openp, ".cue"); }
        r = r | (r != 0 ? 0 : psx_disc_open(&d, openp));
        int idx = r == 0 ? psx_disc_find(&d, "psx.exe;1") : -1;
        int rdok = -1;
        if (idx >= 0) {
            uint32_t rsec = (d.files[idx].size + 2047u) / 2048u;
            uint8_t *back = calloc(1, rsec * 2048u);
            rdok = psx_disc_read(&d, d.files[idx].lba, rsec, back);
            rdok = (rdok == 0 && d.files[idx].size == n && memcmp(back, img, n) == 0) ? 0 : -2;
            free(back);
        }
        char nm[64]; snprintf(nm, sizeof nm, "t1_mode%d_open_find_read", modes[m]);
        CDCHK(r == 0 && idx >= 0 && d.have_iso == 1 && rdok == 0 &&
              strcmp(d.volume_id, "LINBLEE HBTEST  ") == 0, nm);
        psx_disc_close(&d);
    }
    /* T2 寄存器面集成: SetLoc/ReadN 经 psx_dev 消费帧缓冲 */
    {
        extern int psx_cd_dev_integration_probe(void); /* psx_dev.c */
        int pb = psx_cd_dev_integration_probe();
        printf("CD_EV probe_bad=%d\n", pb);
        CDCHK(pb == 0, "t2_dev_cmd_statemachine");
    }
    /* T3 未找到的诚实面 */
    {
        psx_disc_t d;
        if (psx_disc_open(&d, "/tmp/hb_cd_selftest_m1.bin.cue") == 0) {
            CDCHK(psx_disc_find(&d, "NOPE.EXE") == -1, "t3_missing_file");
            psx_disc_close(&d);
        } else CDCHK(0, "t3_missing_file");
    }
    /* T4 PBD 面: HBEXE 头字段回读 (供 task-131 消费) */
    {
        psx_disc_t d; uint8_t hdr[2048];
        int ok = psx_disc_open(&d, path);
        if (ok == 0) {
            int idx = psx_disc_find(&d, "PSX.EXE");
            ok = idx >= 0 ? psx_disc_read(&d, d.files[idx].lba, 1, hdr) : -1;
        }
        uint32_t pc = 0, gp = 0;
        memcpy(&pc, hdr + 0x10, 4); memcpy(&gp, hdr + 0x14, 4);
        CDCHK(ok == 0 && pc == 0x80040100u && gp == 0x80040000u, "t4_hbexe_header");
        psx_disc_close(&d);
    }
    /* T5 零售头型 2352/data@24/无pregap + SYSTEM.CNF BOOT= 解析 (全自造字节) */
    {
        uint8_t *im = calloc(1, 0xA00); psx_cd_synth_exe(im, 0xA00);
        int rb = psx_iso_build2("/tmp/hb_retail24.bin", im, 0xA00, "HB_000.01",
                                2352, 24, 0, "BOOT = cdrom:\\HB_000.01;1\r\nSTACK = 801FFF00\r\n");
        psx_disc_t d; char nm[64] = ""; 
        int ok = rb == 0 && psx_disc_open(&d, "/tmp/hb_retail24.bin") == 0;
        int bi = ok ? psx_disc_boot_exe(&d, nm, sizeof nm) : -1;
        uint8_t hdr[2048]; uint32_t pc = 0;
        if (bi >= 0 && psx_disc_read(&d, d.files[bi].lba, 1, hdr) == 0) memcpy(&pc, hdr + 0x10, 4);
        CDCHK(ok && d.data_off == 24 && d.data_frame0 == 0 && bi >= 0 &&
              strcmp(nm, "HB_000.01") == 0 && pc == 0x80040100u, "t5_retail24_boot_via_cnfcnf");
        if (ok) psx_disc_close(&d);
        free(im);
    }
    /* T6 真 MODE1/2352 data@16 头型探针 */
    {
        uint8_t *im = calloc(1, 0xA00); psx_cd_synth_exe(im, 0xA00);
        int rb = psx_iso_build2("/tmp/hb_retail16.bin", im, 0xA00, "HB_000.02",
                                2352, 16, 0, "BOOT=CDROM:\\HB_000.02;1\n");
        psx_disc_t d; char nm[64] = "";
        int ok = rb == 0 && psx_disc_open(&d, "/tmp/hb_retail16.bin") == 0;
        int bi = ok ? psx_disc_boot_exe(&d, nm, sizeof nm) : -1;
        CDCHK(ok && d.data_off == 16 && bi >= 0 && strcmp(nm, "HB_000.02") == 0, "t6_mode1_16_probe");
        if (ok) psx_disc_close(&d);
        free(im);
    }
    /* T7 2352 + pregap150 + .cue 装载面 (data_frame0=150 判定) */
    {
        uint8_t *im = calloc(1, 0xA00); psx_cd_synth_exe(im, 0xA00);
        int rb = psx_iso_build2("/tmp/hb_retailpg.bin", im, 0xA00, "HB_000.03",
                                2352, 24, 1, "boot=cdrom:\\HB_000.03;1\n");
        psx_disc_t d; char nm[64] = "";
        int ok = rb == 0 && psx_disc_open(&d, "/tmp/hb_retailpg.bin.cue") == 0;
        int bi = ok ? psx_disc_boot_exe(&d, nm, sizeof nm) : -1;
        CDCHK(ok && d.data_off == 24 && d.data_frame0 == 150 && bi >= 0 &&
              strcmp(nm, "HB_000.03") == 0, "t7_pregap_cue_open");
        if (ok) psx_disc_close(&d);
        free(im);
    }
    /* T8 无 CNF 回落 PSX.EXE (t1 老盘面) */
    {
        psx_disc_t d; char nm[64] = "";
        int ok = psx_disc_open(&d, "/tmp/hb_cd_selftest_m1.bin") == 0;
        int bi = ok ? psx_disc_boot_exe(&d, nm, sizeof nm) : -1;
        CDCHK(ok && bi >= 0 && strcmp(nm, "PSX.EXE") == 0, "t8_no_cnf_fallback_psxexe");
        if (ok) psx_disc_close(&d);
    }
    free(img);
    printf("CD_EV bad=%d\n", bad);
    if (bad) { printf("CD SELFTEST FAIL (%d)\n", bad); return 1; }
    printf("CD CORE SELFTEST PASS\n");
    return 0;
}
