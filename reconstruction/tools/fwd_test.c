/* fwd_test.c — 转发器 API 验收驱动（task-064）
 * FWDLIST=<vec cmd 清单> ./fwd_test [budget]
 * 输出: FWD vec=.. cmd=.. rc=.. v0=.. blocks=.. ; FWDSUM ok=.. ctx=.. miss=.. */
#include <stdio.h>
#include <stdlib.h>
#include "fwd.h"

int main(int argc,char**argv){
    setvbuf(stdout,NULL,_IONBF,0);
    const char*pf=getenv("FWDLIST"); if(!pf){printf("FWDLIST missing\n");return 2;}
    if(fwd_init(NULL,NULL)){printf("fwd_init fail\n");return 2;}
    uint32_t budget=argc>1?(uint32_t)strtoul(argv[1],0,10):0u;
    FILE*f=fopen(pf,"r"); if(!f){printf("list open fail\n");return 2;}
    char line[128]; int ok=0,ctx=0,miss=0;
    while(fgets(line,sizeof line,f)){
        unsigned V,C; if(sscanf(line,"%x %x",&V,&C)!=2) continue;
        uint32_t v0=0,blk=0; int rc=fwd_call(V,C,NULL,&v0,budget,&blk);
        printf("FWD vec=%x cmd=%x rc=%d v0=%08x blocks=%u\n",V,C,rc,v0,blk);
        if(rc==0)ok++; else if(rc==1)ctx++; else miss++;
    }
    printf("FWDSUM ok=%d ctx=%d miss=%d\n",ok,ctx,miss);
    return 0;
}
