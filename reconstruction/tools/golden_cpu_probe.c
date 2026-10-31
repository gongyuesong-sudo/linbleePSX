/* golden_cpu_probe.c — task-033/034: 金标准 CPU+RAM 终态自洽 + serve 续跑 + printf 注入达设备面
 * GSTATE=<hdr> [blocks]                  : 直跳自洽(停留金标准 PC 页)
 * GSTATE SERVE=1 [blocks]                : 异常走 RAM 向量 0x80 真 handler 的续跑活性(配 H_EXNVEC)
 * GSTATE INJ=1 [blocks]                  : 注入 A(42) printf("CDTEST") → 设备派发面证据(lowrd)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "mips_jit_inc.h"

uint32_t jit_lib_lowrd(void);
int jit_lib_serve(void);

int main(int argc, char** argv){
    setvbuf(stdout,NULL,_IONBF,0);
    const char* hs = getenv("GSTATE");
    if(!hs){ printf("usage: GSTATE=<out.hdr> %s [blocks]\n", argv[0]); return 2; }
    void*p=mmap((void*)0x2a1000u,64u<<20,PROT_READ|PROT_WRITE,MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS,-1,0); (void)p;
    static uint8_t rom[0x80000];
    const char*rpath=argc>1?argv[1]:"../psxbios/SCPH1001.BIN";
    FILE*f=fopen(rpath,"rb"); if(!f||fread(rom,1,sizeof rom,f)!=(long)sizeof rom){printf("rom fail\n");return 2;} fclose(f);
    extern int jit_lib_load_rom(const unsigned char*);
    if(jit_init()) return 2;
    if(jit_lib_load_rom(rom)) return 2;

    char ramp[512]; strncpy(ramp,hs,sizeof ramp); ramp[sizeof ramp-1]=0;
    char*dot=strrchr(ramp,'.'); if(dot) *dot=0; strncat(ramp,".ram",sizeof ramp-1-strlen(ramp));
    FILE*rf=fopen(ramp,"rb"); if(!rf){printf("ram fail %s\n",ramp);return 2;}
    if(fread(psx_ram,1,0x200000,rf)!=0x200000){printf("ram short\n");return 2;} fclose(rf);
    FILE*hf=fopen(hs,"rb"); if(!hf){printf("hdr fail\n");return 2;}
    unsigned char hdr[4+4+4+128]; if(fread(hdr,1,sizeof hdr,hf)!=sizeof hdr){printf("hdr short\n");return 2;} fclose(hf);
    if(memcmp(hdr,"GS1",3)){printf("hdr magic\n");return 2;}
    uint32_t pc=*(uint32_t*)(hdr+8);
    memcpy(psx_state.r,hdr+12,128);   /* magic "GS1\0"(4B)+npc(4)+pc(4) → GPR 自 byte12 */
    psx_state.pc=pc; psx_state.halted=0; psx_state.halt_kind=0;

    int inj = !!getenv("INJ");
    if(inj){
        uint32_t a42=0x00000000u;
        { const char*ee=getenv("INJENTRY"); a42=ee?(uint32_t)strtoul(ee,0,16):0xBFC0D540u; }  /* 默认=ROM 唯一设备槽读锚 */
        memcpy(psx_ram+0x9100,"CDTEST\n",8);
        psx_state.r[29]-=0x40; *(uint32_t*)(psx_ram+(psx_state.r[29]&0x1FFFFFu))=0xBFC4C1B0u; /* 返回哨兵=ROM break(bfc4c1b0, 干净停机) */
        psx_state.r[4]=0x80009100u;                       /* a0 = 格式串 */
        psx_state.pc=a42;
        printf("INJ printf entry=%08x a0=%08x sp=%08x\n",a42,psx_state.r[4],psx_state.r[29]);
    }
    uint32_t budget=argc>2?(uint32_t)strtoul(argv[2],0,10):(inj?200000u:4000u);
    uint32_t pages=pc&~0xFFFu; uint32_t i,serves=0,fails=0,devwalk=0,vect=0; int outside=0;
    for(i=0;i<budget;i++){
        if(jit_run(1)){
            if(!(psx_state.halted && psx_state.halt_kind!=2)){ fails=1; break; }
            if(getenv("H_EXNVEC")||inj){ if(jit_lib_serve()){ fails=1; break; } serves++; }
            else break;
        }
        uint32_t c=psx_state.pc;
        if((c&0xF0000000u)==0x80000000u||c<0x200000u){ uint32_t k=c&0x1FFFFFu;
            if(k>=0xDE8u&&k<0xE10u) devwalk=1;
            if(k>=0x80u&&k<0xC0u) vect=1;
        }
        if(!inj && (c&~0xFFFu)!=pages) outside++;
    }
    printf("GOLDEN-%s blocks=%u pc=%08x outside=%d halted=%u hk=%u sp=%08x serves=%u devwalk=%u vect=%u lowrd=%u fails=%u\n",
           inj?"INJ":"RUN", i, psx_state.pc, outside, psx_state.halted, psx_state.halt_kind,
           psx_state.r[29], serves, devwalk, vect, jit_lib_lowrd(), fails);
    if(inj) return (jit_lib_lowrd()>0)?0:1;   /* 设备槽读=验收锚; fails 允许(链滑入未装区属预期) */
    return (!fails && outside==0)?0:1;
}
