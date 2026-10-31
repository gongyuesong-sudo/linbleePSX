// boot2.c — faithful native runner for bleem! demo v1.4 self-extractor on Linux (-m32).
//
// Memory model (reverse-engineered; matches original Win95 process exactly):
//   0x400000: flat file view (MapViewOfFile-style, whole file incl. appended 0xFA00..EOF)
//   slot0: stub reads [esp+0x304] = 0x4010c4 (saved code addr) → delta = 0x400000
//   stage-1: esi=[esp+4]+0xfa00 → 0x45FA00 (bootstrap stub); EDI → 0x40FA0C (module table)
//   API dispatch base: 0x4022a4+delta; module dest rvas 0x60/0x8e8/0xe18/0x5a244 → view coords
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/mman.h>

static uint8_t *VIEW;
static uint32_t FSIZE;

#define MAXREG 32
static uint32_t reg_addr[MAXREG]; static uint32_t reg_size[MAXREG]; static int nreg;
static void dump_regions(void){
    for(int i=0;i<nreg;i++){
        char nm[160]; snprintf(nm,sizeof nm,"/home/gill/bleem/.analysis/captured/region_%d_%08x_%x.bin",i,reg_addr[i],reg_size[i]);
        FILE *w=fopen(nm,"wb"); if(!w) continue;
        fwrite((void*)(uintptr_t)reg_addr[i],1,reg_size[i],w); fclose(w);
        printf("[cap] %s\n",nm);
    }
    fflush(stdout);
}

extern void stub_back(void);
extern void sVP(void); extern void sVA(void); extern void sGS(void); extern void sGL(void); extern void sGUn(void); extern void sGF(void);
extern void tramp_0(void);
void tramp_record(int off){
    uint32_t *base=(uint32_t*)&off;
    uint32_t esp_saved=base[7];
    uint32_t *args=(uint32_t*)(esp_saved+4);
    printf("[apiT] slot=%d a0=%08x a1=%08x a2=%08x\n",off,args[0],args[1],args[2]);
    for(int i=0;i<3;i++){
        uint32_t v=args[i];
        if(v>0x400000&&v<0x4c0000){ char *s=(char*)(uintptr_t)v; int ok=1;
            for(int k=0;k<40&&s[k];k++) if((unsigned char)s[k]<32||(unsigned char)s[k]>126){ok=0;break;}
            if(ok&&s[0]) printf("   arg%d=\"%s\"\n",i,s);
        }
    }
    fflush(stdout); }

