/*
 * psx_cdrom.h —— 清洁房战役楔3: 自研光盘装载面 (task-130)
 *
 * 零版权: 读取器 100% 自研 C; 盘图像=用户自有文件运行期装载, 永不入库。
 * 格式依据 = ISO9660(ECMA-119 公开标准) + CD-ROM 扇区帧布局(公开文献):
 *   2048 裸模式        : user data @ frame*2048
 *   MODE1/2352         : sync(12)+hdr(4)+data(2048)+EDC+ECC; data@24
 *   MODE2/2352 FORM1/2 : +subheader(8/0); data@32 / @28
 * 命令面真值 = psx-spx 扇文档的字节语义 (ACK 0x11 / stat 位 / GetStat 壳门),
 * 本仓库 docs/102 已定案寄存器窗 (0x1F801820-27 + CDINT bit2)。
 */
#ifndef PSX_CDROM_H
#define PSX_CDROM_H

#include <stdint.h>
#include <stdio.h>

#define CD_SECT_DATA 2048u
#define CD_TRACK_MAX 8u

typedef struct {
    uint32_t lba;        /* 数据区 LBA (0 = 首数据扇区, MSF-150 约定) */
    uint32_t size;       /* 字节 */
    uint8_t  is_dir;
    char     name[64];   /* 大写基名, ";1" 已剥 */
} cd_file_t;

typedef struct {
    FILE    *f;
    int     sector_size;          /* 2048 | 2352 */
    int     mode2;                /* (兼容位) 2352 帧是 MODE2(XA) */
    int     data_off;             /* 2352 帧 user data 偏移: 16=真MODE1 / 24=零售&自造 / 32=XA-FORM 探测定案 */
    uint32_t data_frame0;         /* 数据区 LBA0 的帧号 */
    uint8_t  buf[2448];           /* 帧暂存 */
    /* ISO9660 */
    int     have_iso;
    uint32_t root_lba, root_size;
    char     volume_id[33];
    cd_file_t files[1024];
    int     nfiles;
    uint32_t cnf_stack;           /* SYSTEM.CNF STACK=(十六进制) 冷启 sp 基址, 0=无 */
    /* 简易 fd 面 (A 族文件 API 消费) */
    struct { int used; int fidx; uint32_t pos; } fd[8];
} psx_disc_t;

int  psx_disc_open(psx_disc_t *d, const char *path);   /* .iso/.bin/(.cue) */
void psx_disc_close(psx_disc_t *d);
/* 读 n 个扇区的 user data(解帧后 2048/扇); 越界=失败 */
int  psx_disc_read(psx_disc_t *d, uint32_t lba, uint32_t nsec, uint8_t *dst);
/* ISO9660 目录全量枚举(根目录一层递归)入 files[] */
int  psx_disc_scan_root(psx_disc_t *d);
/* 找文件(大小写折叠, 剥 ";N"); 命中返回下标, 否则 -1 */
int  psx_disc_find(psx_disc_t *d, const char *name);
/* 零售启动态解析 (公开 PSX 盘约定): 根目录 SYSTEM.CNF 的 BOOT=cdrom:\XXX;1 →
 * 剥前缀大写名; 无 CNF 回落 PSX.EXE。命中返回 files[] 下标, name_out 可 NULL。 */
int  psx_disc_boot_exe(psx_disc_t *d, char *name_out, unsigned cap);

/* ---- 自造 ISO 构建器 (HBEXE 测试盘, 亦 task-133 发布链输入; 零版权) ---- */
/* exe_data = PSX-EXE 镜像(含 2KB 头), n = 字节; cue_mode: 0=裸2048 .iso, 1=MODE1/2352, 2=MODE2/2352 */
int  psx_iso_build(const char *out_path, const uint8_t *exe_data, uint32_t n,
                   const char *exe_name, int cue_mode);
/* 参数化构建器: stride=2048|2352, data_off=16|24|32(2352 时), pregap150, cnf_text 非空
 * 则根目录加 SYSTEM.CNF(LBA20, exe 顺移 LBA21)。全部自造字节, 零版权。 */
int  psx_iso_build2(const char *out_path, const uint8_t *exe_data, uint32_t n,
                    const char *exe_name, int stride, int data_off,
                    int pregap150, const char *cnf_text);

/* main.c 子命令入口 (--cd-selftest): 0=PASS */
int  cmd_cd_selftest(int argc, char **argv);
/* 合成 HBEXE 镜像 (测试夹具; psx_dev 集成探针共用): img=n 字节 2KB 头+段 */
void psx_cd_synth_exe(uint8_t *img, uint32_t n);
/* 自定义代码段版 (task-131 HBEXE 冷启夹具) */
void psx_cd_synth_exe_full(uint8_t *img, uint32_t n, const uint32_t *code, uint32_t code_bytes);
#define PSX_ISO_FILE_LBA 20u   /* psx_iso_build 布局: exe 起始 LBA */

#endif /* PSX_CDROM_H */
