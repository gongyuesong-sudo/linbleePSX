#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "mips_jit_inc.h"
int main(int argc,char**argv){
    setvbuf(stdout,NULL,_IONBF,0);
    void*p=mmap((void*)0x2a1000u,64u<<20,PROT_READ|PROT_WRITE,MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS,-1,0); (void)p;
    static uint8_t rom[0x80000];
    const char*path=argc>1?argv[1]:"../psxbios/SCPH1001.BIN";
    FILE*f=fopen(path,"rb"); if(!f||fread(rom,1,sizeof rom,f)!=(long)sizeof rom){printf("rom fail\n");return 2;} fclose(f);
    extern int jit_lib_load_rom(const unsigned char*);
    if(jit_init()) return 2;
    if(jit_lib_load_rom(rom)) return 2;
    uint32_t v0=0,kind=0,endpc=0;
    uint32_t budget=argc>2?(uint32_t)strtoul(argv[2],0,10):20000u;
    jit_lib_bios_call2(0xBFC00000u,budget,&v0,&kind,&endpc);
    printf("FINAL pc=%08x halted=%u hk=%u\n",psx_state.pc,psx_state.halted,psx_state.halt_kind);
    return 0;
}
