/* fwd_test.c — 转发器 API 验收驱动（task-064）
 * FWDLIST=<vec cmd 清单> ./fwd_test [budget]
 * 输出: FWD vec=.. cmd=.. rc=.. v0=.. blocks=.. ; FWDSUM ok=.. ctx=.. miss=.. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fwd.h"

int main(int argc,char**argv){
    setvbuf(stdout,NULL,_IONBF,0);
    const char*pf=getenv("FWDLIST"); if(!pf && !getenv("FWDMAIN")){printf("FWDLIST missing\n");return 2;}
    if(fwd_init(NULL,NULL)){printf("fwd_init fail\n");return 2;}
    uint32_t budget=argc>1?(uint32_t)strtoul(argv[1],0,10):0u;
    { const char*mr=getenv("FWDMAIN");   /* A3 楔1(task-083): C2 主运行(fwd 语境直入 0x400004, 六件套正交) */
      if(mr){ uint32_t mb=(uint32_t)strtoul(mr,0,10); if(!mb) mb=20000000u;
        const char*rl=getenv("FWDRCLOG"); FILE*rf=rl?fopen(rl,"w"):NULL;
        if(rf) { extern void jit_lib_set_rclog(FILE*); jit_lib_set_rclog(rf); }
        uint32_t v0=0,kind=0,endpc=0;
        extern int jit_lib_bios_call2(uint32_t,uint32_t,uint32_t*,uint32_t*,uint32_t*);
        const char*cg=getenv("H_CPUGOLD");
        uint32_t entry=0x400004u;
        if(cg){ FILE*h2=fopen(cg,"rb"); if(h2){ unsigned char hdr[12]; if(fread(hdr,1,12,h2)==12 && !memcmp(hdr,"GS1\0",4)) entry=*(uint32_t*)(hdr+8); fclose(h2); } }
        extern unsigned char* jit_lib_ram_pub(void);
        { const char*sg=getenv("FWDSEGS");                              /* A3 楔8(task-090): 多段回放+宿主最小核 */
          if(sg){ int nseg=atoi(sg);
            for(int s=0;s<nseg;s++){
              int rcs=jit_lib_bios_call2(entry,mb,&v0,&kind,&endpc);
              printf("SEG %d rc=%d kind=%u v0=%08x endpc=%08x\n",s,rcs,kind,v0,endpc);
              if((endpc>>20u)!=0x44u) break;                            /* 非 HOSTRET 终止 */
              { unsigned char*ram=jit_lib_ram_pub(); uint32_t*w=(uint32_t*)(ram+0x83C58u);
                if(*w>0u) (*w)--;
                { const char*sp2=getenv("H_SEGP");   /* A3 楔9(task-091): 段间宿主注入 RAM 写 */
                  if(sp2 && ram){ char pb[256]; strncpy(pb,sp2,255); pb[255]=0;
                    for(char*tok=strtok(pb,","); tok; tok=strtok(NULL,",")){ unsigned a,v; if(sscanf(tok,"%x:%x",&a,&v)==2 && a<0x200000u) memcpy(ram+a,&v,4); } }
                printf("HOSTCORE 83c58-->%08x reenter=%08x\n",*w,entry); } }
            }
            printf("MAINRUN rc=-1 kind=0 v0=%08x endpc=%08x budget=%u\n",v0,endpc,mb);
            if(rf) fclose(rf);
            return 0; } }
        int rc=jit_lib_bios_call2(entry,mb,&v0,&kind,&endpc);
        printf("MAINRUN rc=%d kind=%u v0=%08x endpc=%08x budget=%u\n",rc,kind,v0,endpc,mb);
        { const char*rd=getenv("H_RAMDUMP");   /* 楔7: 终态 RAM 词观测(递减者普查) */
          if(rd){ extern unsigned char* jit_lib_ram_pub(void); unsigned char*ram=jit_lib_ram_pub();
            char bb[256]; strncpy(bb,rd,255); bb[255]=0;
            for(char*tok=strtok(bb,","); tok; tok=strtok(NULL,",")){ unsigned a; if(sscanf(tok,"%x",&a)==1 && ram)
              printf("RDUMP %08x=%08x\n",a,*(uint32_t*)(ram+(a&0x1FFFFFu))); } } }
        { extern unsigned jit_lib_cop0(unsigned);
          uint32_t st=jit_lib_cop0(12),ca=jit_lib_cop0(13),epc=jit_lib_cop0(14),ba=jit_lib_cop0(8);
          printf("COP0 status=%08x cause=%08x epc=%08x badva=%08x exl=%u exc_code=%u\n",
                 st,ca,epc,ba,(st>>30)&1u,(ca>>2)&31u); }
        if(rf) fclose(rf);
        return 0; } }
    FILE*f=fopen(pf,"r"); if(!f){printf("list open fail\n");return 2;}
    { const char*cf=getenv("FWDCREATE");   /* H-27 楔3(task-079): 前置建语境序列(设备完备下复测) */
      if(cf){ FILE*g=fopen(cf,"r"); if(g){ char cl[128];
          while(fgets(cl,sizeof cl,g)){ unsigned CV,CC; if(sscanf(cl,"%x %x",&CV,&CC)!=2) continue;
            uint32_t cv0=0,cbk=0,cp=0; int crc=fwd_call2(CV,CC,NULL,&cv0,budget,&cbk,&cp);
            printf("CREQ vec=%x cmd=%x rc=%d v0=%08x blocks=%u haltpc=%08x\n",CV,CC,crc,cv0,cbk,cp); }
          fclose(g);} else printf("create list open fail\n"); } }
    char line[128]; int ok=0,ctx=0,miss=0;
    while(fgets(line,sizeof line,f)){
        unsigned V,C; if(sscanf(line,"%x %x",&V,&C)!=2) continue;
        uint32_t v0=0,blk=0,pc=0; int rc=fwd_call2(V,C,NULL,&v0,budget,&blk,&pc);
        printf("FWD vec=%x cmd=%x rc=%d v0=%08x blocks=%u haltpc=%08x\n",V,C,rc,v0,blk,pc);
        if(rc==0)ok++; else if(rc==1)ctx++; else miss++;
    }
    printf("FWDSUM ok=%d ctx=%d miss=%d\n",ok,ctx,miss);
    return 0;
}
