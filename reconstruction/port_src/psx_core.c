/*
 * psx_core.c —— PSX 薄引擎骨架 楔1 (task-106, SESSION-117)
 * 实现语义严格照 docs/82 映射构造器 + docs/81 服务表 ABI 的 Linux 对等物。
 * 注意: 不 MAP_FIXED 原址 (与 port 自身段冲突风险), 用"自由分配 + 平移常数"
 * ——与原件同构 (docs/82 本质就是 delta 直映射)。MAP_FIXED 直置列入楔2。
 */
#include "psx_core.h"
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

/* --- 测试服务族 (docs/81 分派语义的 Linux 侧对等实现) --- */
static uint32_t svc_add(uint32_t a, uint32_t b)   { return a + b; }
static uint32_t svc_sub(uint32_t a, uint32_t b)   { return a - b; }
static uint32_t svc_and(uint32_t a, uint32_t b)   { return a & b; }
static uint32_t svc_or(uint32_t a, uint32_t b)    { return a | b; }
static uint32_t svc_nop(uint32_t a, uint32_t b)   { (void)a; (void)b; return 0; }

int psx_core_init(psx_core_t *c)
{
    if (!c) return -1;
    memset(c, 0, sizeof *c);

    /* MAP_32BIT: 原件是 32 位进程, 状态块槽=uint32 无截断语义 */
    c->ram = mmap(NULL, PSX_RAM_ALLOCSZ, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (c->ram == MAP_FAILED) { c->ram = NULL; return -2; }
    c->mmio = mmap(NULL, PSX_MMIO_ALLOCSZ, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (c->mmio == MAP_FAILED) { c->mmio = NULL; return -3; }
    if ((uintptr_t)c->ram > 0xFFFFFFFFu || (uintptr_t)c->mmio > 0xFFFFFFFFu)
        return -4; /* 32 位不变式防回归 */

    /* 状态块构造 (docs/82 槽位与算式逐一镜像):
     *   +0x44 = RAM base; +0x48 = RAM - 0xBFC00000 (BIOS 窗平移)
     *   +0x28 = scratch;  +0x2C = scratch - 0x1F800000 (MMIO 平移)
     *   +0x24 = scratch + 0x400 (GPU 端口族基) */
    c->sb[PSXSB_RAM / 4]        = (uint32_t)(uintptr_t)c->ram;
    c->sb[PSXSB_ROM_DELTA / 4]  = (uint32_t)(uintptr_t)c->ram - PSX_GPA_ROM_BASE;
    c->sb[PSXSB_SCRATCH / 4]    = (uint32_t)(uintptr_t)c->mmio;
    c->sb[PSXSB_MMIO_DELTA / 4] = (uint32_t)(uintptr_t)c->mmio - PSX_GPA_MMIO_BASE;
    c->sb[PSXSB_GPU_BASE / 4]   = (uint32_t)(uintptr_t)c->mmio + 0x400u;

    /* 服务分派表 (docs/81: 表基址+索引 call 的函数指针面) */
    c->svc[0] = (void *)svc_add;
    c->svc[1] = (void *)svc_sub;
    c->svc[2] = (void *)svc_and;
    c->svc[3] = (void *)svc_or;
    c->svc[4] = (void *)svc_nop;
    return 0;
}

void psx_core_free(psx_core_t *c)
{
    if (!c) return;
    if (c->ram)  munmap(c->ram, PSX_RAM_ALLOCSZ);
    if (c->mmio) munmap(c->mmio, PSX_MMIO_ALLOCSZ);
    c->ram = c->mmio = NULL;
}

void *psx_translate(const psx_core_t *c, uint32_t gpa, uint32_t *win)
{
    uint32_t w = PSX_WIN_BAD;
    void *p = NULL;
    /* RAM 主窗 + kseg 镜像 (0x0000_0000.. 与 0x8000_0000.. 同物理) */
    uint32_t ram_off = gpa & 0x1FFFFFFFu;
    if ((gpa >> 28) == 8 || (gpa >> 28) == 0) {
        if (ram_off < PSX_RAM_ALLOCSZ) { p = c->ram + ram_off; w = PSX_WIN_RAM; }
    }
    /* BIOS ROM 窗: host = gpa + delta(0x48) —— docs/82 平移语义 */
    else if ((gpa >> 24) == 0xBF) {
        p = (void *)(uintptr_t)(gpa + c->sb[PSXSB_ROM_DELTA / 4]);
        w = PSX_WIN_ROM;
    }
    /* MMIO 窗 (0x1F800000-0x1F809400): host = gpa + delta(0x2C) */
    else if (gpa >= PSX_GPA_MMIO_BASE && gpa - PSX_GPA_MMIO_BASE < PSX_MMIO_ALLOCSZ) {
        p = (void *)(uintptr_t)(gpa + c->sb[PSXSB_MMIO_DELTA / 4]);
        w = PSX_WIN_MMIO;
    }
    if (win) *win = w;
    return p;
}

uint32_t psx_svc_call(psx_core_t *c, uint32_t idx, uint32_t a, uint32_t b)
{
    if (idx >= PSX_SVC_SLOTS || !c->svc[idx]) return 0xFFFFFFFFu;
    c->svc_nlog++;
    c->svc_last = idx;
    typedef uint32_t (*fn2)(uint32_t, uint32_t);
    /* docs/81 ABI 对等: 原件"表基址+字节偏移直取再 call"——64 位宿主 slot 宽
     * sizeof(void*)，等价式即按字节偏移取槽 */
    fn2 f = (fn2)(void *)*(void **)((char *)c->svc + idx * (uint32_t)sizeof(void *));
    return f(a, b);
}

/* 单次求值: cond 含副作用调用(如 svc 分派计数)时双评即双计——真实踩后固化 */
#define CHK(cond, name)                                                    \
    do { int _ok = (cond) ? 1 : 0;                                         \
         printf("PSX_TEST %s: %s\n", name, _ok ? "PASS" : "FAIL");        \
         if (!_ok) bad++; } while (0)

int cmd_psx_selftest(int argc, char **argv)
{
    (void)argc; (void)argv;
    psx_core_t c;
    int bad = 0;

    if (psx_core_init(&c) != 0) { printf("PSX_EV init FAIL\n"); return 1; }
    printf("PSX_EV ram=%p mmio=%p allocs=%u/%u\n", (void *)c.ram, (void *)c.mmio,
           PSX_RAM_ALLOCSZ, PSX_MMIO_ALLOCSZ);

    /* 1. 状态块平移常数语义 (docs/82) */
    CHK(c.sb[PSXSB_RAM / 4] == (uint32_t)(uintptr_t)c.ram, "sb44_ram_base");
    CHK((uint32_t)(c.sb[PSXSB_ROM_DELTA / 4] + PSX_GPA_ROM_BASE) ==
        (uint32_t)(uintptr_t)c.ram, "sb48_rom_delta");
    CHK((uint32_t)(c.sb[PSXSB_MMIO_DELTA / 4] + PSX_GPA_MMIO_BASE) ==
        (uint32_t)(uintptr_t)c.mmio, "sb2c_mmio_delta");
    CHK(c.sb[PSXSB_GPU_BASE / 4] == c.sb[PSXSB_SCRATCH / 4] + 0x400u,
        "sb24_gpu_1f800400_base");

    /* 2. 翻译: RAM 主窗/镜像 + BIOS 窗 + MMIO 三族 + 非法 */
    uint32_t w;
    void *p1 = psx_translate(&c, 0x80012340u, &w);
    CHK(p1 && w == PSX_WIN_RAM && p1 == c.ram + 0x12340u, "xlat_ram_80012340");
    void *p2 = psx_translate(&c, 0x00012340u, &w);
    CHK(p2 == p1 && w == PSX_WIN_RAM, "xlat_kseg_mirror_alias");
    void *p3 = psx_translate(&c, 0xBFC00010u, &w);
    CHK(p3 == c.ram + 0x10u && w == PSX_WIN_ROM, "xlat_rom_window");
    void *m1 = psx_translate(&c, 0x1F801040u, &w);
    void *m2 = psx_translate(&c, 0x1F801800u, &w);
    void *m3 = psx_translate(&c, 0x1F801120u, &w);
    CHK(m1 && m2 && m3 && w == PSX_WIN_MMIO && m1 != m2 &&
        (uint8_t *)m2 >= c.mmio && (uint8_t *)m2 < c.mmio + PSX_MMIO_ALLOCSZ,
        "xlat_mmio_1040_1800_1120");
    CHK(psx_translate(&c, 0x7FFFFFFFu, &w) == NULL && w == PSX_WIN_BAD,
        "xlat_oob_bad");

    /* 3. 读写 roundtrip: RAM 主窗写→镜像窗读 / MMIO 寄存器堆 */
    *(uint32_t *)p1 = 0xC0FFEE01u;
    void *pm = psx_translate(&c, 0x00012340u, &w);
    CHK(pm && *(uint32_t *)pm == 0xC0FFEE01u, "ram_mirror_roundtrip");
    *(uint32_t *)m1 = 0x0000ABCDu;
    CHK(*(uint32_t *)psx_translate(&c, 0x1F801040u, &w) == 0x0000ABCDu,
        "mmio_store_load_roundtrip");

    /* 4. 服务分派表 ABI (docs/81): 索引调用 + 日志 + 地址算式 */
    CHK(psx_svc_call(&c, 0, 3, 4) == 7u, "svc_add_idx0");
    CHK(psx_svc_call(&c, 1, 10, 3) == 7u, "svc_sub_idx1");
    CHK(psx_svc_call(&c, 2, 0xF0u, 0x3Cu) == 0x30u, "svc_and_idx2");
    CHK(c.svc_nlog == 3u && c.svc_last == 2u, "svc_log_count");
    CHK(*(void **)((char *)c.svc + 2u * (uint32_t)sizeof(void *)) == c.svc[2] &&
        (char *)&c.svc[2] - (char *)c.svc == (int)(2u * sizeof(void *)),
        "svc_byte_offset_abi");
    CHK(psx_svc_call(&c, 15, 0, 0) == 0xFFFFFFFFu, "svc_empty_slot_guard");

    psx_core_free(&c);
    printf(bad ? "PSX CORE SELFTEST FAIL (bad=%d)\n" : "PSX CORE SELFTEST PASS\n", bad);
    return bad ? 2 : 0;
}
