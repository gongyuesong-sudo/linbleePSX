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
    int     mode2;                /* 2352 帧是 MODE2(XA) */
    uint32_t data_frame0;         /* 数据区 LBA0 的帧号 */
    uint8_t  buf[2448];           /* 帧暂存 */
    /* ISO9660 */
    int     have_iso;
    uint32_t root_lba, root_size;
    char     volume_id[33];
    cd_file_t files[64];
    int     nfiles;
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

/* ---- 自造 ISO 构建器 (HBEXE 测试盘, 亦 task-133 发布链输入; 零版权) ---- */
/* exe_data = PSX-EXE 镜像(含 2KB 头), n = 字节; cue_mode: 0=裸2048 .iso, 1=MODE1/2352, 2=MODE2/2352 */
int  psx_iso_build(const char *out_path, const uint8_t *exe_data, uint32_t n,
                   const char *exe_name, int cue_mode);

/* main.c 子命令入口 (--cd-selftest): 0=PASS */
int  cmd_cd_selftest(int argc, char **argv);
/* 合成 HBEXE 镜像 (测试夹具; psx_dev 集成探针共用): img=n 字节 2KB 头+段 */
void psx_cd_synth_exe(uint8_t *img, uint32_t n);
#define PSX_ISO_FILE_LBA 20u   /* psx_iso_build 布局: exe 起始 LBA */

#endif /* PSX_CDROM_H */
