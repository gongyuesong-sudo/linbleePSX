/* biosrom.h — H-8a: BLEEM_BIOS ROM 接线（route B'，SESSION-19）
 * 约定：BLEEM_BIOS 未设 = 走 HLE ret-sled 老路；已设 = 必须为 0x80000 字节
 * 合法自有 PSX BIOS ROM，否则硬失败退出（不静默降级）。
 * ROM 规范落点 = BIOS 源区 +0xC0000（对应 cb+0x48 BIOS 偏移；
 * 0xBFC00000 窗映射时从源区 +0 整窗拷贝，故先拷入源区）。
 * 含公有域 SHA-256，用于 host/build/bios_sha256.txt 登记。 */
#ifndef BIOSROM_H
#define BIOSROM_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static const uint8_t *g_bios_rom;          /* 载入后的 ROM 镜像（NULL=无 ROM） */
static const char *g_bios_path;
static char g_bios_hex[65];

/* ---- SHA-256（Brad Conte 公有域实现风格，逐字精简） ---- */
static const uint32_t sha256_k[64] = {
 0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
 0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
 0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
 0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
 0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
 0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
 0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
 0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
#define ROR32(x,n) (((x)>>(n))|((x)<<(32-(n))))
static void sha256_compress(uint32_t *h, const uint8_t *p){
    uint32_t w[64],a,b,c,d,e,f,g,hh,t1,t2; int i;
    for(i=0;i<16;i++) w[i]=((uint32_t)p[i*4]<<24)|(p[i*4+1]<<16)|(p[i*4+2]<<8)|p[i*4+3];
    for(;i<64;i++){uint32_t s0=ROR32(w[i-15],7)^ROR32(w[i-15],18)^(w[i-15]>>3),
                      s1=ROR32(w[i-2],17)^ROR32(w[i-2],19)^(w[i-2]>>10);
                   w[i]=w[i-16]+s0+w[i-7]+s1;}
    a=h[0];b=h[1];c=h[2];d=h[3];e=h[4];f=h[5];g=h[6];hh=h[7];
    for(i=0;i<64;i++){uint32_t S1=ROR32(e,6)^ROR32(e,11)^ROR32(e,25),
                      ch=(e&f)^((~e)&g),
                      S0=ROR32(a,2)^ROR32(a,13)^ROR32(a,22),
                      maj=(a&b)^(a&c)^(b&c);
        t1=hh+S1+ch+sha256_k[i]+w[i]; t2=S0+maj;
        hh=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
}
static void sha256_hex(const void *data, size_t len, char out_hex[65]){
    uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    const uint8_t *m=(const uint8_t*)data; size_t n=len, i;
    uint8_t tail[128]; size_t tl; uint64_t bits=(uint64_t)len*8;
    for(i=0;i+64<=n;i+=64) sha256_compress(h,m+i);
    tl=n-i; memcpy(tail,m+i,tl); tail[tl++]=0x80;
    while((tl&63)!=56) tail[tl++]=0;
    for(int q=0;q<8;q++) tail[tl++]=(uint8_t)(bits>>(56-8*q));
    for(i=0;i<tl;i+=64) sha256_compress(h,tail+i);
    for(i=0;i<8;i++) sprintf(out_hex+i*8,"%08x",h[i]);
}

/* ---- 载入 + 登记 ---- */
static int bios_rom_load(void){
    const char *p = getenv("BLEEM_BIOS");
    if(!p || !*p) return 0;                                   /* 未设 → HLE 老路 */
    g_bios_path = p;
    FILE *f = fopen(p, "rb");
    if(!f){ fprintf(stderr, "BLEEM_BIOS error: cannot open '%s'\n", p); return -1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if(sz != 0x80000){ fprintf(stderr, "BLEEM_BIOS error: size=%ld expected 524288 (0x80000)\n", sz);
                       fclose(f); return -1; }
    void *buf = malloc(0x80000);
    if(!buf || fread(buf,1,0x80000,f) != 0x80000){ fprintf(stderr,"BLEEM_BIOS error: read fail\n");
                       fclose(f); return -1; }
    fclose(f);
    g_bios_rom = (const uint8_t*)buf;
    sha256_hex(buf, 0x80000, g_bios_hex);
    fprintf(stderr, "[host] BLEEM_BIOS loaded '%s' size=524288 sha256=%s\n", p, g_bios_hex);
    return 1;
}
/* ROM 拷入 BIOS 源区基址 base。两宿主约定：
 *  htrace：源区=512KB，base+0 ↔ 0xBFC00000（窗整区拷贝）
 *  host32：源区=2MB（cb+0x44 块），ROM 置于 +0xC0000（cb+0x48 偏移语义），窗拷贝走该偏移 */
static void bios_rom_place(uint32_t base, uint32_t off){
    if(g_bios_rom) memcpy((void*)(uintptr_t)(base + off), g_bios_rom, 0x80000);
}
/* 接线登记文件（task-017 验收对象） */
static void bios_rom_register(const char *path_out){
    if(!g_bios_rom) return;
    FILE *f = fopen(path_out, "w");
    if(f){ fprintf(f, "%s  %s  524288\n", g_bios_hex, g_bios_path); fclose(f); }
}
#endif /* BIOSROM_H */
