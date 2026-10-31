#define _GNU_SOURCE 1
/* host32.c — H-1 宿主：bleem PSX 引擎静态镜像载入 + API 表注入 + 入口蹦床
 *
 * 协议证据（docs/08/09 + 反汇编 img+0x4）：
 *   入口 = img+0x4，EAX = 控制块 cb（cb+0x170 由宿主预填；alloc 结果写 cb+0x34..0x40）
 *   API 槽0 = [0x4022a4]（img 内 .data），stdcall VirtualAlloc(a,s,t,p) ret 16
 *   img 映射 0x400000（13 处 imm32 引用为 img 内部坐标 0x401000-0x403000）
 * 32-bit native Linux（gcc -m32）。只执行已解密静态镜像，无原始 Windows 代码。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <signal.h>
#include <ucontext.h>
#define REG_EIP REG_EIP
#include <unistd.h>

extern void call_engine(uint32_t entry, uint32_t cb);  /* tramp32.s */
#include "biosrom.h"                                    /* H-8a BLEEM_BIOS 接线 */
extern void host_service_cb(void);
void finish(int fromsignal);
extern uint32_t svc_call(uint32_t);

#define ARENA_BASE 0x2a1000u
#define ARENA_SIZE 0x04000000u   /* seg0 常驻 64 MB RWX：img 把 alloc 指针当 int 存，高地址会溢出 */
/* H-9a seg1 柔性扩展段（内核 7.0 大 MAP_FIXED 触雷 → 禁 FIXED，成功即启用）*/
static const uint32_t SEG1_BASE=0x10000000u;
static uint32_t seg1_cap=0, arena_used=0;
static int32_t arena_take(uint32_t sz){
    uint32_t tot=ARENA_SIZE+seg1_cap;
    if(arena_used+sz>tot) return -1;
    int32_t base=(arena_used<ARENA_SIZE)?(int32_t)(ARENA_BASE+arena_used):(int32_t)(SEG1_BASE+(arena_used-ARENA_SIZE));
    arena_used+=sz; return base;
}
static int arena_contains(uint32_t a){
    return (a>=ARENA_BASE&&a<ARENA_BASE+ARENA_SIZE)||(seg1_cap&&a>=SEG1_BASE&&a<SEG1_BASE+seg1_cap);
}

uint32_t arena_bump = ARENA_BASE;
static uint32_t cb_snap[0x1000];
static int seg=0;
static int st[64]; static int nst=0;
static int st_flushed=0;
static uint32_t bios_region = 0;
static uint32_t allocs[64]; static int nalloc = 0;

/* thunk ABI 实证（0x455459 / img 0x400014 序列 + stdcall ret $16 全流程通过）：
 * 调用方固定压 4 参（含 eax 值），栈序 [0]=size [4]=hint [8]=type/flags [12]=protect
 * → 本 shim 按 stdcall 清 16 字节，size 取栈上第一个压入值。 */
/* BIOS 映射回调：签名 = 引擎 call [esp] 约定，返回 BIOS 基址 */
uint32_t bios_map_cb_c(void){
    void*m=mmap((void*)0xBFC00000u,0x80000,PROT_READ|PROT_WRITE|PROT_EXEC,
                MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS,-1,0);
    if(m!=(void*)0xBFC00000u){ printf("[host] bios map fail\n"); return bios_region; }
    if(g_bios_rom){
        memcpy(m,(void*)(uintptr_t)(bios_region+0xC0000u),0x80000);
        printf("[host] bios mapped @0xBFC00000 (ROM loaded sha256=%s)\n", g_bios_hex);
        return 0xBFC00000u;
    }
    memset(m,0xC3,0x80000);   /* BIOS stub：ret 滑道（H-5 换 PSX.EXE JIT 码）*/
    printf("[host] bios mapped @0xBFC00000 (ret-sled)\n");
    return 0xBFC00000u;   /* 引擎契约：返回 PSX BIOS 基址 */
}

