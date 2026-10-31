/* hprobe.c — M-6 (task-062): 金标 RAM 终态上直跑 BIOS rootcall handler 的语义探针
 * 用法: PROBEF=<vec cmd phys 清单> ./hprobe [ROM] [budget]
 * 每 handler: 真 ABI 入口(t1=cmd,t2=vec,ra 哨兵) + rootcall serve 续跑
 * → 观测 BREAK 收尾/v0/停机面。HP 行 = docs/48 实证语义素材。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "mips_jit_inc.h"

void jit_lib_flush_blocks(void);
int  jit_lib_serve(void);

#define SENT 0xBFC4C1B0u
int main(int argc,char**argv){
    setvbuf(stdout,NULL,_IONBF,0);
    void*p=mmap((void*)0x2a1000u,64u<<20,PROT_READ|PROT_WRITE,MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS,-1,0); (void)p;
    static uint8_t rom[0x80000];
    const char*rp=argc>1?argv[1]:"/home/gill/bleem/psxbios/SCPH1001.BIN";
    FILE*f=fopen(rp,"rb"); if(!f||fread(rom,1,sizeof rom,f)!=(long)sizeof rom){printf("rom fail\n");return 2;} fclose(f);
    if(jit_init()) return 2;
    extern int jit_lib_load_rom(const unsigned char*);
    if(jit_lib_load_rom(rom)) return 2;
    FILE*gf=fopen("/home/gill/bleem/.analysis/golden/GOLDEN_RAM_2MB.bin","rb");
    if(!gf||fread(psx_ram,1,0x200000,gf)!=0x200000){printf("golden fail\n");return 2;} fclose(gf);
    const char*pf=getenv("PROBEF"); if(!pf){printf("PROBEF missing\n");return 2;}
    FILE*pfh=fopen(pf,"r"); if(!pfh){printf("PROBEF open fail\n");return 2;}
    uint32_t budget=argc>2?(uint32_t)strtoul(argv[2],0,10):200000u;
    char line[128]; int n=0,nbreak=0,nhalt=0,nsrv=0;
    while(fgets(line,sizeof line,pfh)){
        unsigned V,C,P; if(sscanf(line,"%x %x %x",&V,&C,&P)!=3) continue;
        uint32_t ev=(P<0x80000u)?(0xBFC00000u+P):(0x80000000u|P);   /* 表值=相对偏移 → kseg1 镜像 */
        psx_state.pc=ev;
        memset(psx_state.r,0,sizeof psx_state.r);
        psx_state.r[9]=C; psx_state.r[10]=V;
        psx_state.r[29]=0x801FFE00u; psx_state.r[31]=SENT;
        *(uint32_t*)(psx_ram+0x1FFDFCu)=SENT;
        psx_state.halted=0; psx_state.halt_kind=0;
        uint32_t b=0; int rc=0,srv=0;
        for(;b<budget;b++){
            rc=jit_run(1);
            if(rc){
                uint32_t c=psx_state.pc;
                if(c>=0xBFC4C1B0u&&c<0xBFC4C1C0u) break;              /* 哨兵 = 正常收尾 */
                if(jit_lib_serve()){ srv++; if(srv>50) break; continue; } /* syscall/rootcall 放行续跑 */
                break;
            }
        }
        uint32_t c=psx_state.pc;
        int brk=(c>=0xBFC4C1B0u&&c<0xBFC4C1C0u);
        if(brk) nbreak++; else nhalt++; nsrv+=srv;
        printf("HP vec=%x cmd=%x h=%08x rc=%d hk=%u pc=%08x blocks=%u srv=%d %s v0=%08x a0=%08x sp=%08x\n",
               V,C,ev,rc,psx_state.halt_kind,c,b,srv,brk?"BREAK":"HALT",
               psx_state.r[2],psx_state.r[4],psx_state.r[29]);
        n++;
    }
    printf("HPSUM total=%d break=%d halt=%d serve=%d\n",n,nbreak,nhalt,nsrv);
    return 0;
}
