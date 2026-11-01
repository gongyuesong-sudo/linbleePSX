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
    { const char*lg=getenv("FWDLOG"); if(lg){ FILE*lf=fopen(lg,"w"); if(lf){ extern void jit_lib_set_rclog(FILE*); jit_lib_set_rclog(lf); } } }
    FILE*gf=fopen(gp,"rb");
    if(!gf||fread(psx_ram,1,0x200000,gf)!=0x200000){ if(gf)fclose(gf); return 2; } fclose(gf);
    { const uint32_t bs[3]={0x200u,0x874u,0x674u};
      for(int i=0;i<3;i++) memcpy(tab_snap[i],psx_ram+bs[i],sizeof tab_snap[i]);
      if(getenv("FWDSLOT")){   /* H-26 楔3(task-076): 三域零槽注册"未装服务"桩(v0=-1)——断 0xB0 无限嵌套链 */
        *(uint32_t*)(psx_ram+0xB1D00u)=0x2402FFFFu; *(uint32_t*)(psx_ram+0xB1D04u)=0x03E00008u; *(uint32_t*)(psx_ram+0xB1D08u)=0u;
        unsigned nf=0;
        for(int i=0;i<3;i++){ uint32_t*tp=(uint32_t*)(psx_ram+bs[i]);
          for(int k=0;k<256;k++) if(tp[k]==0u){ tp[k]=0x800B1D00u; tab_snap[i][k]=0x800B1D00u; nf++; } }
        fprintf(stderr,"FWDSLOT filled=%u\n",nf);
        extern void jit_lib_flush_blocks(void); jit_lib_flush_blocks(); } }
  { const char*cg=getenv("H_CPUGOLD");   /* A3 楔6(task-088): gstate.hdr(GS1+npc+pc+gpr32) 装载金标 CPU */
    if(cg){ FILE*cf=fopen(cg,"rb"); if(cf){ unsigned char hdr[140];
      if(fread(hdr,1,140,cf)==140 && !memcmp(hdr,"GS1\0",4)){
        uint32_t pc,gpr[32]; pc=*(uint32_t*)(hdr+8); memcpy(gpr,hdr+12,128);
        extern void jit_lib_set_regs(const uint32_t*,uint32_t); jit_lib_set_regs(gpr,pc);
        printf("CPUGOLD pc=%08x sp=%08x ra=%08x t9=%08x s3=%08x\n",pc,gpr[29],gpr[31],gpr[25],gpr[19]);
      } else printf("CPUGOLD bad hdr\n"); fclose(cf); } else printf("CPUGOLD open fail\n"); } }
  { const char*pk=getenv("H_POKE");   /* A3 楔5(task-087): 启动期 RAM 毒值/恢复钩 addr:val[,addr:val...] */
    if(pk && psx_ram){ char buf[256]; strncpy(buf,pk,255); buf[255]=0;
      for(char*tok=strtok(buf,","); tok; tok=strtok(NULL,",")){ unsigned a,v; if(sscanf(tok,"%x:%x",&a,&v)==2 && a<0x200000u){ memcpy(psx_ram+a,&v,4); printf("POKE %08x=%08x\n",a,v); } } } }
  { const char*sl=getenv("H_SVCLOW");   /* A3 楔2(task-084): 低页服务窗默认核——0x100..0x1FF 全填 jr ra+nop thunk(宿主服务 trap 的 guest 侧等价物) */
    if(sl && psx_ram){ unsigned base=0x100u, end=0x200u; const char*a=strchr(sl,'@'); if(a){ base=(unsigned)strtoul(sl,0,16); end=(unsigned)strtoul(a+1,0,16); }
      uint32_t*w=(uint32_t*)(psx_ram+base);
      for(unsigned i=0;i<(end-base)/4;i+=2){ w[i]=0x03E00008u; w[i+1]=0x00000000u; }   /* jr ra / nop */
      printf("SVCLOW win=%x..%x jr-ra thunks=%u\n",base,end,(end-base)/8); } }
  { const char*vq=getenv("FWDVECT");   /* 批2增量1(task-066): boot 早期根call向量表窗填零槽 */
      if(vq && strlen(vq)<300){ FILE*vf=fopen(vq,"rb");
        if(vf){ uint32_t w[4096]; size_t n=fread(w,1,sizeof w,vf); fclose(vf); unsigned nf=0;
          for(size_t i=0;i+4<=n;i+=4){ uint32_t*a=(uint32_t*)(psx_ram+0x100u+i);
            if(*a==0u && w[i/4]){ *a=w[i/4]; nf++; } }
        printf("FWDVECT filled=%u win=%s\n",nf,vq); } } }
  if(getenv("FWDLiveThread")){   /* 批3实现1(task-070): 填表式活线程——队列五 sp 槽指自造栈, 帧尾 ra=SENT(顶层完成语义) */
    uint32_t spn=0x8A100u; *(uint32_t*)(psx_ram+(spn-4))=0xBFC4C1B0u;   /* saved ra=SENT */
    for(int q=0;q<5;q++) *(uint32_t*)(psx_ram+0x6CE0u+q*4)= getenv("FWDPoison")?0xDEAD0000u:spn;
    *(uint32_t*)(psx_ram+0x6CF4u)=spn; *(uint32_t*)(psx_ram+0x6CF8u)=spn-4u;
    *(uint32_t*)(psx_ram+0x85D4u)=0xBFC4C1B0u;   /* ra 恢复实体位点(golden 唯一 0xE10 槽)直写 SENT
    for(int q=0;q<8;q++) *(uint32_t*)(psx_ram+0x9400u+q*4)= (q==0?0x88000000u|spn:(q==1?8u:(q==3?0x88000000u|spn:0u)));
    *(uint32_t*)(psx_ram+0x89C44u)=0x9400u; *(uint32_t*)(psx_ram+0x89C3Cu)=0x9400u;
    fprintf(stderr,"FWDLiveThread injected sp=0x%x\n",spn); }
  if(getenv("FWDTcb")){   /* 批3楔子3(task-069): TCB 构造注入——字段清单见 docs/55: +0=sp(高16位=0x8800), +4=flags(bit3=ready) */
    struct { uint32_t off; uint32_t val; } slots[]={ {0x9C44,0x9400},{0x9C3C,0x9400},{0x9C28,0x9410},{0x9C30,0x9414},{0x9C34,0x9418} };
    uint32_t tcb[8]={0x880085D8u,0x8u,0u,0x880085D8u, 0x88008608u,0x8u,0u,0x88008608u};
    for(unsigned q=0;q<8;q++) *(uint32_t*)(psx_ram+0x9400u+q*4)=tcb[q];   /* H-26: 迁出 devsw 0x90000 冲突区... 0x9400 在低页仍安全 */
    psx_ram[0x9410]=0; psx_ram[0x9411]=0; psx_ram[0x9414]=1; psx_ram[0x9418]=1;
    if(!getenv("FWDTcbKeep")) for(unsigned q=0;q<sizeof slots/sizeof slots[0];q++) *(uint32_t*)(psx_ram+slots[q].off)=slots[q].val;
    fprintf(stderr,"FWDTcb injected\n"); }
  if(getenv("FWDTTY")){   /* H-26 楔1(task-074): TTY devsw 重装——金标 [0x64/0x68]=0xFF 哨兵回收致 printf 指针腐 */
    uint32_t fn=getenv("FWTTYSLOT")?0xB1D00u:0x90100u;   /* 0x90100 区=引擎 free-block(实测被写 04308420), 迁 0xB1D00 零页 */
    if(getenv("FWTTYSLOT")){ uint32_t*d=(uint32_t*)(psx_ram+0x90000u);
      for(int i=1;i<=9;i++) d[i]=0x80000000u|(fn+(uint32_t)(i-1)*12u); }
    for(int i=0;i<8;i++){ *(uint32_t*)(psx_ram+fn+(uint32_t)i*12u)=0x24020001u; *(uint32_t*)(psx_ram+fn+(uint32_t)i*12u+4)=0x03E00008u; *(uint32_t*)(psx_ram+fn+(uint32_t)i*12u+8)=0u; }
    *(uint32_t*)(psx_ram+0x64u)=0x80000000u|0x90000u; *(uint32_t*)(psx_ram+0x68u)=0x80000000u|0x90000u;
    if(getenv("FWDTTY2")){   /* ROM+0x14D8 实证: printf 调用点 t9=lw [sp+0x64](boot 栈缓存槽, 金标=0) */
      *(uint32_t*)(psx_ram+0x1FFDE4u)=0x80000000u|fn;        /* sp=0x801FFE00+0x64 */
      *(uint32_t*)(psx_ram+0x1FFDD4u)=0x80000000u|fn;        /* 帧 -0x2c 再保险 */
      fprintf(stderr,"FWDTTY2 stack slots <-0x%x\n",fn); }
    extern void jit_lib_flush_blocks(void); jit_lib_flush_blocks();
    fprintf(stderr,"FWDTTY devsw reinstalled\n"); }
    if(getenv("FWDEX")){   /* 批2(task-065): 自建向量跳板+派发块(与 hprobe PROBEEX 同源), 抗终态清零 */
        uint32_t vec[]={0x3C080000u,0x250805C4u,0x01000008u,0u};
        uint32_t bl0[]={0x24080200u,0x00094880u,0x01094020u,0x8D080000u,0u,0x01000008u};
        uint32_t bl1[]={0x3c080000u,0x25080874u,0x00094880u,0x01094020u,0x8D080000u,0u,0x01000008u};
        memcpy(psx_ram+0x0A0u,vec,12); vec[1]=0x250805E0u; memcpy(psx_ram+0x0B0u,vec,12);
        vec[1]=0x25080600u; memcpy(psx_ram+0x0C0u,vec,12);
        memcpy(psx_ram+0x5C4u,bl0,sizeof bl0); memcpy(psx_ram+0x5E0u,bl1,sizeof bl1); memcpy(psx_ram+0x600u,bl1,sizeof bl1);
        extern void jit_lib_flush_blocks(void); jit_lib_flush_blocks();
    }
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

