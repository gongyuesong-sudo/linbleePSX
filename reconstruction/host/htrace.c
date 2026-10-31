/* htrace.c — H-2 诊断：SIGTRAP 指令级追踪（0x400004-0x400300 范围）
 * 用法：./htrace [trace_from_eip]；命中 int3 记录 eip+regs，恢复 dr7 续跑。
 * 只诊断不改语义：镜像与生产 host 完全一致。 */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include <signal.h>
#include <setjmp.h>
#include <ucontext.h>
#include <unistd.h>
#include <errno.h>

extern void call_engine(uint32_t entry, uint32_t cb);
#include "biosrom.h"
/* H-10b mips_jit 库接口（弱符号：旧构建行不链 mips_jit.c 时自动降级为无 JIT）*/
#pragma weak jit_lib_load_rom
#pragma weak jit_lib_trace_boot
#pragma weak jit_lib_bios_call
#pragma weak jit_lib_bios_call2
int jit_lib_load_rom(const unsigned char*rom_shadow);
int jit_lib_bios_call2(unsigned int e,unsigned int b,unsigned int*v0,unsigned int*kind,unsigned int*endpc);
int jit_lib_trace_boot(FILE*log,unsigned int entry,unsigned int max_blocks,unsigned char*win,unsigned int win_base,int backfill);
int jit_lib_bios_call(unsigned int entry_pc,unsigned int budget,unsigned int*ret_v0);
static FILE*logf;
static uint32_t bios_lo=0;
uint32_t bios_map_cb_c(void){
    if(!bios_lo){ if(mmap((void*)0x38000000u,0x80000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS,-1,0)==MAP_FAILED) return 0; bios_lo=0x38000000u; }
    void*m=mmap((void*)0xBFC00000u,0x80000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS,-1,0);
    if(m==(void*)0xBFC00000u){
        if(g_bios_rom){
            memcpy((void*)0xBFC00000u,(void*)bios_lo,0x80000);   /* 源区=512K，base+0↔窗 */
            if(logf){ fprintf(logf,"BIOS-LOADED real ROM @0xBFC00000 sha256=%s\n",g_bios_hex); fflush(logf); }
            /* H-10b：引擎跳入窗之前——宿主 JIT 预热引导链并把生成码回填窗（E9 跳板）*/
            if(getenv("H_BIOSJIT") && jit_lib_load_rom && jit_lib_trace_boot){
                if(!jit_lib_load_rom((const unsigned char*)bios_lo)){
                    if(logf){ fprintf(logf,"BIOS-JIT-WARM entry=BFC00000\n"); fflush(logf); }
                    jit_lib_trace_boot(logf?logf:stderr,0xBFC00000u,
                        (unsigned)(getenv("H_JIT_BUDGET")?atoi(getenv("H_JIT_BUDGET")):400),
                        (unsigned char*)0xBFC00000u,0xBFC00000u,1);
                } else if(logf){ fprintf(logf,"BIOS-JIT-WARM init-fail\n"); fflush(logf); }
            }
            return 0xBFC00000u;
        }
        memcpy((void*)0xBFC00000u,(void*)bios_lo,0x80000); return 0xBFC00000u;
    }
    return 0xBFC00000u;
}
uint32_t arena_bump=0x2a1000;
/* H-9a 双段 arena：seg0=64MB @0x2a1000（0xC3 滑地）；seg1=柔性高位扩展（内核 7.0 大 MAP_FIXED 触雷，禁 FIXED）*/
static const uint32_t SEG1_BASE=0x10000000u;
static uint32_t seg1_cap=0;                       /* 字节，0=无扩展段 */
static uint32_t arena_used=0;
static int32_t arena_take(uint32_t sz){           /* 记账分配；-1=耗尽 */
    uint32_t tot=0x04000000u+seg1_cap;
    if(arena_used+sz>tot) return -1;
    int32_t base=(arena_used<0x04000000u)?(int32_t)(0x2a1000u+arena_used):(int32_t)(SEG1_BASE+(arena_used-0x04000000u));
    arena_used+=sz; return base;
}
static int arena_contains(uint32_t a){
    return (a>=0x2a1000u&&a<0x2a1000u+0x04000000u)||(seg1_cap&&a>=SEG1_BASE&&a<SEG1_BASE+seg1_cap);
}
static int bios_hits=0;
static uint32_t allocs[64];static uint32_t alloc_sz[64];static int nalloc;
static uint8_t pre_copy[0x40000];
static jmp_buf die_env;
static uint32_t *cb_g;
static void dump_blocks(void){
    FILE*bf=fopen("host/build/blocks.txt","w"); if(!bf) return;
    uint32_t wins[]={0x38,0x3c,0x40,0x44,0x48,0x4c,0x50,0x54,0x58,0x5c,0x24,0x28,0x2c};
    if(!cb_g){ fclose(bf); return; }
    for(unsigned w=0;w<sizeof wins/sizeof*wins;w++){
        uint32_t b=cb_g[wins[w]/4]; if(!b||b==1) continue;
        if(!((b>=0x2a1000u&&b<0x042a1000u)||(b>=0x00400000u&&b<0x00fc0000u))) continue;  /* 只读已映射块（arena/img 真实上界）*/
        int nz=0,c3=0; unsigned char*pb=(unsigned char*)b; unsigned lim=0x40000;
        for(int q=0;q<nalloc;q++) if(allocs[q]==b && alloc_sz[q]) lim=alloc_sz[q];
        { uint32_t maxend = (b>=0x2a1000u)?0x042a1000u:0x00fc0000u;                      /* H-8b：块扫描不得越出映射尾 */
          if(b+lim>maxend) lim=maxend-b; }
        if(b+lim>0x400000u && b<0x400000u) lim=0x400000u-b;      /* 不越界进 img */
        if(b==0x3e1000u && lim<=0x40000u){
            unsigned char*pc=pre_copy; unsigned wr=0,c3w=0;
            for(unsigned i=0;i<lim;i++) if(pb[i]!=pc[i]){ wr++; if(pb[i]==0xC3)c3w++; }
            fprintf(bf,"CTRL-BLK 0x3e1000 len=%x engine_written=%d (of which C3=%d)\n",lim,wr,c3w);
            unsigned shown=0;
            for(unsigned i=0;i<lim && shown<6;i++) if(pb[i]!=pc[i]){
                fprintf(bf,"  @+%05x: pre=%02x post=%02x\n",i,pc[i],pb[i]); shown++; }
        }
        for(unsigned i=0;i<lim;i++){ unsigned v=pb[i]; if(v){nz++; if(v==0xC3)c3++;} }
        fprintf(bf,"cb+0x%02x blk=%08x len=%x nonzero=%d C3=%d\n",wins[w],b,lim,nz,c3);
        fprintf(bf,"  head:"); for(int i=0;i<48;i++) fprintf(bf,"%02x",pb[i]); fprintf(bf,"\n");
    }
    fclose(bf);
}

