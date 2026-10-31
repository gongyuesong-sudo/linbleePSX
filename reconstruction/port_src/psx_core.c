/*
 * psx_core.c —— PSX 薄引擎骨架 楔1+楔2 (task-106/108); 楔2=fixed 原址三窗+BIOS 装载
 * 实现语义严格照 docs/82 映射构造器 + docs/81 服务表 ABI 的 Linux 对等物。
 * 注意: 不 MAP_FIXED 原址 (与 port 自身段冲突风险), 用"自由分配 + 平移常数"
 * ——与原件同构 (docs/82 本质就是 delta 直映射)。MAP_FIXED 直置列入楔2。
 */
#define _GNU_SOURCE
#include "psx_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>

/* --- SHA-256 (FIPS 180-4, compact static impl — 取证登记专用, 无外部依赖) --- */
typedef struct { uint32_t h[8]; uint64_t len; uint8_t buf[64]; size_t fill; } sha256_ctx;
static const uint32_t sha256_k[64] = {
 0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
 0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
 0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
 0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
 0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
 0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
 0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
 0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
#define ROR(x,n) (((x)>>(n))|((x)<<(32-(n))))
static void sha256_block(sha256_ctx *s, const uint8_t *p)
{
    uint32_t w[64], a,b,c,d,e,f,g,h,t1,t2; int i;
    for (i=0;i<16;i++) w[i]=(uint32_t)p[i*4]<<24|p[i*4+1]<<16|p[i*4+2]<<8|p[i*4+3];
    for (;i<64;i++){uint32_t s0=ROR(w[i-15],7)^ROR(w[i-15],18)^(w[i-15]>>3),
                       s1=ROR(w[i-2],17)^ROR(w[i-2],19)^(w[i-2]>>10);
                    w[i]=w[i-16]+s0+w[i-7]+s1;}
    a=s->h[0];b=s->h[1];c=s->h[2];d=s->h[3];e=s->h[4];f=s->h[5];g=s->h[6];h=s->h[7];
    for (i=0;i<64;i++){uint32_t S1=ROR(e,6)^ROR(e,11)^ROR(e,25),
        ch=(e&f)^((~e)&g),t1=h+S1+ch+sha256_k[i]+w[i],
        S0=ROR(a,2)^ROR(a,13)^ROR(a,22),mj=(a&b)^(a&c)^(b&c),t2=S0+mj;
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
    s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h;
}
static void sha256_init(sha256_ctx *s){s->h[0]=0x6a09e667;s->h[1]=0xbb67ae85;s->h[2]=0x3c6ef372;
 s->h[3]=0xa54ff53a;s->h[4]=0x510e527f;s->h[5]=0x9b05688c;s->h[6]=0x1f83d9ab;s->h[7]=0x5be0cd19;
 s->len=0;s->fill=0;}
static void sha256_upd(sha256_ctx *s,const uint8_t *d,size_t n){s->len+=n;
 while(n){size_t k=64-s->fill;if(k>n)k=n;memcpy(s->buf+s->fill,d,k);s->fill+=k;d+=k;n-=k;
 if(s->fill==64){sha256_block(s,s->buf);s->fill=0;}}}
static void sha256_fin(sha256_ctx *s,char hex[65]){uint64_t bits=s->len*8;uint8_t z=0x80;
 sha256_upd(s,&z,1);z=0;
 while(s->fill!=56) sha256_upd(s,&z,1);
 uint8_t lb[8];for(int i=0;i<8;i++)lb[i]=(uint8_t)(bits>>(56-8*i));
 sha256_upd(s,lb,8);
 for(int i=0;i<8;i++)sprintf(hex+i*8,"%08x",s->h[i]);
}

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

/* 楔2: 三窗 MAP_FIXED_NOREPLACE 原址直置。NOREPLACE 而非 FIXED:
 * 与 port 自身段冲突时返回 EEXIST 硬失败(-5..-7), 绝不覆盖自有映射。 */
int psx_core_init_fixed(psx_core_t *c)
{
    if (!c) return -1;
    memset(c, 0, sizeof *c);
    int fl = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE;
    c->ram = mmap((void *)(uintptr_t)PSX_GPA_RAM_BASE, PSX_RAM_ALLOCSZ,
                  PROT_READ | PROT_WRITE, fl, -1, 0);
    if (c->ram != (void *)(uintptr_t)PSX_GPA_RAM_BASE) { c->ram = NULL; return -5; }
    c->bios = mmap((void *)(uintptr_t)PSX_GPA_ROM_BASE, PSX_BIOS_SIZE,
                   PROT_READ | PROT_WRITE, fl, -1, 0);
    if (c->bios != (void *)(uintptr_t)PSX_GPA_ROM_BASE) { c->bios = NULL; return -6; }
    c->mmio = mmap((void *)(uintptr_t)PSX_GPA_MMIO_BASE, PSX_MMIO_ALLOCSZ,
                   PROT_READ | PROT_WRITE, fl, -1, 0);
    if (c->mmio != (void *)(uintptr_t)PSX_GPA_MMIO_BASE) { c->mmio = NULL; return -7; }
    c->fixed = 1;
    /* fixed 态状态块=原址恒等: delta 槽全 0, 基址槽=原 GPA —— 原件 32 位进程天然如此 */
    c->sb[PSXSB_RAM / 4]        = PSX_GPA_RAM_BASE;
    c->sb[PSXSB_ROM_DELTA / 4]  = 0;
    c->sb[PSXSB_SCRATCH / 4]    = PSX_GPA_MMIO_BASE;
    c->sb[PSXSB_MMIO_DELTA / 4] = 0;
    c->sb[PSXSB_GPU_BASE / 4]   = PSX_GPA_MMIO_BASE + 0x400u;
    c->svc[0] = (void *)svc_add;
    c->svc[1] = (void *)svc_sub;
    c->svc[2] = (void *)svc_and;
    c->svc[3] = (void *)svc_or;
    c->svc[4] = (void *)svc_nop;
    return 0;
}

/* 楔2: BLEEM_BIOS 装载(AGENTS 安全约束): 只登记 size/sha, ROM 字节不入 git;
 * 未设=0 保持 HLE; 已设但缺失/尺寸非法/读败=硬失败, 不静默降级 */
int psx_load_bios(psx_core_t *c)
{
    if (!c || !c->fixed || !c->bios) return -9; /* 装载须 fixed 态独立 BIOS 窗 */
    const char *path = getenv("BLEEM_BIOS");
    if (!path || !*path) return 0;
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return -10;
    if ((uint32_t)st.st_size != PSX_BIOS_SIZE) return -11;
    FILE *f = fopen(path, "rb");
    if (!f) return -12;
    sha256_ctx sc; sha256_init(&sc);
    size_t total = 0, n; uint8_t tmp[8192];
    while ((n = fread(tmp, 1, sizeof tmp, f)) > 0) {
        if (total + n > PSX_BIOS_SIZE) { fclose(f); return -13; }
        memcpy(c->bios + total, tmp, n); sha256_upd(&sc, tmp, n); total += n;
    }
    fclose(f);
    if (total != PSX_BIOS_SIZE) return -14;
    sha256_fin(&sc, c->bios_sha);
    return 0;
}

void psx_core_free(psx_core_t *c)
{
    if (!c) return;
    if (c->ram)  munmap(c->ram,  c->fixed ? PSX_RAM_ALLOCSZ : PSX_RAM_ALLOCSZ);
    if (c->mmio) munmap(c->mmio, PSX_MMIO_ALLOCSZ);
    if (c->bios) munmap(c->bios, PSX_BIOS_SIZE);
    c->ram = c->mmio = c->bios = NULL;
}

void *psx_translate(const psx_core_t *c, uint32_t gpa, uint32_t *win)
{
    uint32_t w = PSX_WIN_BAD;
    void *p = NULL;
    uint32_t ram_off = gpa & 0x1FFFFFFFu;
    if ((gpa >> 28) == 8 || (gpa >> 28) == 0) {
        /* RAM 主窗 + kseg 镜像 (0x0000_0000.. 与 0x8000_0000.. 同物理) */
        if (ram_off < PSX_RAM_ALLOCSZ) {
            p = c->fixed ? (void *)(uintptr_t)(PSX_GPA_RAM_BASE + ram_off)
                         : c->ram + ram_off;
            w = PSX_WIN_RAM;
        }
    }
    /* BIOS ROM 窗: 楔1 = host gpa+delta(0x48) 折进 RAM 块(docs/82 平移语义);
     * 楔2 fixed = 独立 512KB 窗原址恒等, 越出 0x80000 判 BAD */
    else if ((gpa >> 24) == 0xBF) {
        if (c->fixed) {
            if (gpa - PSX_GPA_ROM_BASE < PSX_BIOS_SIZE) {
                p = (void *)(uintptr_t)gpa; w = PSX_WIN_ROM;
            }
        } else {
            p = (void *)(uintptr_t)(gpa + c->sb[PSXSB_ROM_DELTA / 4]);
            w = PSX_WIN_ROM;
        }
    }
    /* MMIO 窗 (0x1F800000-0x1F809400): 楔1 delta 平移 / 楔2 恒等 */
    else if (gpa >= PSX_GPA_MMIO_BASE && gpa - PSX_GPA_MMIO_BASE < PSX_MMIO_ALLOCSZ) {
        p = c->fixed ? (void *)(uintptr_t)gpa
                     : (void *)(uintptr_t)(gpa + c->sb[PSXSB_MMIO_DELTA / 4]);
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

    /* ---- 楔2 Phase B: MAP_FIXED_NOREPLACE 原址直置 + BLEEM_BIOS 装载 ---- */
    psx_core_t f;
    int rc = psx_core_init_fixed(&f);
    printf("PSX_EV2 fixed_init rc=%d ram=%p bios=%p mmio=%p\n", rc,
           (void *)f.ram, (void *)f.bios, (void *)f.mmio);
    CHK(rc == 0 && f.ram  == (void *)(uintptr_t)PSX_GPA_RAM_BASE &&
        f.bios == (void *)(uintptr_t)PSX_GPA_ROM_BASE &&
        f.mmio == (void *)(uintptr_t)PSX_GPA_MMIO_BASE, "fixed_three_windows_at_gpa");
    CHK(f.sb[PSXSB_RAM/4]==PSX_GPA_RAM_BASE && f.sb[PSXSB_ROM_DELTA/4]==0 &&
        f.sb[PSXSB_MMIO_DELTA/4]==0 && f.sb[PSXSB_GPU_BASE/4]==0x1F800400u &&
        f.sb[PSXSB_SCRATCH/4]==PSX_GPA_MMIO_BASE, "fixed_sb_identity_slots");
    void *q1 = psx_translate(&f, 0x80012340u, &w);
    CHK(q1 == (void *)(uintptr_t)0x80012340u && w == PSX_WIN_RAM, "xlat2_ram_identity");
    void *q2 = psx_translate(&f, 0x00012340u, &w);
    CHK(q2 == (void *)(uintptr_t)0x80012340u, "xlat2_kseg_to_phys");
    void *q3 = psx_translate(&f, 0xBFC00010u, &w);
    CHK(q3 == (void *)(uintptr_t)0xBFC00010u && w == PSX_WIN_ROM, "xlat2_bios_identity");
    void *q4 = psx_translate(&f, 0x1F801040u, &w);
    CHK(q4 == (void *)(uintptr_t)0x1F801040u && w == PSX_WIN_MMIO, "xlat2_mmio_identity");
    CHK(psx_translate(&f, 0xBFC80000u, &w) == NULL && w == PSX_WIN_BAD,
        "xlat2_bios_window_oob");
    /* 0 别名窗不可直映(mmap_min_addr=64K 禁裸 0x0 段)——原件同址别名在 guest
     * 页表层, Linux 对等=translate 软件别名; 直读仅限已 mmap 的 0x80000000 域 */
    *(uint32_t *)q1 = 0xDEADBEEFu;
    CHK(*(volatile uint32_t *)((uintptr_t)PSX_GPA_RAM_BASE + 0x12340u) == 0xDEADBEEFu,
        "fixed_gpa_direct_roundtrip");
    *(uint32_t *)q4 = 0x5A5A0001u;
    CHK(*(volatile uint32_t *)(uintptr_t)0x1F801040u == 0x5A5A0001u,
        "fixed_mmio_gpa_roundtrip");
    CHK(psx_svc_call(&f, 0, 40, 2) == 42u, "fixed_svc_table_ok");

    /* BIOS 装载三态 (AGENTS 硬失败语义) */
    char *saved = getenv("BLEEM_BIOS") ? strdup(getenv("BLEEM_BIOS")) : NULL;
    unsetenv("BLEEM_BIOS");
    CHK(psx_load_bios(&f) == 0 && f.bios_sha[0] == '\0', "bios_unset_hle_zero");
    setenv("BLEEM_BIOS", "/nonexistent/bleem_bios.bin", 1);
    CHK(psx_load_bios(&f) == -10, "bios_missing_hardfail_m10");
    setenv("BLEEM_BIOS", "/etc/hostname", 1);
    CHK(psx_load_bios(&f) == -11, "bios_badsize_hardfail_m11");
    if (saved && *saved) {
        setenv("BLEEM_BIOS", saved, 1);
        int b = psx_load_bios(&f);
        uint32_t v0 = *(volatile uint32_t *)(uintptr_t)PSX_GPA_ROM_BASE;
        printf("PSX_EV2 bios_sha=%s first_insn=%08x\n", f.bios_sha, v0);
        CHK(b == 0 && strlen(f.bios_sha) == 64 && v0 != 0 &&
            psx_translate(&f, PSX_GPA_ROM_BASE, &w) == f.bios,
            "bios_load_sha_register_window");
        free(saved);
    } else {
        printf("PSX_EV2 bios=absent(HLE 态, host100 带 ROM 复跑覆盖装载面)\n");
        CHK(1, "bios_absent_skipped");
    }
    psx_core_free(&f);

    printf(bad ? "PSX CORE SELFTEST FAIL (bad=%d)\n" : "PSX CORE SELFTEST PASS\n", bad);
    return bad ? 2 : 0;
}
