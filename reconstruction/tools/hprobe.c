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
    if(getenv("PROBEEX")){   /* v2(task-063): 自建向量跳板+派发块(BOOT16 词形), 表数据用金标 */
        uint32_t vec[]={0x3C080000u,0x250805C4u,0x01000008u,0u};
        uint32_t bl0[]={0x24080200u,0x00094880u,0x01094020u,0x8D080000u,0u,0x01000008u};   /* A0: [0x200+c*4] */
        uint32_t bl1[]={0x3C080000u,0x25080874u,0x00094880u,0x01094020u,0x8D080000u,0u,0x01000008u}; /* B0: [0x874+c*4] */
        memcpy(psx_ram+0x0A0u,vec,12); vec[1]=0x250805E0u; memcpy(psx_ram+0x0B0u,vec,12);
        vec[1]=0x25080600u; memcpy(psx_ram+0x0C0u,vec,12);
        memcpy(psx_ram+0x5C4u,bl0,sizeof bl0); memcpy(psx_ram+0x5E0u,bl1,sizeof bl1); memcpy(psx_ram+0x600u,bl1,sizeof bl1);
        vec[1]=0u; vec[0]=0x03E00008u; memcpy(psx_ram+0x620u,vec,8);   /* 0xC0 块后旧 cop0 槽→jr ra 无害化 */
        extern void jit_lib_flush_blocks(void); jit_lib_flush_blocks();
        printf("PROBEEX dispatch rebuilt\n");
    }
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
                if(jit_lib_serve()){ srv++; if(srv>200) break; continue; } /* syscall/rootcall 放行续跑 */
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