static uint32_t cb_snap[0x1000];
uint32_t sVA_impl(void*addr,uint32_t size,uint32_t type,uint32_t prot){
    (void)prot;
    /* VirtualFree 复用槽0（2 参）：栈上 [esp+4]=addr [esp+8]=flags(0x10001=MEM_RELEASE) */
    if((size&0xffffu)==0x0001u && (size>>16)>=0x10 && size<=0x1000ffffu){
        fprintf(stderr,"[FREE %#x flags %#x -> ok]\n",(uint32_t)(uintptr_t)addr,size);
        return 1;   /* BOOL TRUE，且 test eax,eax 走 je 分支（free 后继续）*/
    }
    (void)type;
    if(size>0x40000000u||size==0) size=0x10000;
    size=(size+0xffff)&~0xffffu; if(!size) size=0x10000;
    { int32_t r=arena_take(size); if(r<0) return (uint32_t)-1; arena_bump=(uint32_t)r+size;
      if(nalloc<64){allocs[nalloc]=(uint32_t)r; alloc_sz[nalloc]=size; nalloc++;}
      fprintf(stderr,"[VA %#x -> %#x]\n",size,(uint32_t)r);
      return (uint32_t)r; }
}
uint32_t __attribute__((stdcall)) sVA(void*a,uint32_t sz,uint32_t t,uint32_t p){return sVA_impl(a,sz,t,p);}

