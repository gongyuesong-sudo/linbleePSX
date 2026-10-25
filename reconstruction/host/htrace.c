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
#include <ucontext.h>
#include <unistd.h>

extern void call_engine(uint32_t entry, uint32_t cb);
static uint32_t bios_lo=0;
uint32_t bios_map_cb_h(void){
    if(!bios_lo){ if(mmap((void*)0x38000000u,0x80000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS,-1,0)==MAP_FAILED) return 0; bios_lo=0x38000000u; }
    void*m=mmap((void*)0xBFC00000u,0x80000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS,-1,0);
    if(m==(void*)0xBFC00000u){ memcpy((void*)0xBFC00000u,(void*)bios_lo,0x80000); return 0xBFC00000u; }
    return bios_lo;
}
static uint32_t arena_bump=0x2a1000;
static uint32_t allocs[64];static int nalloc=0;
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
    uint32_t r=arena_bump; arena_bump+=size;
    if(arena_bump>=0x2a1000+0x04000000) return (uint32_t)-1;
    if(nalloc<64)allocs[nalloc++]=r;
    fprintf(stderr,"[VA %#x -> %#x]\n",size,r);
    return r;
}
uint32_t __attribute__((stdcall)) sVA(void*a,uint32_t sz,uint32_t t,uint32_t p){return sVA_impl(a,sz,t,p);}

static uint8_t *IMG;
static uint32_t *cb_g;
static volatile int tracing=0, range_lo=0x400004, range_hi=0x400300;
static FILE*logf;

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
    
    if(eip>=0x400000u && eip<0x472000u) u->uc_mcontext.gregs[REG_EFL]|=0x100;
    else { fprintf(logf,"LEFT engine range at %08x\n",eip); fflush(logf); }
}
/* 恢复式 SEGV：占位派发表（mov [ecx],eax 模式被当函数调用）命中时记日志、
 * 令 EAX=0 从调用点返回，模拟 API stub 返回 0，追踪引擎真实推进深度。 */
static volatile int recovered=0; static uint32_t last_site=0, last_addr=0;
static void onsegv(int sig,siginfo_t*si,void*uc){
    (void)sig;ucontext_t*u=(ucontext_t*)uc;
    uint32_t eip=(uint32_t)u->uc_mcontext.gregs[REG_EIP];
    uint32_t esp=(uint32_t)u->uc_mcontext.gregs[REG_ESP];
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
    if(eip==0x40017b){
        /* 调用方把 cb 传丢（edi=0）：恢复 edi=cb 并让 [edi+0x30] 写入完成 */
        u->uc_mcontext.gregs[REG_EDI]=(uint32_t)(uintptr_t)cb_g;
        recovered++;
        fprintf(logf,"RECOVER-17B #%d edi<-cb esp=%08x\n",recovered,(uint32_t)u->uc_mcontext.gregs[REG_ESP]); fflush(logf);
        if(recovered<=80){ u->uc_mcontext.gregs[REG_EFL]|=0x100; return; }
        _exit(4);
    }
    fprintf(logf,"SEGV-FATAL eip=%08x addr=%p recovered_before=%d\n",eip,si->si_addr,recovered);
    fflush(logf);fclose(logf);_exit(3);
}

int main(int argc,char**argv){
    setvbuf(stdout,NULL,_IONBF,0);
    logf=fopen("host/build/htrace.log","w"); if(!logf) logf=fopen("htrace.log","w");
    if(argc>2) range_lo=strtoul(argv[2],0,0);
    const char*imgp=argc>1?argv[1]:"decrypted/stream_10a4c_L71cbf_relocated.bin";
    FILE*f=fopen(imgp,"rb");if(!f)return 1;
    fseek(f,0,SEEK_END);long sz=ftell(f);fseek(f,0,SEEK_SET);
    mmap((void*)0x2a1000,0x04000000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS,-1,0);
    memset((void*)0x2a1000,0xC3,0x04000000);
    void*v=mmap((void*)0x400000,12u<<20,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS,-1,0);
    if(v!=(void*)0x400000)return 3;
    IMG=v; if(fread(IMG,1,sz,f)!=(size_t)sz)return 4; fclose(f);
    ((uint32_t*)(IMG+0x22a4))[0]=(uint32_t)(uintptr_t)&sVA;
    uint32_t*cb=mmap(NULL,0x4000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    cb[0x170/4]=(uint32_t)(uintptr_t)cb+0x200; cb_g=cb;
    cb[0]=(uint32_t)(uintptr_t)&bios_map_cb_h;   /* slot0 BIOS-map callback */

    struct sigaction sa;memset(&sa,0,sizeof sa);sa.sa_sigaction=ontrap;sa.sa_flags=SA_SIGINFO;
    sigaction(SIGTRAP,&sa,0);
    struct sigaction sv;memset(&sv,0,sizeof sv);sv.sa_sigaction=onsegv;sv.sa_flags=SA_SIGINFO;
    sigaction(SIGSEGV,&sv,0);

    /* seed TF via asm: pushf/or/popf — the handler re-arms stepping each trap */
    __asm__ volatile("pushfl; orl $0x100, (%%esp); popfl" ::: "memory");
    call_engine(0x400004u,(uint32_t)(uintptr_t)cb);
    fprintf(logf,"returned\n");fclose(logf);
    return 0;
}
