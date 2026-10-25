/*
 * boot.c —— 原生 booter: 原"自映射+CALL 桩"协议在 Linux 上的等价重建
 *
 * 原协议 (rewrite/docs/02_loader.md):
 *   Win9x 门 -> GetModuleFileName -> CreateFile(PAGE_READONLY 映射) ->
 *   CALL view+0x55338 (EAX=view) -> 错误表 -> ExitProcess(0)
 * 端口等价:
 *   平台门 = Linux + (X11|Wayland) + Vulkan 可用性检查(由 vk_host 完成)
 *   映射   = mmap(PROT_READ, MAP_PRIVATE) 原 EXE 文件 (只读, 永不执行)
 *   "CALL" = 布局/魔数/RAND 折叠校验 —— **不执行原始代码** (NATIVE-ONLY)
 */
#include "bleem_native.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

uint32_t bleem_rand_fold(const uint8_t *p, size_t nbytes, uint32_t start_rva)
{
    uint32_t fold = (start_rva & ~3u) ^ BLEEM_RAND_SEED;
    for (size_t i = 0; i + 4 <= nbytes; i += 4) {
        uint32_t w;
        memcpy(&w, p + i, 4);
        fold ^= w;
    }
    return fold;
}

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

int bleem_boot_open(bleem_image_t *img, const char *path)
{
    memset(img, 0, sizeof(*img));
    img->feat = bleem_features_full();
    img->path = path;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return BLEEM_ERR_GENERIC;
    struct stat st;
    if (fstat(fd, &st) != 0 || (size_t)st.st_size != 351732) {
        close(fd);
        return BLEEM_ERR_CORRUPT;   /* 原语义: "Please reinstall" */
    }
    void *m = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED) return BLEEM_ERR_NOMEM;
    img->map = m;
    img->len = (size_t)st.st_size;
    return BLEEM_OK;
}

/* PE 段表走查: 计算 raw 尾, 确认追加区边界 (与 tools/extract_payload.py 同判据) */
int bleem_boot_verify(bleem_image_t *img)
{
    const uint8_t *d = img->map;
    if (img->len < 0x1000 || d[0] != 'M' || d[1] != 'Z') return BLEEM_ERR_CORRUPT;
    uint32_t pe = rd32(d + 0x3C);
    if (pe + 24 > img->len || rd32(d + pe) != 0x00004550u) return BLEEM_ERR_CORRUPT;
    uint16_t nsec = d[pe + 6] | (d[pe + 7] << 8);
    uint16_t szopt = d[pe + 20] | (d[pe + 21] << 8);
    size_t sec = pe + 24 + szopt, raw_end = 0;
    for (uint16_t i = 0; i < nsec && sec + 40 * (i + 1) <= img->len; i++) {
        uint32_t rs = rd32(d + sec + 40 * i + 16);
        uint32_t ro = rd32(d + sec + 40 * i + 20);
        if ((size_t)ro + rs > raw_end) raw_end = (size_t)ro + rs;
    }
    if (raw_end != BLEEM_APPEND_START) return BLEEM_ERR_CORRUPT;
    if (img->len - raw_end != BLEEM_APPEND_SIZE) return BLEEM_ERR_CORRUPT;
    if (rd32(d + BLEEM_STUB_OFF) != BLEEM_STUB_MAGIC) return BLEEM_ERR_CORRUPT;

    img->rand_fold_stub = bleem_rand_fold(d + BLEEM_STUB_OFF,
                                          BLEEM_STUB_END - BLEEM_STUB_OFF - 4,
                                          (uint32_t)BLEEM_STUB_OFF);
    img->ok_layout = 1;
    return BLEEM_OK;
}

void bleem_boot_close(bleem_image_t *img)
{
    if (img->map) munmap(img->map, img->len);
    memset(img, 0, sizeof(*img));
}