static uint8_t *IMG;
static volatile int tracing=0, range_lo=0x400004, range_hi=0x400300;
static volatile uint32_t last_img_eip=0;   /* H-8b：最后一次 img 内 trap 的 eip（跳转来源指令）*/
static volatile uint32_t bios_max_off=0;   /* H-9a：观测到的最大 ROM 偏移 */
static uint32_t bios_resume(ucontext_t*u,int relaxed){
    uint32_t eip=(uint32_t)u->uc_mcontext.gregs[REG_EIP];
    uint32_t esp=(uint32_t)u->uc_mcontext.gregs[REG_ESP];
    uint32_t *st=(uint32_t*)(uintptr_t)esp; int k=0; uint32_t ret=0;
    uint32_t lo = relaxed?0x400010u:0x400010u;   /* 均 ≥0x400010：img 基址 0x400000/入口 400004 不是合法 BIOS 后续续点 */
    while(k<24){ uint32_t c=st[k]; if(c>=lo&&c<0x472000){ ret=c; break; } k++; }
    if(ret){ u->uc_mcontext.gregs[REG_ESP]=esp+4*(k+1); }
    else if(relaxed && last_img_eip && last_img_eip>0x400010u && last_img_eip<0x472000u
            && IMG[last_img_eip-0x400000u]==0xC3){ ret=last_img_eip+1;   /* ret 尾派发（thunk 弹 BIOS 指针）：BIOS“返回”后续下一条 */
        if(ret==0x4002b8u && cb_g) st[1]=(uint32_t)(uintptr_t)cb_g; }    /* 后续块 stdcall arg1=控制块（同 RECOVER-29C 约定修正）*/
    else return 0;
    u->uc_mcontext.gregs[REG_EIP]=ret;
    u->uc_mcontext.gregs[REG_EAX]=0;
    u->uc_mcontext.gregs[REG_EFL]|=0x100;
    (void)eip; return ret;
}
/* logf 已前置声明（H-8a：bios_map_cb_c 需写 BIOS-LOADED） */

