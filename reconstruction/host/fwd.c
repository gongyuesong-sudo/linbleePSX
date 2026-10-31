/* fwd.c — H-25 批1 转发器 API 实现（task-064）
 * 单 TU 内嵌 JIT 库(mips_jit_inc.h 由 tools/gen_inc.py 生成), 对外仅暴露 fwd.h。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "mips_jit_inc.h"
#include "fwd.h"

void jit_lib_flush_blocks(void);
int  jit_lib_serve(void);
extern int jit_lib_load_rom(const unsigned char*);

#define SENT 0xBFC4C1B0u
static int g_ready=0;
static uint32_t tab_snap[3][256];  /* 安装态表快照(task-064): 真 handler 会清活表槽, 解析须抗副作用; 0xB0 族 cmd 达 0x5B */

static uint32_t vec_base(uint32_t vec){
    switch(vec){ case 0xA0u: return 0x200u; case 0xB0u: return 0x874u; case 0xC0u: return 0x674u; }
    return 0xFFFFFFFFu;
}

int fwd_init(const char *bios_path,const char *golden_ram_path){
    static uint8_t rom[0x80000];
    const char *bp=bios_path?bios_path:"/home/gill/bleem/psxbios/SCPH1001.BIN";
    const char *gp=golden_ram_path?golden_ram_path:"/home/gill/bleem/.analysis/golden/GOLDEN_RAM_2MB.bin";
    void*p=mmap((void*)0x2a1000u,64u<<20,PROT_READ|PROT_WRITE,MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS,-1,0); (void)p;
    FILE*f=fopen(bp,"rb"); if(!f||fread(rom,1,sizeof rom,f)!=(long)sizeof rom) return 2; fclose(f);
    if(jit_init()) return 2;
    if(jit_lib_load_rom(rom)) return 2;
    FILE*gf=fopen(gp,"rb");
    if(!gf||fread(psx_ram,1,0x200000,gf)!=0x200000){ if(gf)fclose(gf); return 2; } fclose(gf);
    { const uint32_t bs[3]={0x200u,0x874u,0x674u};
      for(int i=0;i<3;i++) memcpy(tab_snap[i],psx_ram+bs[i],sizeof tab_snap[i]); }
    g_ready=1; return 0;
}

int fwd_entry(uint32_t vec,uint32_t cmd,uint32_t *pc_out){
    if(!g_ready) return 3;
    uint32_t b=vec_base(vec); if(b==0xFFFFFFFFu||cmd>=256u) return 2;
    uint32_t raw=tab_snap[(b==0x200u)?0:((b==0x874u)?1:2)][cmd];   /* 快照解析: 抗 handler 副作用 */
    if(raw==0u) return 2;
    *pc_out=(raw<0x80000u)?(0xBFC00000u+raw):(0x80000000u|raw);
    return 0;
}

int fwd_call(uint32_t vec,uint32_t cmd,const uint32_t a0a3[4],
             uint32_t *v0,uint32_t budget,uint32_t *blocks_out){
    if(!g_ready) return 3;
    uint32_t ev; if(fwd_entry(vec,cmd,&ev)) return 2;
    if(!budget) budget=200000u;
    psx_state.pc=ev;
    memset(psx_state.r,0,sizeof psx_state.r);
    psx_state.r[9]=cmd; psx_state.r[10]=vec;
    psx_state.r[29]=0x801FFE00u; psx_state.r[31]=SENT;
    *(uint32_t*)(psx_ram+0x1FFDFCu)=SENT;
    if(a0a3){ psx_state.r[4]=a0a3[0]; psx_state.r[5]=a0a3[1]; psx_state.r[6]=a0a3[2]; psx_state.r[7]=a0a3[3]; }
    psx_state.halted=0; psx_state.halt_kind=0;
    uint32_t b=0; int srv=0;
    for(;b<budget;b++){
        if(jit_run(1)){
            uint32_t c=psx_state.pc;
            if(c>=0xBFC4C1B0u&&c<0xBFC4C1C0u) break;
            if(jit_lib_serve()){ if(++srv>200) break; continue; }
            break;
        }
    }
    if(v0)*v0=psx_state.r[2];
    if(blocks_out)*blocks_out=b;
    uint32_t c=psx_state.pc;
    return (c>=0xBFC4C1B0u&&c<0xBFC4C1C0u)?0:1;
}