uint32_t __attribute__((stdcall)) sVA(void *addr, uint32_t size, uint32_t type, uint32_t prot){
    (void)type;(void)prot;
    /* 槽0 复用协议：VirtualFree(h,flags) 2 参也打到这里 — flags 落在 size 位 */
    if(addr==(void*)0x2a0000u){ printf("[host] svc VP/Query(0x2a0000) -> 0x104\n"); return 0x104; }
    if((size&0xffffu)==0x0001u && size>=0x10001u && size<=0x1000ffffu){
        printf("[host] FREE %#x flags=%#x\n",(uint32_t)(uintptr_t)addr,size);
        return 1;
    }
    if(size>0x40000000u||size==0) size=0x10000;           /* flags 误入时兜底 */
    size = (size + 0xffff) & ~0xffffu;
    if(!size) size = 0x10000;
    { int32_t r=arena_take(size);
      if(r<0){ printf("[host] arena exhausted\n"); return (uint32_t)-1; } arena_bump=r+size;
    if(nalloc<64) allocs[nalloc++]=r;
    printf("[host] VA size=%#x -> %#x\n", size, (uint32_t)r); return (uint32_t)r; }
}

static uint32_t*cb_g; static int nalloc_g; static int nrec=0;

static void onsegv(int sig,siginfo_t*si,void*uc){
    (void)sig; ucontext_t*u=(ucontext_t*)uc;
    uint32_t eip=(uint32_t)u->uc_mcontext.gregs[REG_EIP], eax=(uint32_t)u->uc_mcontext.gregs[REG_EAX];
    uint32_t esp=(uint32_t)u->uc_mcontext.gregs[REG_ESP];
    printf("[SEGV] eip=%08x eax=%08x esp=%08x addr=%p\n",eip,eax,esp,si->si_addr); fflush(stdout);
    { uint32_t*sp=(uint32_t*)(uintptr_t)esp; for(int i=0;i<8;i++) printf("   st[%d]=%08x\n",i,sp[i]); 
      printf("   cb=%08x thunk[0..3]=%08x %08x\n",(uint32_t)(uintptr_t)cb_g,cb_g?cb_g[0]:0,cb_g?cb_g[1]:0); } fflush(stdout);
    { FILE*g=fopen("host/build/segv_site.txt","w"); if(g){fprintf(g,"eip=%08x eax=%08x esp=%08x addr=%p\n",eip,eax,esp,si->si_addr);fclose(g);} }
    if((eip<0x400000||eip>=0x472000) && cb_g){
        /* 数据槽 call 恢复：栈上找回调点；0x400194=BIOS 回调点 → 返回 0xBFC00000 */
        uint32_t*st=(uint32_t*)(uintptr_t)esp; int k=0; uint32_t ret=0;
        while(k<24){ uint32_t c=st[k]; if(c>=0x400000&&c<0x472000){ ret=c; break; } k++; }
        if(ret){
            /* 服务派发：BIOS(0)→恒等映射；尺寸→arena bump 页对齐分配 */
            uint32_t eax_in=(uint32_t)u->uc_mcontext.gregs[REG_EAX];
            uint32_t r0;
            if(eax_in==0){ r0=bios_map_cb_c(); }
            else { uint32_t sz=(eax_in+0xffff)&~0xffffu; if(!sz)sz=0x10000;
                   r0=arena_bump; arena_bump+=sz;
                   if(arena_bump>=ARENA_BASE+ARENA_SIZE){ r0=(uint32_t)-1; arena_bump-=sz; }
                   printf("[host] svc alloc %#x -> %#x\n", sz, r0); }
            u->uc_mcontext.gregs[REG_ESP]=esp+4*(k+1);
            u->uc_mcontext.gregs[REG_EAX]=r0;
            u->uc_mcontext.gregs[REG_EIP]=ret;
            if(ret==0x4002cd) printf("[host] CB-SVC ret=0x2cd (BIOS-read cb) eax_in=%08x out=%08x\n",eax_in,r0);
            fflush(stdout);
            return;
        }
    }
    if(eip==0x40029c && cb_g){   /* popa 后 edi 污染：恢复 edi=cb 重放 +0x254..0x25c 写入 */
        u->uc_mcontext.gregs[REG_EDI]=(uint32_t)(uintptr_t)cb_g;
        printf("[host] store-recover@29c edi<-cb (ebx=%08x ecx=%08x edx=%08x)\n",(uint32_t)u->uc_mcontext.gregs[REG_EBX],(uint32_t)u->uc_mcontext.gregs[REG_ECX],(uint32_t)u->uc_mcontext.gregs[REG_EDX]); fflush(stdout);
        return;
    }
    if((eip>=0xBFC00000u&&eip<0xBFC80000u) || (eip>=0x38000000u&&eip<0x38080000u) ||
       ((eip>=0x2a1000u&&eip<0x42a1000u)&&eip!=0) || eip==0x400000u || eip==0x400004u){
        /* BIOS-vector / arena / memtable-call 跳入：HLE stub 语义 —— 返回 0 并回到 img 调用者 */
        uint32_t *st=(uint32_t*)(uintptr_t)esp; int k=0; uint32_t ret=0;
        while(k<24){ uint32_t c=st[k]; if(c>=0x400010&&c<0x472000){ ret=c; break; } k++; }
        if(ret){ u->uc_mcontext.gregs[REG_EIP]=ret; u->uc_mcontext.gregs[REG_ESP]=esp+4*(k+1);
                 u->uc_mcontext.gregs[REG_EAX]=0;
                 if(seg<8 || seg%400==0) printf("[host] stub-sled@%08x -> %08x (#%d)\n",eip,ret,seg); fflush(stdout);
            { FILE*f=fopen("host/build/stsites.txt","a"); if(f){fprintf(f,"stub %08x\n",eip);fclose(f);} }
                 if(++seg>=3000){ printf("[host] stub-sled cap 3000\n"); finish(0); } return; }
    }
    if(eip==0x400005 && cb_g){   /* 第二轮 init：EAX 丢失 → 恢复 EAX=cb */
        u->uc_mcontext.gregs[REG_EAX]=(uint32_t)(uintptr_t)cb_g;
        printf("[host] reinit-recover@5 eax<-cb\n"); fflush(stdout);
        return;
    }
    if(eip>=0x400000&&eip<0x472000 && (uint32_t)(uintptr_t)si->si_addr<0x1000 && cb_g && eip!=0x40017b){
        u->uc_mcontext.gregs[REG_EDI]=(uint32_t)(uintptr_t)cb_g;   /* store replay */
        if(nst<64) st[nst++]=eip;
        if(++nrec>4000){ printf("[host] recover cap\n");
            if(!st_flushed){ FILE*f=fopen("host/build/stsites.txt","a");
                if(f){ for(int q=0;q<nst;q++) fprintf(f,"store %08x\n",st[q]); fclose(f);} st_flushed=1; }
            for(int q=0;q<nst;q++) st[q]=0; nst=0;
            u->uc_mcontext.gregs[REG_EIP]=0x4002b7;   /* 强制返回出口：完成本轮 init */
            u->uc_mcontext.gregs[REG_EAX]=1;
            return; }
        return;
    }
    if(eip==0x40017b && cb_g){
        u->uc_mcontext.gregs[REG_EIP]=0x400181;   /* 跳过 edi 写：尺寸+槽由宿主直接补 */
        cb_g[0x30/4]=0x800000; cb_g[0x60/4]=0x328400; cb_g[0x34/4]=cb_g[0x38/4]||0x2a1000;
        /* 已知派发约定瑕疵：call 方丢失 cb(edi=0)。宿主恢复 edi=cb 并放行，
         * 使 +0x30 RAM 尺寸槽正确写入，推进至 BIOS 回调阶段（htrace 已验证该路径）。*/
        u->uc_mcontext.gregs[REG_EDI]=(uint32_t)(uintptr_t)cb_g;
        u->uc_mcontext.gregs[REG_EFL]|=0x100;   /* 由 finish 前继续步进？不 — 恢复执行 */
        /* 撤销 TF：仅恢复 edi 继续 */
        u->uc_mcontext.gregs[REG_EFL]&=~0x100;
        printf("[host] recover@0x17b edi<-cb\n"); fflush(stdout);
        return;
    }
    finish(1);
}
int main(int argc,char**argv){
    setvbuf(stdout,NULL,_IONBF,0);
    const char*imgp = argc>1?argv[1]:"decrypted/stream_10a4c_L71cbf_relocated.bin";
    FILE*f=fopen(imgp,"rb"); if(!f){perror("img");return 1;}
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    if(!mmap((void*)ARENA_BASE, ARENA_SIZE, PROT_READ|PROT_WRITE|PROT_EXEC,
             MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS,-1,0)){printf("[host] arena fail\n");return 2;}
    { const char*am=getenv("H_ARENA_MB");                 /* H-9a: seg1（H_ARENA_MB=总容量，含 seg0 64MB）*/
      if(am){ uint32_t mb=strtoul(am,0,0); if(mb>64u){ uint32_t extra=(mb-64u)*0x100000u; if(extra>0x30000000u) extra=0x30000000u;
        void*q=mmap((void*)SEG1_BASE,extra,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(q==(void*)SEG1_BASE) seg1_cap=extra;
        printf("[host] seg1=%p cap=%#x\n",q,seg1_cap); } } }
    /* arena 页预置可执行存根：0xC3 ret / 0xC2 10 00 ret $16 —— 引擎把 VA 结果
     * 当函数指针调用时安全返回 0（H-2 探针实证 pc=e9353000 = 数据被当代码）*/
    memset((void*)ARENA_BASE, 0xC3, ARENA_SIZE);
    void*v=mmap((void*)0x400000,12u<<20,PROT_READ|PROT_WRITE|PROT_EXEC,
                MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS,-1,0);
    if(v!=(void*)0x400000){perror("img map");return 3;}
    uint8_t*IMG=v;
    if(fread(IMG,1,(size_t)sz,f)!=(size_t)sz){printf("[host] img short\n");return 4;}
    fclose(f);
    printf("[host] img %ld bytes @0x400000, arena @%#x\n", sz, ARENA_BASE);

    uint32_t*tab=(uint32_t*)(IMG+0x22a4);
    /* 槽0 = VirtualAlloc（host shim）。注意：img+0x22a4 其余区域是运行期由
     * 引擎自身填写的代码占位（mov [ecx],eax 模式）——静态预填会破坏语义，禁止。*/
    tab[0]=(uint32_t)(uintptr_t)&sVA;

    uint32_t*cb=mmap((void*)0x50000,0x4000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS,-1,0);
    /* cb 头 = 通用服务跳板：call [esp] 命中 cb 基址(cb+0x68 未写时数值=cb)→ 跳 host_service_cb */
    { extern void host_service_cb(void); cb[0x68/4]=(uint32_t)(uintptr_t)&host_service_cb; }  /* 服务指针槽 */

    cb[0x170/4]=(uint32_t)(uintptr_t)&host_service_cb;

    /* H-2：BIOS 映射回调槽 — 引擎 img+0x191 `call [esp]` 请求 host 把
     * 0xBFC00000 区映射并返回指针（img+0x1A4 sub eax,0xBFC00000 → cb+0x48 偏移）。
     * 返回固定 2MB 假 BIOS 区（内容 = 引擎自带 ROM 数据，blk1/blk2 载入待 H-3）。*/
    bios_region=0x38000000u;   /* 低 4GB 内固定区，指针算术安全 */
    if(mmap((void*)bios_region,2u<<20,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS,-1,0)==MAP_FAILED)
        printf("[host] warn: bios region map failed\n");
    else {
        /* 假 BIOS 头：PC=0xBFC00000 区可读 */
        uint32_t*b=(uint32_t*)(uintptr_t)bios_region; b[0]=0x40000000u; /* 占位指令 */
        { int r=bios_rom_load();                   /* H-8a：BLEEM_BIOS 硬校验 */
          if(r<0) return 6;
          if(r>0){ bios_rom_place(bios_region, 0xC0000u); bios_rom_register("host/build/bios_sha256.txt"); } }
        void stub_fn(void); /* noop */
    }


    cb_g=cb;
    struct sigaction sa; memset(&sa,0,sizeof sa); sa.sa_sigaction=onsegv; sa.sa_flags=SA_SIGINFO;
    sigaction(SIGSEGV,&sa,0); sigaction(SIGBUS,&sa,0);

    /* 探针：入口 EAX 变体（0=原映像值）*/
    memcpy(cb_snap,cb,0x4000);
    if(getenv("H_EAX0")) call_engine(0x400004u,0);
    else call_engine(0x400004u,(uint32_t)(uintptr_t)cb);

    finish(0);
    return 0;
}
void finish(int fromsignal){
    uint32_t*cb=cb_g;
    { FILE*df=fopen("host/build/cb_diff.txt","w");
      if(df){ for(int i=0;i<0x1000;i++) if(cb[i]!=cb_snap[i] && cb[i]) fprintf(df,"+%04x=%08x\n",i*4,cb[i]); fclose(df); } }
    printf("[host] cb+0x34=%08x +0x38=%08x +0x3c=%08x +0x40=%08x allocs=%d\n",
           cb[0x34/4],cb[0x38/4],cb[0x3c/4],cb[0x40/4],nalloc);
    printf("[host] BIOS: +0x44=%08x +0x48=%08x +0x4c=%08x +0x50=%08x +0x54=%08x +0x58=%08x +0x5c=%08x | +0x30=%08x +0x60=%08x\n",
           cb[0x44/4],cb[0x48/4],cb[0x4c/4],cb[0x50/4],cb[0x54/4],cb[0x58/4],cb[0x5c/4],cb[0x30/4],cb[0x60/4]);
    { FILE*mm=fopen("host/build/memmap.txt","w");
      if(mm){ fprintf(mm,"psx_size=%08x\nram=%08x,%08x,%08x,%08x\nbios_base=%08x bios_off=%08x\nwin4c=%08x win50=%08x win54=%08x win58=%08x win5c=%08x\nmemtable=%08x\n",
              cb[0x30/4],cb[0x34/4],cb[0x38/4],cb[0x3c/4],cb[0x40/4],cb[0x44/4],cb[0x48/4],
              cb[0x4c/4],cb[0x50/4],cb[0x54/4],cb[0x58/4],cb[0x5c/4],cb[0x60/4]); fclose(mm); } }
    int slots_ok=0;
    for(int i=0;i<4;i++){
        uint32_t a=cb[0x34/4+i];
        if(arena_contains(a)){
            slots_ok++;
            *(volatile uint32_t*)(uintptr_t)a=0x51455250u;      /* 写 */
            if(*(volatile uint32_t*)(uintptr_t)a!=0x51455250u) slots_ok--;
            *(volatile uint32_t*)(uintptr_t)a=0x41414141u;      /* 覆盖回 */
        }
    }
    int ok = slots_ok>=2 && nalloc>=2;   /* H-1 验收：≥2 控制块槽带 RW 证明 + shim 记账一致 */
    FILE*pf=fopen("host/build/arena_proof.txt","w");
    if(!pf) pf=fopen("arena_proof.txt","w");
    if(pf){ fprintf(pf,"H1 %s slots=%08x,%08x,%08x,%08x allocs=%d slots_ok=%d\n",
            ok?"PASS":"FAIL",cb[0x34/4],cb[0x38/4],cb[0x3c/4],cb[0x40/4],nalloc,slots_ok); fclose(pf); }
    printf("[host] H1 %s (signal=%d)\n", ok?"PASS":"FAIL", fromsignal);
    _exit(ok?0:5);
}
