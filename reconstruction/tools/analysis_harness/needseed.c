#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
extern void run_decoder(void*,void*,void*,uint32_t,void*);
extern void run_back(void);
int main(int c,char**v){
    FILE*f=fopen("/home/gill/bleem/demo-v1_4.EXE","rb");
    fseek(f,0,SEEK_END);uint32_t fs=ftell(f);rewind(f);
    void*img=mmap((void*)0x400000,fs+0x4000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_FIXED,fileno(f),0);
    uint8_t*ar=mmap(0,96u<<20,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    uint32_t sb=strtoul(v[1],0,16), L=*(uint32_t*)((uint8_t*)img+sb);
    uint32_t res[2]; memset(ar,0,32u<<20);
    run_decoder(res,ar+(16u<<20),(uint8_t*)img+sb,0x45577au,(void*)run_back);
    uint32_t x=0;uint32_t*o=(uint32_t*)(ar+(16u<<20));
    for(uint32_t i=0;i<L/4;i++)x^=o[i];
    printf("sb=%#x L=%#x res=[%+#x,%#x] xorall=%#010x\n",sb,L,res[0]-(uint32_t)(ar+(16u<<20)),res[1],x);
    uint32_t tg[]={0xe2808b09,0x5a53cb18,0x8a3a69fd,0x4d709eca};
    for(int t=0;t<4;t++)printf("  need_seed[%#010x] = %#010x\n",tg[t],tg[t]^0x52414E44u^x);
    FILE*w=fopen("/home/gill/bleem/.analysis/native_dec.bin","wb");fwrite(o,1,L,w);fclose(w);
    unsigned char*b=(unsigned char*)o;
    printf("head: %02x %02x %02x %02x | %02x %02x %02x %02x\n",b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7]);
    unsigned nz=0;for(uint32_t i=0;i<L;i++)nz+=b[i]!=0;printf("nonzero=%u/%#x\n",nz,L);
    return 0;
}
