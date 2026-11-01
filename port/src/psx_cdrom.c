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
    uint32_t off = d->mode2 ? 32u : 24u;
    memcpy(dst, d->buf + off, 2048);
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

int psx_disc_open(psx_disc_t *d, const char *path)
{
    memset(d, 0, sizeof *d);
    char bin[512] = "";
    int ss = 0; uint32_t frame0 = 0;
    size_t plen = strlen(path);
    if (plen > 4 && strcasecmp(path + plen - 4, ".cue") == 0) {
        if (cue_parse(path, bin, sizeof bin, &ss, &frame0) != 0) return -1;
        d->f = fopen(bin, "rb");
    } else {
        d->f = fopen(path, "rb");
    }
    if (!d->f) return -2;
    if (!ss) { /* 裸盘: 尺寸判型 */
        long sz = (fseek(d->f, 0, SEEK_END), ftell(d->f));
        if (sz % 2352 == 0 && sz % 2048 != 0) ss = 2352;
        else if (sz % 2048 == 0) ss = 2048;
        else { fclose(d->f); memset(d, 0, sizeof *d); return -3; }
        /* PVD 探针定 data_frame0: CD001 @ (16 或 166)*ss */
        uint8_t probe[2352];
        long cand[2] = { (long)(16 * ss), (long)(166 * ss) };
        int found = -1;
        for (int i = 0; i < 2; i++) {
            if (cand[1] && (probe_seek_read(d, cand[i], probe, (size_t)ss) == 0) &&
                probe[0] == 0x01 && memcmp(probe + 1, "CD001", 5) == 0) { found = i; break; }
        }
        frame0 = found == 1 ? 150u : (found == 0 ? 0u : 150u);
    }
    d->sector_size = ss;
    d->mode2 = 0;
    if (ss == 2352 && frame0) { /* 用 PVD 帧的 mode 字节定 MODE1/MODE2 */
        uint8_t probe[2352];
        if (probe_seek_read(d, (long)(frame0 + 16) * 2352 + 15, probe, 1) == 0 && probe[0] == 0x02)
            d->mode2 = 1;
    }
    d->data_frame0 = frame0;
    /* MODE2 帧若 FORM2 (data@28) 与 FORM1(@32) 差异: v1 统一按 32 (PSX 盘=FORM1) */
    if (psx_disc_scan_root(d) == 0) d->have_iso = 1;
    return 0;
}