static void ontrap(int sig,siginfo_t*si,void*uc){
    (void)sig;ucontext_t*u=(ucontext_t*)uc;
    uint32_t eip=(uint32_t)u->uc_mcontext.gregs[REG_EIP];
    uint32_t *gr=u->uc_mcontext.gregs;
    fprintf(logf,"%08x eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x esp=%08x\n",
        eip,(uint32_t)gr[REG_EAX],(uint32_t)gr[REG_EBX],(uint32_t)gr[REG_ECX],
        (uint32_t)gr[REG_EDX],(uint32_t)gr[REG_ESI],(uint32_t)gr[REG_EDI],(uint32_t)gr[REG_ESP]);
    fflush(logf);
    /* restore dr7 off; single-step 1 instr then re-arm via trap flag? simplest:
     * clear DR7 entirely and set TF for one instruction — but TF trap = SIGTRAP too,
     * eip = next executed — perfect stepping: */
    
    if(eip>=0x400000u && eip<0x472000u){
        last_img_eip=eip;
        u->uc_mcontext.gregs[REG_EFL]|=0x100;
        if(eip>0x4002b7u){ static uint32_t seen_deep[4096]; static int ndeep=0; int q=0,news=1;  /* H-8b：深段观测锚 */
            for(;q<ndeep;q++) if(seen_deep[q]==eip){news=0;break;}
            if(news){ if(ndeep<4096) seen_deep[ndeep++]=eip; fprintf(logf,"DEEP eip=%08x\n",eip); fflush(logf); } }
    }
    else if(g_bios_rom && eip>=0xBFC00000u && eip<0xBFC80000u){
        uint32_t r=bios_resume(u,1);                       /* H-8b：BIOS 入口早期捕获（MIPS 不滑成 x86）*/
        if(r){ bios_hits++;
            if(getenv("H_BIOSJIT") && jit_lib_bios_call2){
                uint32_t v0=0,kind=0,endpc=0; int nb=jit_lib_bios_call2(eip,400,&v0,&kind,&endpc);
                fprintf(logf,"BIOS-JIT-CALL #%d entry=%08x blocks=%d kind=%u endpc=%08x v0=%08x\n",bios_hits,eip,nb,kind,endpc,v0); fflush(logf);
            }
            fprintf(logf,"BIOS-MIPS-ENTRY #%d from %08x -> %08x\n",bios_hits,eip,r); fflush(logf);
            static int cap=-1; if(cap<0){ const char*c=getenv("H_SLED_CAP"); cap=c?atoi(c):40; }
            if(bios_hits>cap){ fprintf(logf,"VERDICT: engine BIOS-CALL LOOP — PSX exec requires JIT/interpreter at BIOS vector\n"); fprintf(logf,"BIOS-MAX-OFF %08x\n",bios_max_off); dump_blocks(); fflush(logf); fclose(logf); _exit(0); }
            return; }
        fprintf(logf,"LEFT engine range at %08x\n",eip); fflush(logf);
    }
    else if(g_bios_rom && eip<0x80000u && eip>=0x40u){
        /* H-9a ROM-offset 约定：深段弹出的是 ROM 内偏移（相对 BIOS 基）→ 重映射进窗，走 MIPS 捕获路 */
        uint32_t we=0xBFC00000u+eip;
        gr[REG_EIP]=we;
        uint32_t r=bios_resume(u,1);
        if(r){ bios_hits++;
            if(getenv("H_BIOSJIT") && jit_lib_bios_call2){
                uint32_t v0=0,kind=0,endpc=0; int nb=jit_lib_bios_call2(0xBFC00000u+eip,400,&v0,&kind,&endpc);
                fprintf(logf,"BIOS-JIT-CALL #%d entry=%08x(off %08x) blocks=%d kind=%u endpc=%08x\n",bios_hits,0xBFC00000u+eip,eip,nb,kind,endpc); fflush(logf);
            }
            if(eip>bios_max_off) bios_max_off=eip;
            fprintf(logf,"BIOS-MIPS-ENTRY #%d from %08x (ROM off) -> %08x\n",bios_hits,eip,r); fflush(logf);
            static int cap=-1; if(cap<0){ const char*c=getenv("H_SLED_CAP"); cap=c?atoi(c):40; }
            if(bios_hits>cap){ fprintf(logf,"VERDICT: engine BIOS-CALL LOOP — PSX exec requires JIT/interpreter at BIOS vector\n"); dump_blocks(); fflush(logf); fclose(logf); _exit(0); }
            return; }
        fprintf(logf,"LEFT engine range at %08x\n",eip); fflush(logf);
    }
    else { fprintf(logf,"LEFT engine range at %08x\n",eip); fflush(logf); }
}
/* 恢复式 SEGV：占位派发表（mov [ecx],eax 模式被当函数调用）命中时记日志、
 * 令 EAX=0 从调用点返回，模拟 API stub 返回 0，追踪引擎真实推进深度。 */
