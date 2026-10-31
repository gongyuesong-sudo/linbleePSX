/* task-042 取证探针: 捕获 RAM kernel 派发块(0x600/0x610/0x674 表)首执行时的表基/表项/jr 目标,
   及首个跳入 3C 区的块与其源。native 直跑, 不经 gdb。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "mips_jit_inc.h"

static uint32_t rd(uint32_t off){ return *(uint32_t*)(psx_ram+off); }

int main(int argc,char**argv){
    setvbuf(stdout,NULL,_IONBF,0);
    void*p=mmap((void*)0x2a1000u,64u<<20,PROT_READ|PROT_WRITE,MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS,-1,0);(void)p;
    static unsigned char rom[0x80000];
    const char*path=argc>1?argv[1]:"/home/gill/bleem/psxbios/SCPH1001.BIN";
    FILE*f=fopen(path,"rb"); if(!f||fread(rom,1,sizeof rom,f)!=(long)sizeof rom){printf("rom fail\n");return 2;} fclose(f);
    extern int jit_lib_load_rom(const unsigned char*);
    if(jit_init()) return 2;
    if(jit_lib_load_rom(rom)) return 2;
    psx_state.pc=0xBFC00000;
    int b; uint32_t prev=0; int seen600=0, seen610=0, first3c=0;
    for(b=0;b<40000;b++){
        uint32_t cur=psx_state.pc;
        if(!seen600 && cur==0x600){
            printf("FIRST-600 blk@b=%d: v0=%08x t1=%08x | [0x674]=%08x [0x670]=%08x [0x66c]=%08x [0x668]=%08x [0x664]=%08x [0x660]=%08x\n",
                   b, psx_state.r[2], psx_state.r[9], rd(0x674),rd(0x670),rd(0x66c),rd(0x668),rd(0x664),rd(0x660));
            seen600=1;
        }
        if(!seen610 && cur==0x610){
            printf("FIRST-610 blk@b=%d: v0=%08x | [v0]=%08x [0x600]=%08x [0x608]=%08x [0x610]=%08x\n",
                   b, psx_state.r[2], rd(psx_state.r[2]&0x1fffff), rd(0x600),rd(0x608),rd(0x610));
            seen610=1;
        }
        if(!first3c && (cur>>28)==3 && (prev>>28)!=3 && prev!=0){
            printf("FIRST-3C blk@b=%d: prevblk=%08x -> %08x | v0=%08x v1=%08x t0=%08x t1=%08x t9=%08x ra=%08x a2=%08x | [674]=%08x\n",
                   b, prev, cur, psx_state.r[2],psx_state.r[3],psx_state.r[8],psx_state.r[9],psx_state.r[25],psx_state.r[31],psx_state.r[6], rd(0x674));
            printf("  RAM600@3C: %08x %08x %08x %08x %08x %08x %08x %08x\n", rd(0x600),rd(0x604),rd(0x608),rd(0x60c),rd(0x610),rd(0x614),rd(0x618),rd(0x61c));
            printf("  idx: 0x674+t1*4=0x%x -> [%08x]=%08x\n", 0x674+(psx_state.r[9]<<2)&0x1fffff, (0x674+(psx_state.r[9]<<2))&0x1fffff, rd((0x674+(psx_state.r[9]<<2))&0x1fffff));
            first3c=1;
        }
        int rc=jit_run(1);
        prev=cur;
        if(rc){ printf("HALT b=%d rc=%d pc=%08x v0=%08x\n",b,rc,psx_state.pc,psx_state.r[2]); break; }
        if(first3c && b>0 && (psx_state.pc>>28)!=3) { /* 若 3C 后回到正常域也记录 */ }
    }
    /* 扫描 rootFunction 派发表(0x674 起, kernel 相对 u32 偏移): 找零槽=C2 缺口 */
    { extern int jit_lib_stop_at(void); }
    printf("done b=%d pc=%08x\n",b,psx_state.pc);
    printf("ROOTFNTAB@boot(base 0x674 kernel-rel u32): "); for(int k=0;k<24;k++){ uint32_t v=rd(0x674+k*4); if(v<0x9000u||v==0) printf("[%d]=%04x ",k,v); else {printf("[%d]=%08x?",k,v);break;} } printf("\n");
    return 0;
}