/* ---- ISO9660 ---- */
static int dir_add(psx_disc_t *d, uint32_t lba, uint32_t size, const char *name, int is_dir)
{
    if (d->nfiles >= 64) return -1;
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
    if (nsec == 0 || nsec > 256u) return -1;
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
            if (!(nlen <= 2 && sec[p + 33] <= 1))
                dir_add(d, lba, size, name, (flags & 2) ? 1 : 0);
            if ((flags & 2) && depth < 2 && size <= 512u * 1024u)
                scan_dir(d, lba, size, depth + 1);
            p += rl;
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
    /* ECMA-119: 根目录记录 0x86..0xA9: +2 ext LE@0x88, +10 size LE@0x90 */
    d->root_lba = rd_le32(pvd + 0x88);
    d->root_size = rd_le32(pvd + 0x90);
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

/* ---------------- 自造 ISO 构建器 ---------------- */
static void put_both32(uint8_t *p, uint32_t v)
{ /* ECMA 双字节序: LE 4B + BE 4B */
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = v >> 24;
    p[4] = v >> 24; p[5] = (v >> 16) & 0xFF; p[6] = (v >> 8) & 0xFF; p[7] = v & 0xFF;
}
static void wrap_frame(uint8_t *sect2048, uint8_t out2352[2352], uint32_t frame_lba, int mode2)
{
    memset(out2352, 0, 2352);
    out2352[0] = 0x00;
    for (int i = 1; i < 11; i++) out2352[i] = 0xFF;
    out2352[11] = 0x00;
    int32_t l = (int32_t)frame_lba - 150;
    uint32_t m = (uint32_t)((l + 150) / 75 / 60), s = (uint32_t)((l + 150) / 75 % 60), fr = (uint32_t)((l + 150) % 75);
    out2352[12] = (uint8_t)(((m / 10) << 4) | (m % 10));
    out2352[13] = (uint8_t)(((s / 10) << 4) | (s % 10));
    out2352[14] = (uint8_t)(((fr / 10) << 4) | (fr % 10));
    out2352[15] = mode2 ? 0x02 : 0x01;
    if (mode2) {
        out2352[16] = 0x00; out2352[17] = 0x00; /* 文件号/通道(复制模式) */
        out2352[18] = 0x01; out2352[19] = 0x00; /* 扇区号(LE2) */
        out2352[20] = 0x00; out2352[21] = 0x00; out2352[22] = 0x00; out2352[23] = 0x00;
        memcpy(out2352 + 32, sect2048, 2048);
    } else {
        memcpy(out2352 + 24, sect2048, 2048);
    }
}

int psx_iso_build(const char *out_path, const uint8_t *exe_data, uint32_t n,
                  const char *exe_name, int cue_mode)
{
    /* 布局: [150 pregap 2352 模式] LBA0-15 零, 16 PVD, 17 SVD, 18 terminator,
     *       19 根目录(1 扇区), 20.. exe 数据 */
    const uint32_t ROOT_LBA = 19, FILE_LBA = 20;
    uint32_t exe_sect = (n + 2047u) / 2048u;
    int ss = (cue_mode == 0) ? 2048 : 2352;
    uint32_t base = ss == 2352 ? 150u : 0u;   /* data_frame0 偏移 */
    uint32_t total_lba = FILE_LBA + exe_sect;
    FILE *f = fopen(out_path, "wb");
    if (!f) return -1;
    uint8_t z2048[2048]; memset(z2048, 0, sizeof z2048);
    uint8_t w2352[2352];
    uint32_t frame = 0;
    int mode2 = (cue_mode == 2);
    for (uint32_t i = 0; i < total_lba + base; i++, frame++) {
        uint32_t lba = i >= base ? i - base : 0u;
        uint8_t *s = z2048;
        static uint8_t sect[2048];
        memset(sect, 0, sizeof sect);
        if (i < base) { /* pregap 空帧 */
            if (ss == 2352) { wrap_frame(z2048, w2352, i, mode2);
                              fwrite(w2352, 1, 2352, f); }
            continue;
        }
        if (lba == 16 || lba == 17) {   /* PVD / SVD */
            sect[0] = lba == 16 ? 1 : 2; memcpy(sect + 1, "CD001", 5); sect[6] = 1;
            /* volume id / space size (both-byte 0x50) */
            memcpy(sect + 40, "LINBLEE HBTEST  ", 16);
            put_both32(sect + 0x50, total_lba);
            put_both32(sect + 0x58, total_lba);
            sect[0x7C] = 0x01; sect[0x7E] = 0x01; /* v/s vs */
            sect[0x80] = 0x01; sect[0x82] = 0x01; /* c/b size both LE */
            put_both32(sect + 0x82, 2048);
            /* 根目录记录 @0x86 (34B): len,ea, ext(8B both), size(8B both), dt, flags, ... name=1 */
            sect[0x86] = 34; sect[0x87] = 0;
            put_both32(sect + 0x88, ROOT_LBA);
            put_both32(sect + 0x90, 2048);
            sect[0x9F] = 0x02;                      /* 记录+25 flags: dir */
            sect[0xA6] = 1; sect[0xA7] = 0x00;      /* 记录+32 idlen + 根目录名 0 */
            s = sect;
        } else if (lba == 18) {                      /* 终止符 */
            sect[0] = 0xFF; memcpy(sect + 1, "CD001", 5); sect[6] = 1;
            s = sect;
        } else if (lba == ROOT_LBA) {                /* 根目录 */
            uint32_t p = 0;
            /* "." */
            sect[p + 0] = 34; put_both32(sect + p + 2, ROOT_LBA); put_both32(sect + p + 10, 2048);
            sect[p + 25] = 0x02; sect[p + 32] = 1; sect[p + 33] = 0x00;
            p += 34;
            /* ".." */
            sect[p + 0] = 34; put_both32(sect + p + 2, ROOT_LBA); put_both32(sect + p + 10, 2048);
            sect[p + 25] = 0x02; sect[p + 32] = 1; sect[p + 33] = 0x01;
            p += 34;
            /* exe */
            char nm[40]; snprintf(nm, sizeof nm, "%s", exe_name);
            size_t nl = strlen(nm);
            uint32_t rec = 33u + (uint32_t)nl + 2u; /* ";1" */
            sect[p + 0] = (uint8_t)rec; put_both32(sect + p + 2, FILE_LBA); put_both32(sect + p + 10, n);
            sect[p + 25] = 0x00; sect[p + 32] = (uint8_t)(nl + 2);
            memcpy(sect + p + 33, nm, nl); memcpy(sect + p + 33 + nl, ";1", 2);
            s = sect;
        } else if (lba >= FILE_LBA) {                /* exe 字节 */
            uint32_t off = (lba - FILE_LBA) * 2048u;
            uint32_t len = off < n ? (n - off < 2048u ? n - off : 2048u) : 0;
            memcpy(sect, exe_data + off, len);
            s = sect;
        }
        if (ss == 2352) { wrap_frame(s, w2352, i, mode2); fwrite(w2352, 1, 2352, f); }
        else fwrite(s, 1, 2048, f);
    }
    fclose(f);
    if (ss == 2352) {
        char cue[600]; snprintf(cue, sizeof cue, "%s.cue", out_path);
        FILE *c = fopen(cue, "w");
        if (c) { fprintf(c, "FILE \"%s\" BINARY\nTRACK 01 MODE%d/2352\nINDEX 01 00:02:00\n",
                         strrchr(out_path, '/') ? strrchr(out_path, '/') + 1 : out_path,
                         mode2 ? 2 : 1); fclose(c); }
    }
    return 0;
}

/* ---------------- --cd-selftest ---------------- */
#define CDCHK(cond, name) do { printf("CD_TEST %s: %s\n", name, (cond) ? "PASS" : "FAIL"); \
    if (!(cond)) bad++; } while (0)

