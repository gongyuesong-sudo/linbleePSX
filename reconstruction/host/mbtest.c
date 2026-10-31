/* mbtest.c — 单块直调观察器：支持只跑前 N 条指令（task-021 调试） */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "mips_jit_inc.h"
int main(int argc,char**argv){
    setvbuf(stdout,NULL,_IONBF,0);
    if(jit_init_dbg()){ printf("init fail\n"); return 1; }
    g_dbg_emit=0;
    int N=(argc>1)?atoi(argv[1]):16;
    uint32_t P[]={0x24020005u,0x24091234u,0xAC092000u,0x8C0A2000u,0x01223021u,
                  0x900B2004u,0x900C2007u,0x300D000Fu,0x000A3880u,0x000A40C3u,
                  0x00000000u,0x14400001u,0x24190099u,0x00000000u,0x00000000u,0x00000000u};
    for(int i=0;i<N;i++) jh_sw(0x100000+i*4,P[i]);
    jh_sw(0x2000,0xAABBCCDD); for(int i=0;i<5;i++) jh_sb(0x2004+i,i+1);
    uint8_t *code; uint32_t len;
    if(!emit_block_dbg_wrap(0x100000,&code,&len)){ printf("emit fail\n"); return 1; }
    psx_state.pc=0x100000;
    extern void trampoline_call(void*); trampoline_call(code);
    printf("after N=%d: pc=%#x v0=%u t2=%#x a2=%#x t3=%u t4=%u a3=%#x t0=%#x t9=%#x mem=%#x\n",
        N,psx_state.pc,psx_state.r[2],psx_state.r[10],psx_state.r[6],psx_state.r[11],psx_state.r[12],
        psx_state.r[7],psx_state.r[8],psx_state.r[25],jh_lw(0x2000));
    return 0;
}
