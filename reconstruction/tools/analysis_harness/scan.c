// scan.c — run bleem's plaintext decompressor natively over candidate stream headers
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>

extern void run_decoder(void *resbuf, void *dst, void *stream, uint32_t fn, void *sentinel);
extern void run_back(void);

#define IMG_BASE 0x400000u
static uint8_t *arena;
static uint8_t *file_image;
static uint32_t file_size;

static uint32_t xorall(uint32_t *p, size_t nd){
    uint32_t x=0; for(size_t i=0;i<nd;i++) x^=p[i]; return x;
}

int main(int argc, char**argv){
    const char *path = argc>1?argv[1]:"/home/gill/bleem/demo-v1_4.EXE";
    uint32_t lo = argc>2?strtoul(argv[2],0,16):0xfa00;
    uint32_t hi = argc>3?strtoul(argv[3],0,16):0x55338;
    const char *outp = argc>4?argv[4]:NULL;
    FILE *f=fopen(path,"rb"); if(!f){perror("open");return 1;}
    fseek(f,0,SEEK_END); file_size=ftell(f); rewind(f);
    void *m=mmap((void*)IMG_BASE, file_size+0x4000, PROT_READ|PROT_WRITE|PROT_EXEC,
                 MAP_PRIVATE|MAP_FIXED, fileno(f), 0);
    if(m==MAP_FAILED){perror("mmap img");return 2;}
    file_image=m;
    arena=mmap(NULL, 96u<<20, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANONYMOUS, -1,0);
    if(arena==MAP_FAILED){perror("mmap arena");return 3;}

    uint32_t targets[] = {0xe2808b09u,0x5a53cb18u,0x8a3a69fdu,0x4d709ecau,0x4d24af3du};
    for(uint32_t sb = lo; sb+8 <= hi; sb += 4){
        uint32_t L   = *(uint32_t*)(file_image+sb);
        uint32_t jmp = *(uint32_t*)(file_image+sb+4);
        if(L < 0x10 || L > 0x600000) continue;
        if((uint32_t)jmp > (hi-lo+0x10000)) continue;
        memset(arena, 0, 32u<<20);
        void *dst = arena + (16u<<20);
        uint32_t res[2];
        run_decoder(res, dst, file_image+sb, IMG_BASE+0x5577au, (void*)run_back);
        uint32_t *o = dst;
        uint32_t xa = xorall(o, L/4);
        uint32_t nz=0; for(uint32_t i=0;i<L && i<4096;i++) nz += o[i]!=0;
        for(int t=0;t<5;t++){
            uint32_t need = targets[t] ^ 0x52414E44u ^ xa;
            // plausible if need is in table vicinity or equals known dsts/L/sb
            uint32_t known[] = {0x60,0x6c,0x70,0x8e8,0xe18,0x5a244,0x44,L,sb,IMG_BASE+sb,0,(uint32_t)(uintptr_t)dst};
            for(unsigned s=0;s<12;s++){
                if(need==known[s]){
                    printf("HIT sb=%#x L=%#x jmp=%#x target=%#010x seed=%#010x edi_rel=%#x nz4k=%u\n",
                        sb,L,jmp,targets[t],need,res[0]-((uint32_t)(uintptr_t)dst),nz);
                    char name[96]; snprintf(name,sizeof name,"/home/gill/bleem/.analysis/dec_%08x.bin",sb);
                    FILE *w=fopen(name,"wb"); fwrite(o,1,L,w); fclose(w);
                    printf("   wrote %s head=%02x %02x %02x %02x\n",name,o[0],o[1],o[2],o[3]);
                }
            }
        }
        if(outp && nz>2000 && L>=0x1000)
            printf("cand sb=%#x L=%#x jmp=%#x nz4k=%u head=%02x %02x %02x %02x %02x %02x\n",
                   sb,L,jmp,nz,o[0],o[1],o[2],o[3],o[4],o[5]);
    }
    printf("scan done\n");
    return 0;
}
