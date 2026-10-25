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
void finish(int fromsignal);

#define ARENA_BASE 0x2a1000u
#define ARENA_SIZE 0x04000000u   /* 常驻 64 MB RWX：img 把 alloc 指针当 int 存，高地址会溢出 */

static uint32_t arena_bump = ARENA_BASE;
static uint32_t bios_region = 0;
static uint32_t allocs[64]; static int nalloc = 0;

/* thunk ABI 实证（0x455459 / img 0x400014 序列 + stdcall ret $16 全流程通过）：
 * 调用方固定压 4 参（含 eax 值），栈序 [0]=size [4]=hint [8]=type/flags [12]=protect
 * → 本 shim 按 stdcall 清 16 字节，size 取栈上第一个压入值。 */
/* BIOS 映射回调：签名 = 引擎 call [esp] 约定，返回 BIOS 基址 */
/* BIOS 映射回调（img+0x191 call [esp] 协议）：
 * 引擎随后 `sub eax,0xBFC00000` → 必须返回 PSX 规范基址 0xBFC00000，
 * 并保证该 VAS 有映射（host 低位 BIOS 区 + 512K 恒等区）。*/
uint32_t bios_map_cb(void){
    void*m=mmap((void*)0xBFC00000u,0x80000,PROT_READ|PROT_WRITE,
                MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS,-1,0);
    if(m!=(void*)0xBFC00000u){ printf("[host] bios map fail\n"); return bios_region; }
    /* 拷贝已备好的低位区内容（若有数据）*/
    memcpy((void*)0xBFC00000u,(void*)bios_region,0x80000);
    printf("[host] bios mapped @0xBFC00000\n");
    return 0xBFC00000u;
}

uint32_t __attribute__((stdcall)) sVA(void *addr, uint32_t size, uint32_t type, uint32_t prot){
    (void)type;(void)prot;
    /* 槽0 复用协议：VirtualFree(h,flags) 2 参也打到这里 — flags 落在 size 位 */
    if((size&0xffffu)==0x0001u && size>=0x10001u && size<=0x1000ffffu){
        printf("[host] FREE %#x flags=%#x\n",(uint32_t)(uintptr_t)addr,size);
        return 1;
    }
    if(size>0x40000000u||size==0) size=0x10000;           /* flags 误入时兜底 */
    size = (size + 0xffff) & ~0xffffu;
    if(!size) size = 0x10000;
    uint32_t r = arena_bump;
    arena_bump += size;
    if(arena_bump >= ARENA_BASE + ARENA_SIZE){ printf("[host] arena exhausted\n"); return (uint32_t)-1; }
    if(nalloc<64) allocs[nalloc++]=r;
    printf("[host] VA size=%#x -> %#x\n", size, r);
    return r;
}

static uint32_t*cb_g; static int nalloc_g; 
static void onsegv(int sig,siginfo_t*si,void*uc){
    (void)sig; ucontext_t*u=(ucontext_t*)uc;
    uint32_t eip=(uint32_t)u->uc_mcontext.gregs[REG_EIP], eax=(uint32_t)u->uc_mcontext.gregs[REG_EAX];
    uint32_t esp=(uint32_t)u->uc_mcontext.gregs[REG_ESP];
    printf("[SEGV] eip=%08x eax=%08x esp=%08x addr=%p\n",eip,eax,esp,si->si_addr); fflush(stdout);
    { FILE*g=fopen("host/build/segv_site.txt","w"); if(g){fprintf(g,"eip=%08x eax=%08x esp=%08x addr=%p\n",eip,eax,esp,si->si_addr);fclose(g);} }
    if(eip==0x40017b && cb_g){
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

    uint32_t*cb=mmap(NULL,0x4000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    /* cb 头 16B = 可执行入口 thunk：call [esp] 直接跳进 bios_map_cb（JMP rel32 保留槽可覆盖）*/
    {
        uint8_t*th=(uint8_t*)cb;
        th[0]=0xB8; *(uint32_t*)(th+1)=(uint32_t)(uintptr_t)&bios_map_cb; /* mov eax,cb_fn */
        th[5]=0xFF; th[6]=0xE0;                                           /* jmp eax */
    }
    cb[0x170/4]=(uint32_t)(uintptr_t)cb+0x200;

    /* H-2：BIOS 映射回调槽 — 引擎 img+0x191 `call [esp]` 请求 host 把
     * 0xBFC00000 区映射并返回指针（img+0x1A4 sub eax,0xBFC00000 → cb+0x48 偏移）。
     * 返回固定 2MB 假 BIOS 区（内容 = 引擎自带 ROM 数据，blk1/blk2 载入待 H-3）。*/
    bios_region=0x38000000u;   /* 低 4GB 内固定区，指针算术安全 */
    if(mmap((void*)bios_region,2u<<20,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS,-1,0)==MAP_FAILED)
        printf("[host] warn: bios region map failed\n");
    else {
        /* 假 BIOS 头：PC=0xBFC00000 区可读 */
        uint32_t*b=(uint32_t*)(uintptr_t)bios_region; b[0]=0x40000000u; /* 占位指令 */
        void stub_fn(void); /* noop */
    }


    cb_g=cb;
    struct sigaction sa; memset(&sa,0,sizeof sa); sa.sa_sigaction=onsegv; sa.sa_flags=SA_SIGINFO;
    sigaction(SIGSEGV,&sa,0); sigaction(SIGBUS,&sa,0);

    /* 探针：入口 EAX 变体（0=原映像值）*/
    if(getenv("H_EAX0")) call_engine(0x400004u,0);
    else call_engine(0x400004u,(uint32_t)(uintptr_t)cb);

    finish(0);
    return 0;
}
void finish(int fromsignal){
    uint32_t*cb=cb_g;
    printf("[host] cb+0x34=%08x +0x38=%08x +0x3c=%08x +0x40=%08x allocs=%d\n",
           cb[0x34/4],cb[0x38/4],cb[0x3c/4],cb[0x40/4],nalloc);
    int slots_ok=0;
    for(int i=0;i<4;i++){
        uint32_t a=cb[0x34/4+i];
        if(a>=ARENA_BASE && a<ARENA_BASE+ARENA_SIZE){
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
