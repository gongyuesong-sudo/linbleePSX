// scan2.c — find all bleem compressed streams: native-run decoder, accept if bit source
// exhausts exactly at header (final ESI == img+sb+4) and output plausible.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/mman.h>
extern void run_decoder(void*,void*,void*,uint32_t,void*);
extern void run_back(void);
static sigjmp_buf jb; static volatile int inrun;
static void segv(int s){ (void)s; if(inrun) siglongjmp(jb,1); _exit(9); }
int main(int argc,char**argv){
    FILE*f=fopen("/home/gill/bleem/demo-v1_4.EXE","rb");
    fseek(f,0,SEEK_END);uint32_t fs=ftell(f);rewind(f);
    uint8_t*img=mmap((void*)0x400000,fs+0x4000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_FIXED|MAP_FIXED,fileno(f),0);
    if(img==MAP_FAILED){perror("img");return 2;}
    uint8_t*ar=mmap(NULL,128u<<20,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    signal(SIGSEGV,segv); signal(SIGBUS,segv);
    uint32_t lo=argc>1?strtoul(argv[1],0,16):0xfa00, hi=argc>2?strtoul(argv[2],0,16):0x55338;
    for(uint32_t sb=lo; sb+8<=hi; sb+=4){
        uint32_t L=*(uint32_t*)(img+sb), jmp=*(uint32_t*)(img+sb+4);
        if(L<0x10||L>0x400000) continue;
        if((uint32_t)jmp > 0x60000) continue;
        memset(ar,0,8u<<20); memset(ar+(16u<<20),0,L+0x40);
        uint32_t res[2]; inrun=1;
        if(sigsetjmp(jb,1)){ inrun=0; continue; }
        run_decoder(res,ar+(16u<<20),img+sb,0x45577au,(void*)run_back);
        inrun=0;
        uint32_t done = (res[1]==0x400000u+sb+4);
        uint32_t rel = res[0]-(uint32_t)(uintptr_t)(ar+(16u<<20));
        if(!done) continue;
        unsigned char*b=ar+(16u<<20); unsigned nz=0;
        for(uint32_t i=0;i<L;i++)nz+=b[i]!=0;
        printf("STREAM sb=%#07x L=%#07x(%u) jmp=%#x rel=%#x nz=%u(%u%%) head=%02x %02x %02x %02x\n",
            sb,L,L,jmp,rel,nz,nz*100/(L?L:1),b[0],b[1],b[2],b[3]);
        char nm[96]; snprintf(nm,sizeof nm,"/home/gill/bleem/rewrite/decrypted/stream_%05x_L%x.bin",sb,L);
        FILE*w=fopen(nm,"wb"); if(w){fwrite(b,1,L,w);fclose(w);}
    }
    printf("scan2 done\n"); return 0;
}