void psx_cd_synth_exe_full(uint8_t *img, uint32_t n, const uint32_t *code, uint32_t code_bytes)
{   /* img=2048 头 + 0x100 data + code@0x900, n>=0x900+code_bytes */
    memset(img, 0, n);
    uint32_t *h = (uint32_t *)img;
    h[0x10 / 4] = 0x80041000u;      /* initial pc = 代码入口 */
    h[0x14 / 4] = 0x80040000u;      /* initial gp */
    h[0x18 / 4] = 0x80040000u;      /* data vaddr */
    h[0x1C / 4] = 0x100u;           /* data size */
    h[0x20 / 4] = 0x80041000u;      /* code vaddr */
    h[0x24 / 4] = code_bytes;       /* code size */
    if (code && code_bytes) memcpy(img + 0x800 + 0x100, code, code_bytes);
    uint32_t *dat = (uint32_t *)(img + 0x800);
    for (int i = 0; i < 0x100 / 4; i++) dat[i] = 0xABCD0000u + (uint32_t)i;
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
        CDCHK(ok == 0 && pc == 0x80041000u && gp == 0x80040000u, "t4_hbexe_header");
        psx_disc_close(&d);
    }
    free(img);
    printf("CD_EV bad=%d\n", bad);
    if (bad) { printf("CD SELFTEST FAIL (%d)\n", bad); return 1; }
    printf("CD CORE SELFTEST PASS\n");
    return 0;
}