static uint32_t bump=0x2a1000;
uint32_t cVA(void*a,uint32_t sz,uint32_t t,uint32_t pr){
    printf("[api] VA a=%p sz=%#x t=%#x pr=%#x\n",a,sz,t,pr); fflush(stdout);
    uint32_t m=(sz+0xfff)&~0xfff; uint32_t r=bump; bump+=m+0x1000;
    if(bump>0x3f000000){ printf("[api] arena exhausted\n"); return 0xffffffff; }
    if(nreg<MAXREG){reg_addr[nreg]=r;reg_size[nreg]=m;nreg++;}
    return r;
}
static uint32_t shim_GetVersionExA(uint32_t p){
    uint32_t*size=0; // OSVERSIONINFOA at p: {size,major,minor,build,platform,csd[128]}
    uint32_t*q=(uint32_t*)(uintptr_t)p; uint32_t n=q[0];
    printf("[api] GetVersionExA(%#x) size=%u\n",p,n); fflush(stdout);
    if(n>=0x1c||n==0x124||n==0x14){ q[1]=4; q[2]=0; q[3]=0x535; q[4]=2; }
    memcpy((void*)(uintptr_t)(p+20),"95 4.00 A\0",10);
    return 1;
}
static uint32_t shim_VirtualProtect(void*lp,void*tab,uint32_t n){
    printf("[api] VP lp=%p table=%p n=%#x\n",lp,tab,n); fflush(stdout);
    *(uint32_t*)(uintptr_t)0x2a0000u = (uint32_t)(uintptr_t)shim_GetVersionExA;  // stub: call [lp] next
    return 0x104;                                                                 // old protect = PAGE_EXECUTE_READWRITE
}
uint32_t cGS(uint32_t h){ printf("[api] GlobalSize(%#x)\n",h); return 0x400000; }
static uint8_t *gmem;
uint32_t cGL(uint32_t h){ printf("[api] GlobalLock(%#x)\n",h); return (uint32_t)(uintptr_t)gmem; }
static uint32_t shim_GlobalUnlock(uint32_t h){ return 0; }
static uint32_t shim_GlobalFree(uint32_t h){ return 0; }
static uint32_t shim_Generic(void){ printf("[api?] generic\n"); fflush(stdout); return 1; }
static uint32_t shim_MessageBoxA(uint32_t h,char*t,char*c,uint32_t u){ printf("[msg] %s | %s\n",c?c:"?",t?t:"?"); dump_regions(); _exit(0); return 2; }
static uint32_t shim_ExitProcess(uint32_t c){ printf("[exit] %u\n",c); dump_regions(); _exit(0); }
static void onalarm(int s){ (void)s; printf("[boot] alarm dump\n"); dump_regions(); _exit(0); }
#include <execinfo.h>
static void onsegv(int sig, siginfo_t *si, void *uc){
    (void)sig;
    ucontext_t *u=(ucontext_t*)uc;
    uint32_t eip=(uint32_t)u->uc_mcontext.gregs[REG_EIP];
    uint32_t esp=(uint32_t)u->uc_mcontext.gregs[REG_ESP];
    uint32_t eax=(uint32_t)u->uc_mcontext.gregs[REG_EAX];
    uint32_t ebx=(uint32_t)u->uc_mcontext.gregs[REG_EBX];
    uint32_t esi=(uint32_t)u->uc_mcontext.gregs[REG_ESI];
    uint32_t edi=(uint32_t)u->uc_mcontext.gregs[REG_EDI];
    printf("[SEGV] eip=%08x esp=%08x eax=%08x ebx=%08x esi=%08x edi=%08x at=%p\n",eip,esp,eax,ebx,esi,edi,(void*)si->si_addr);
    uint32_t *sp=(uint32_t*)(uintptr_t)esp;
    for(int i=0;i<12;i++) printf("   [%02d]=%08x\n",i,sp[i]);
    dump_regions(); _exit(0);
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    mkdir("/home/gill/bleem/.analysis/captured",0775);
    FILE *f=fopen("/home/gill/bleem/demo-v1_4.EXE","rb");
    if(!f){perror("exe");return 1;}
    fseek(f,0,SEEK_END); FSIZE=ftell(f); rewind(f);
    uint8_t *raw=malloc(FSIZE); if(fread(raw,1,FSIZE,f)!=FSIZE) return 2; fclose(f);

    gmem=mmap(NULL,0x400000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    void *sb=mmap((void*)0x2a0000,0x1000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS,-1,0);(void)sb;
    void *ar=mmap((void*)0x2a1000,0x3d000000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(ar==MAP_FAILED) perror("arena");

    VIEW=mmap((void*)0x400000,8u<<20,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS,-1,0);
    if(VIEW==MAP_FAILED){perror("view");return 3;}
    memcpy(VIEW,raw,FSIZE);
    printf("[boot] view at %p size %u\n",(void*)VIEW,FSIZE);
    signal(SIGALRM,onalarm); struct sigaction sa; memset(&sa,0,sizeof sa); sa.sa_sigaction=onsegv; sa.sa_flags=SA_SIGINFO; sigaction(SIGSEGV,&sa,0); sigaction(SIGBUS,&sa,0); alarm(60);

    // Win95 PEB gate: stub proceeds only when PEB-read==0. Patch the executed copy.
    memcpy(VIEW+0x5558e,"\x31\xc0\x90\x90\x90\x90",6);   // mov eax,fs:[0x20] → xor eax,eax

    uint32_t *apit=(uint32_t*)(VIEW+0x11e0);   // natural landing: 0x4022a4 + (-0x10c4) — NO remap
    // Exact stack choreography (from objdump of 0x55338..0x553c9):
    //   entry esp = S; slot0 = *(S+0x304); frame after sub = S-0x304;
    //   exit esp = frame+0x308 = S+4; RET pops *(S+4) → send it to loader tail 0x4010c4
    extern void sX1(void);
    void *x2a=mmap((void*)0x10482000,0x2000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS,-1,0);(void)x2a;
    *(uint32_t*)(uintptr_t)0x104822d8=(uint32_t)(uintptr_t)sX1;  // 2nd-stage table slot -> resolver shim
    void *stkarea=mmap((void*)0x10000000,0x100000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS,-1,0);(void)stkarea;
    uint32_t S=0x10080000+0x2a8;
    uint32_t *stk=(uint32_t*)0x10080000;
    for(int i=0;i<0x200;i++) stk[i]=0;
    extern void sX2(void);
    *(uint32_t*)(S+4)=0x4010c4;                         // final RET lands in loader tail (shims dump)
    // [esp+0x10] slot at FUN_00055895 (entry pushes 13 regs + sub 0x2d4, no further pushes
    // before the inner call): slot_abs = S - 0x2d4 - 13*4 + 0x10 + 4(inner retaddr push)
    {
        uint32_t slot = S - 0x2d4 - 13*4 + 0x10 + 4;
        *(uint32_t*)(uintptr_t)slot = 0u;
        printf("[boot] slot=%08x delta=%08x apit@%08x\n", slot, *(uint32_t*)(uintptr_t)slot, VIEW+0x22a4+0xFFFFEF3C);
        fflush(stdout);
    }
    (void)stk;

    // (re)fill API table last: earlier IAT shims overwrite this region
    for(int i=0;i<320;i++) apit[i]=(uint32_t)(uintptr_t)((char*)&tramp_0+32*i);

        apit[0x104]=(uint32_t)(uintptr_t)sVA;

        apit[0x105]=(uint32_t)(uintptr_t)sVP;

        apit[0x12]=(uint32_t)(uintptr_t)sGS;

        apit[0x15]=(uint32_t)(uintptr_t)sGL;

        extern void sX1(void);
    apit[0]=(uint32_t)(uintptr_t)sX1;   // double-call chain: [buf]=sX2, ret 0x104; sX2 ret 0xE70C → FUN==0 → loader branch

        apit[0x16]=(uint32_t)(uintptr_t)sGUn;

        apit[0x17]=(uint32_t)(uintptr_t)sGF;
        // blanket zero stack window, EXCEPT FUN counter slot [S-0x2d4+0x24] — nonzero keeps
        // 'dec' prologue result != 0 so 0x455394 je NOT taken -> loader branch to view+0xfa00
        {
            uint32_t keep = S - 0x2d4 + 0x24;
            for(uint32_t a=(S-0x340)&~3u; a<S+0x10; a+=4) if(a!=keep) *(uint32_t*)(uintptr_t)a=0u;
            *(uint32_t*)(uintptr_t)keep = 2u;   // dec -> 1 -> jne path (prologue nonzero in)
            *(uint32_t*)(uintptr_t)(S - 0x2d4 + 0x10) = 0xFFFFEF3Cu;  // ★attempt5: stable delta = -0x10c4 -> table = VIEW+0x11e0
            printf("[boot] FUN counter @%08x=2 delta-slot @%08x=-0x10c4\n", keep, S-0x2d4+0x10); fflush(stdout);
        }
    *(uint32_t*)(uintptr_t)(S-0x340)=0u;
    extern void run_stub2(uint32_t stub_va, uint32_t esp, uint32_t entry_eax);
        // stub prologue reads [esp+0x324] (== entry [esp]) as CALL RETURN ADDRESS:
    // delta = retaddr - 0x4010c4. Fake original call site: retaddr = 0x4010c4 → delta 0.
    *(uint32_t*)(uintptr_t)(S+0x00) = (uint32_t)(uintptr_t)VIEW;  // ★call-view-pushes-EAX protocol: [S]=view
    *(uint32_t*)(uintptr_t)(S+0x04) = (uint32_t)(uintptr_t)stub_back;  // stub final RET lands here
    extern void run_stub2(uint32_t,uint32_t,uint32_t);
        *(uint32_t*)(uintptr_t)(S+4) = 0xc3;                       // [esp+4] stage-2 base → call 0x400000 = stub entry
    *(uint32_t*)(uintptr_t)(S-0x2d4+0x24) = 6;                 // block counter (0x55586 sets 6; be safe)
    extern void run_stub2(uint32_t,uint32_t,uint32_t);
    run_stub2((uint32_t)(uintptr_t)(VIEW+0x55338), S, (uint32_t)(uintptr_t)VIEW);  // entry EAX=VIEW (stos slot0)
    printf("[boot] stub returned\n");
    dump_regions();
    return 0;
}