static volatile int recovered=0; static uint32_t last_site=0, last_addr=0;
static volatile int segv_canary=0;
static volatile int die_armed=1;   /* H-8b：二次 fatal 不得复用已消费 die_env（会 main↔handler 风暴循环）*/
static void onsegv(int sig,siginfo_t*si,void*uc){
    (void)sig;ucontext_t*u=(ucontext_t*)uc;
    uint32_t eip=(uint32_t)u->uc_mcontext.gregs[REG_EIP];
    uint32_t esp=(uint32_t)u->uc_mcontext.gregs[REG_ESP];
    if(++segv_canary<=4000){ char b[64]; int n=snprintf(b,sizeof b,"[S%d eip=%08x esp=%08x addr=%p]\n",segv_canary,eip,esp,si->si_addr); if(write(2,b,n)<0){} }
    /* H-8a 防御：ESP 非映射页（跳飞进 ROM 垃圾码后栈已毁）→ 直接 fatal 上报，不在 handler 内再炸 */
    if(msync((void*)((esp)&0xfffff000u),4096,MS_SYNC)==EINVAL || msync((void*)((esp+0x60)&0xfffff000u),4096,MS_SYNC)==EINVAL){
        fprintf(logf,"SEGV-FATAL eip=%08x bad-esp=%08x\n",eip,esp); fflush(logf);
        if(!die_armed) _exit(5);
        die_armed=0; siglongjmp(die_env,1);
    }
    /* 若 eip 在 .data 占位区（0x4022a4-0x402400 等）= 被 call 的占位：
     * 从调用者栈返回：retaddr 在 esp[0] */
    if(eip>=0x4022a4&&eip<0x402500){
        last_site=eip; last_addr=(uint32_t)(uintptr_t)si->si_addr; recovered++;
        uint32_t ret=*(uint32_t*)(uintptr_t)esp;
        u->uc_mcontext.gregs[REG_EIP]=ret;                 /* 返回调用点 */
        u->uc_mcontext.gregs[REG_ESP]=esp+4;               /* pop retaddr */
        u->uc_mcontext.gregs[REG_EAX]=0;                   /* stub 返回 0 */
        if(recovered<=40) fprintf(logf,"RECOVER#%d in .data@%08x addr=%p -> ret %08x\n",
            recovered,eip,si->si_addr,ret);
        fflush(logf);
        if(recovered>400){ fprintf(logf,"recovered cap 400 — stall loop\n"); fflush(logf); _exit(4);}
        u->uc_mcontext.gregs[REG_EFL]|=0x100;
        return;   /* 继续跑（handler 返回即恢复） */
    }
    if(arena_contains(eip) || eip==0x400000 || eip==0x400004){   /* H-9a：seg1 零页调用同语义 */
        /* call 进 arena/memtable/img 头（ROM 入口期望区）：返回 0 保命继续 */
        uint32_t *st=(uint32_t*)(uintptr_t)esp; int k=0; uint32_t ret=0;
        while(k<24){ uint32_t c=st[k]; if(c>=0x400010&&c<0x472000){ ret=c; break; } k++; }
        if(ret){
            u->uc_mcontext.gregs[REG_ESP]=esp+4*(k+1);
            u->uc_mcontext.gregs[REG_EAX]=0;
            u->uc_mcontext.gregs[REG_EIP]=ret;
            recovered++;
            if(recovered<=200) fprintf(logf,"RECOVER-ARENA #%d arena-call@%08x -> ret %08x\n",recovered,eip,ret);
            fflush(logf);
            if(recovered>200){ dump_blocks(); _exit(4); }
            u->uc_mcontext.gregs[REG_EFL]|=0x100;
            return;
        }
    }
    if(eip<0x400000 && (eip>=0x1000||eip==0) && cb_g){
        /* 服务/槽调用恢复（与生产 host 同语义）：找 img 返回地址，真跑 svc 分派
         * H-8b：eip==0 = 未安装服务槽（cb+0x174 等），同走 svc 返回语义 */
        uint32_t *st=(uint32_t*)(uintptr_t)esp; int k=0; uint32_t ret=0;
        while(k<24){ uint32_t c=st[k]; if(c>=0x400000&&c<0x472000){ ret=c; break; } k++; }
        if(ret){
            uint32_t eax_in=(uint32_t)u->uc_mcontext.gregs[REG_EAX]; uint32_t r0;
            if(eax_in==0){ r0=bios_map_cb_c(); }
            else if(eax_in==0x2a0000u){ r0=0x104; }
            else if(eax_in==0x400004u || eax_in==0x400000u){ r0=eax_in; }  /* 真实函数指针：原样返回 */
            else if(eax_in==0x400004u || eax_in==0x400000u){ r0=eax_in; }  /* 真实函数指针：原样返回 */
            else { uint32_t sz=(eax_in+0xffff)&~0xffffu; if(!sz)sz=0x10000;
                   { int32_t r=arena_take(sz); r0=(r<0)?(uint32_t)-1:(uint32_t)r; if(r>=0) arena_bump=r+sz; } }
            u->uc_mcontext.gregs[REG_ESP]=esp+4*(k+1);
            u->uc_mcontext.gregs[REG_EAX]=r0;
            u->uc_mcontext.gregs[REG_EIP]=ret;
            u->uc_mcontext.gregs[REG_EDI]=(uint32_t)(uintptr_t)cb_g;   /* H-9a：宿主 C 函数污染 EDI——尾派发约定 EDI=cb 必须保持 */
            recovered++;
            if(recovered<=160) fprintf(logf,"RECOVER-SVC #%d in=%08x out=%08x ret=%08x\n",recovered,eax_in,r0,ret);
            fflush(logf);
            if(recovered>160){ fprintf(logf,"cap\n"); fflush(logf); _exit(4); }
            u->uc_mcontext.gregs[REG_EFL]|=0x100;
            return;
        }
    }
    if(cb_g && ( g_bios_rom ? (eip>=0xBFC00000u&&eip<0xBFC80000u)      /* H-8b：真 ROM 下整窗捕获（x86 滑行落在 +2..+n） */
                            : (eip==0xBFC00000u||(eip>=0x38000000u&&eip<0x38080000u)) )){
        uint32_t r=bios_resume(u,g_bios_rom?1:0);
        if(r){
            bios_hits++;
            const char *tag = g_bios_rom ? "BIOS-MIPS-ENTRY" : "BIOS-SLED";
            fprintf(logf,"%s #%d from %08x -> %08x\n",tag,bios_hits,eip,r); fflush(logf);
            static int cap=-1; if(cap<0){ const char*c=getenv("H_SLED_CAP"); cap=c?atoi(c):40; }
            if(bios_hits>cap){ fprintf(logf,"VERDICT: engine BIOS-CALL LOOP — PSX exec requires JIT/interpreter at BIOS vector\n"); dump_blocks(); fflush(logf); fclose(logf); _exit(0); }
            return;
        }
    }
    if(eip==0x40029c && cb_g){
        u->uc_mcontext.gregs[REG_EDI]=(uint32_t)(uintptr_t)cb_g;
        recovered++;
        fprintf(logf,"RECOVER-29C #%d edi<-cb\n",recovered); fflush(logf);
        if(recovered<=120){ u->uc_mcontext.gregs[REG_EFL]|=0x100; return; }
        _exit(4);
    }
    if(eip==0x40017b){
        /* 已知约定瑕疵：尺寸槽 edi 丢失 — 跳过 store，宿主补写 +0x30=8MB */
        cb_g[0x30/4]=0x800000;
        u->uc_mcontext.gregs[REG_EIP]=0x400181;
        recovered++;
        fprintf(logf,"RECOVER-17B #%d skip store, +0x30=8MB\n",recovered); fflush(logf);
        if(recovered<=80){ u->uc_mcontext.gregs[REG_EFL]|=0x100; return; }
        _exit(4);
    }
    /* img 内 store 到 0：edi/caller 丢 cb — 恢复 edi=cb 重放指令 */
    if(eip==0x400005 && cb_g){   /* 第二轮 init：EAX 丢失 → 恢复 EAX=cb */
        u->uc_mcontext.gregs[REG_EAX]=(uint32_t)(uintptr_t)cb_g;
        printf("[host] reinit-recover@5 eax<-cb\n"); fflush(stdout);
        return;
    }
    if(eip>=0x400000&&eip<0x472000 && (uint32_t)(uintptr_t)si->si_addr<0x1000 && cb_g){
        u->uc_mcontext.gregs[REG_EDI]=(uint32_t)(uintptr_t)cb_g;
        recovered++;
        if(recovered<=120) fprintf(logf,"RECOVER-STORE #%d eip=%08x edi<-cb replay\n",recovered,eip);
        fflush(logf);
        if(recovered<=120){ u->uc_mcontext.gregs[REG_EFL]|=0x100; return; }
        _exit(4);
    }
    /* H-8b：低 VA 补页方案已证伪——img 以流尺寸(0x71cbf)作基址直写 0x71c00，补页后
     * 引擎滑入宿主码以垃圾指针循环（0804b866 storm）。保持 SEGV-FATAL 干净终点。*/
    fprintf(logf,"SEGV-FATAL eip=%08x addr=%p recovered_before=%d\n",eip,si->si_addr,recovered);
    fflush(logf);
    if(!die_armed){ fprintf(stderr,"[htrace] secondary fatal — hard exit\n"); _exit(5); }
    die_armed=0; siglongjmp(die_env,1);
}