int fwd_call2(uint32_t vec,uint32_t cmd,const uint32_t a0a3[4],
              uint32_t *v0,uint32_t budget,uint32_t *blocks_out,uint32_t *pc_out){
    if(!g_ready) return 3;
    uint32_t ev; if(fwd_entry(vec,cmd,&ev)) return 2;
    if(!budget) budget=200000u;
    psx_state.pc=ev;
    memset(psx_state.r,0,sizeof psx_state.r);
    psx_state.r[9]=cmd; psx_state.r[10]=vec;
    psx_state.r[29]=0x801FFE00u; psx_state.r[31]=SENT;
    { const char*gg=getenv("H_GP"); if(gg) psx_state.r[28]=(uint32_t)strtoul(gg,NULL,0); }   /* 批3楔子: gp 模式开关(0x3d 族 r28==10 门) */
    *(uint32_t*)(psx_ram+0x1FFDFCu)=SENT;
    if(a0a3){ psx_state.r[4]=a0a3[0]; psx_state.r[5]=a0a3[1]; psx_state.r[6]=a0a3[2]; psx_state.r[7]=a0a3[3]; }
    psx_state.halted=0; psx_state.halt_kind=0;
    uint32_t b=0; int srv=0; static int nst=0; static uint32_t sv_ctx[512][64]; (void)sv_ctx;
    uint32_t sp_prev=psx_state.r[29]; const char*sbw=getenv("FWDSBW");
    static const int g_svc=0; (void)g_svc;
    const int serve_vec = getenv("FWDSERVE")!=NULL;
    for(;b<budget;b++){
        /* 切换出口钩(task-072): 尸检 ra(低页 thunk 区 0xE00-0xFFF)改道宿主出口 */
        if(getenv("H_RAFIX") && psx_state.r[31]>=0xE00u && psx_state.r[31]<0x1000u){
            if(getenv("H_RAFIXLOG") && jit_rc_log){ fprintf(jit_rc_log,"RAFIX ra=%08x->SENT pc=%08x sp=%08x\n",psx_state.r[31],psx_state.pc,psx_state.r[29]); fflush(jit_rc_log); }
            psx_state.r[31]=0xBFC4C1B0u; }
        if(jit_run(1)){
            uint32_t c=psx_state.pc;
            if(getenv("H_ZEROFIX") && psx_state.halt_kind==2){   /* EMITFAIL: jr/返回目标为死值(task-074 广义零出口) */
                extern uint32_t jit_fail_pc; uint32_t fp=jit_fail_pc;
                int dead = fp<0x200000u && !*(uint32_t*)(psx_ram+ (fp&0x1FFFFFu));
                if(getenv("H_ZEROFIXLOG") && jit_rc_log){ fprintf(jit_rc_log,"ZEROFIX failpc=%08x dead=%d pc=%08x\n",fp,dead,c); fflush(jit_rc_log); }
                if(dead && b>=1000u){ psx_state.r[2]=0u; psx_state.pc=0xBFC4C1B0u; goto zfx_done; } }   /* 深度门: 浅层死跳=真失败不吞(task-075 语义防腐) */
            if(sbw && psx_state.r[29]!=sp_prev && getenv("FWDLiveThread")){   /* 切换时刻语境快照(task-070) */
                FILE*qf=fopen(sbw,"a"); if(qf){ fprintf(qf,"QDUMP sp=%08x q0=%08x q4=%08x q5=%08x q6=%08x m85d4=%08x tcb94=%08x\n",
                    psx_state.r[29], *(uint32_t*)(psx_ram+0x6CE0u),*(uint32_t*)(psx_ram+0x6CF0u),*(uint32_t*)(psx_ram+0x6CF4u),*(uint32_t*)(psx_ram+0x6CF8u),*(uint32_t*)(psx_ram+0x85D4u),*(uint32_t*)(psx_ram+0x9400u)); fclose(qf);} }
            if(sbw && psx_state.r[29]!=sp_prev){
                FILE*sbf=fopen(sbw,"a"); if(sbf){ fprintf(sbf,"SPB b=%u d=%d %08x->%08x pc=%08x ra=%08x\n",b,nst,sp_prev,psx_state.r[29],c,psx_state.r[31]); fclose(sbf);} 
                sp_prev=psx_state.r[29]; }
            if(c>=0xBFC4C1B0u&&c<0xBFC4C1C0u){
                if(c!=0xBFC4C1BCu && psx_state.r[31]>=0x80000000u && nst==0) break;  /* 深层 jal 返回点命中 SENT 窗: 不是顶层完成 */
                if(c==0xBFC4C1BCu && serve_vec && nst>0){   /* 嵌套根call返回: 恢复父上下文; 若父 ra=SENT(尾跳桩)则重放父根call 派发(子 v0 弃, 父语义即尾跳); 否则 ra 续跑 */
                    nst--; uint32_t save_v0=psx_state.r[2]; uint32_t pra=sv_ctx[nst][31];
                    uint32_t pcmd=sv_ctx[nst][9], pvec=sv_ctx[nst][10];
                    for(int i=0;i<32;i++) psx_state.r[i]=sv_ctx[nst][i];
                    (void)pcmd; (void)pvec;
                    uint32_t ppc=sv_ctx[nst][32];   /* 父保存 pc(=向量入口, 寄存器 t1/t2 已在父上下文里) — 重新派发即尾跳语义 */
                    psx_state.r[2]=save_v0;
                    psx_state.pc = (ppc>=0xA0u&&ppc<0xC0u) ? ppc
                        : (pra>=0xBFC4C1B0u&&pra<0xBFC4C1C0u) ? pra
                        : ((pra>=0x80000000u||pra>=0xBFC00000u)?pra:0x801FFE00u);
                    psx_state.halted=0; psx_state.halt_kind=0; continue; }
                break; }
            if(serve_vec && c>=0xA0u && c<0xC0u){   /* 批3楔子 v2: 嵌套根call=host 影子栈全量上下文切换 */
                uint32_t nv=psx_state.r[10]&0xF0u, nc=psx_state.r[9];
                uint32_t e2; if(nst<500 && fwd_entry(nv,nc,&e2)==0){
                    uint32_t*bk=sv_ctx[nst];
                    for(int i=0;i<32;i++) bk[i]=psx_state.r[i]; bk[32]=psx_state.pc;

                    nst++;
                    { const char*sw=getenv("FWDSPW");
                      if(sw && nst>1){ uint32_t psp=sv_ctx[nst-1][29]; if(psp<0x80000000u) fprintf(stderr,"FWDSPW bad_parent_sp=%08x d=%d cur_sp=%08x\n",psp,nst,psx_state.r[29]); } }
                    if(getenv("FWDNST")&&nst<=6) fprintf(stderr,"FWDNST d=%d %02x/%02x parent_pc=%08x parent_ra=%08x\n",nst,nv,nc,c,psx_state.r[31]);
                    memset(psx_state.r,0,sizeof psx_state.r);
                    psx_state.r[9]=nc; psx_state.r[10]=nv;
                    psx_state.r[29]=0x801FFE00u; psx_state.r[31]=0xBFC4C1BCu;
                    psx_state.pc=e2; psx_state.halted=0; psx_state.halt_kind=0;
                    if(++srv>500) break; continue; } }
            if(jit_lib_serve()){ if(++srv>200) break; continue; }
            break;
        }
    }
zfx_done: ;
    nst=0;   /* 防污染下次调用 */
    if(getenv("FWDTTYCHK")) fprintf(stderr,"TTYCHK in64=%08x in68=%08x outfn=%08x pc=%08x t9=%08x\n",
        *(uint32_t*)(psx_ram+0x64u),*(uint32_t*)(psx_ram+0x68u),
        *(uint32_t*)(psx_ram+(0x90000u+9*4)), psx_state.pc, psx_state.r[25]);
    if(v0)*v0=psx_state.r[2];
    if(blocks_out)*blocks_out=b;
    if(pc_out)*pc_out=psx_state.pc;
    uint32_t c=psx_state.pc;
    return (c>=0xBFC4C1B0u&&c<0xBFC4C1C0u)?0:1;
}
int fwd_call(uint32_t vec,uint32_t cmd,const uint32_t a0a3[4],
             uint32_t *v0,uint32_t budget,uint32_t *blocks_out){
    return fwd_call2(vec,cmd,a0a3,v0,budget,blocks_out,NULL);
}