int main(int argc,char**argv){
    setvbuf(stdout,NULL,_IONBF,0);
    logf=fopen(getenv("HTRACE_LOG")?getenv("HTRACE_LOG"):"host/build/htrace.log","w"); if(!logf) logf=fopen("htrace.log","w");
    if(argc>2) range_lo=strtoul(argv[2],0,0);
    if(argc>3) range_hi=strtoul(argv[3],0,0);
    fprintf(logf,"RANGE %08x..%08x\n",range_lo,range_hi);
    const char*imgp=argc>1?argv[1]:"decrypted/stream_10a4c_L71cbf_relocated.bin";
    FILE*f=fopen(imgp,"rb");if(!f)return 1;
    fseek(f,0,SEEK_END);long sz=ftell(f);fseek(f,0,SEEK_SET);
    mmap((void*)0x2a1000,0x04000000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS,-1,0);
    memset((void*)0x2a1000,0xC3,0x04000000);   /* arena seg0 = ret 滑地（与生产一致）*/
    { const char*am=getenv("H_ARENA_MB");       /* H-9a: seg1 柔性扩展（不碰 64MB..libc 触雷区，MAP_FIXED 禁用） */
      if(am){ uint32_t extra=(uint32_t)atoi(am)*0x100000u; if(extra>64u){ extra-=64u;
        if(extra>0x30000000u) extra=0x30000000u;                       /* seg1 封顶 768MB：base+cap<0x40000000 */
        void*q=mmap((void*)SEG1_BASE,extra,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(q!=(void*)-1 && q) seg1_cap=(q==(void*)SEG1_BASE)?extra:0;   /* 内核挪址则保守禁用扩展 */
        fprintf(stderr,"[host] seg1=%p cap=%x\n",q,seg1_cap); } } }
    memset((void*)0x2a1000,0xC3,0x04000000);
    void*v=mmap((void*)0x400000,12u<<20,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS,-1,0);
    if(v!=(void*)0x400000)return 3;
    IMG=v; if(fread(IMG,1,sz,f)!=(size_t)sz)return 4; fclose(f);
    ((uint32_t*)(IMG+0x22a4))[0]=(uint32_t)(uintptr_t)&sVA;
    { int r=bios_rom_load();                       /* H-8a：BLEEM_BIOS 硬校验 */
      if(r<0){ fprintf(logf,"BLEEM_BIOS wiring fail\n"); fclose(logf); return 6; }
      if(r>0){ if(mmap((void*)0x38000000u,0x80000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS,-1,0)==MAP_FAILED){
                 fprintf(stderr,"BLEEM_BIOS error: bios source region mmap fail\n"); fclose(logf); return 6; }
               bios_lo=0x38000000u;
               bios_rom_place(bios_lo, 0); bios_rom_register("host/build/bios_sha256.txt"); } }
    uint32_t*cb=mmap((void*)0x50000,0x4000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS,-1,0);
    { extern void host_service_cb(void); cb[0x68/4]=(uint32_t)(uintptr_t)&host_service_cb; }   /* 服务指针槽 */
    cb[0x170/4]=(uint32_t)(uintptr_t)cb; cb_g=cb;


    struct sigaction sa;memset(&sa,0,sizeof sa);sa.sa_sigaction=ontrap;sa.sa_flags=SA_SIGINFO;
    sigaction(SIGTRAP,&sa,0);
    struct sigaction sv;memset(&sv,0,sizeof sv);sv.sa_sigaction=onsegv;
    sv.sa_flags=SA_SIGINFO|SA_ONSTACK;        /* H-8b：引擎栈耗尽时处理器仍需可执行（altstack），否则双 SEGV 必死 */
    { static char as_mem[2<<20]; stack_t ss; ss.ss_sp=as_mem; ss.ss_size=sizeof as_mem; ss.ss_flags=0; sigaltstack(&ss,0); }
    sigaction(SIGSEGV,&sv,0);

    /* seed TF via asm: pushf/or/popf — the handler re-arms stepping each trap */
    __asm__ volatile("pushfl; orl $0x100, (%%esp); popfl" ::: "memory");
    memcpy(cb_snap,cb,0x4000);
    memcpy(pre_copy,(void*)0x3e1000,0x40000);   /* 128KB 控制块 init 前快照 */
    if(sigsetjmp(die_env,1)==0){ call_engine(0x400004u,(uint32_t)(uintptr_t)cb); }
    else { fprintf(logf,"[htrace] fatal-recover: dump blocks and report\n"); }
    fflush(logf); dump_blocks();
    { fprintf(logf,"BIOS-MAX-OFF %08x\n",bios_max_off); }
    fprintf(logf,"returned\n");
    { FILE*df=fopen("host/build/cb_diff_htrace.txt","w");
      if(df){ for(int i=0;i<0x1000;i++) if(cb[i]!=cb_snap[i]&&cb[i]) fprintf(df,"+%04x=%08x\n",i*4,cb[i]); fclose(df);} }
    fclose(logf);
    return 0;
}
