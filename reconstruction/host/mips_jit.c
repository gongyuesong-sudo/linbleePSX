/* mips_jit.c — H-10a BIOS-JIT 框架：MIPS-I(R3000)→x86-32 块级按需翻译器（task-021）
 *
 * 设计（输入=docs/13 最小指令集清单）：
 *  - 状态：psx_state（32 寄存器 + PC，-no-pie 固定址）；ECX 作每指令寄存器号 scratch，
 *    MIPS 寄存器 i ⇄ [EAX_BASE + ECX*4] SIB 寻址；块间控制流走 __jit_next helper
 *  - 内存桥：8MB RAM + ROM 别名 phys 0x1FC00000；访存/非对齐/COP 全走 stdcall helper
 *  - 基本块终止于 jr/jal/jalr/branch/syscall；块缓存 hash[vpc>>2 & 1023]
 *  - 取指失败 = 停机 rc=2（PSX 异常面留 H-10b）
 * 自测：默认=指令语义对拍；BIOS=/path 参数=SCPH1001 引导烟测 200k 步。
 * 构建：gcc -m32 -O1 -no-pie -c host/mips_jit.c；测试链接加 -DJIT_MAIN */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/syscall.h>
static const uint32_t c2stub_words[]={0x3c020000u,0x34420001u,0x11200003u,0u,0x03e00008u,0u,0x03e00008u,0u}; /* tools/c2stub_build.py 同步(task-044) */
#include <unistd.h>

int g_dbg_emit=0;
static FILE *jit_rc_log;
typedef struct { uint32_t r[32]; uint32_t pc; uint32_t halted; uint32_t halt_kind; uint32_t bflag; } PsxState;  /* halt_kind: 1=syscall/break 2=emit-fail; bflag=块内分支 flag(勘误: 原用 ST+124=ra 同址互踩, 后 140 越界) */
PsxState psx_state;
#define HK (ST+136u)

uint8_t *psx_ram;                        /* 8MB 物理 RAM */
const uint8_t *psx_rom;
uint32_t jit_fail_pc, jit_fail_op;
uint32_t jit_cur_pc;                  /* 512KB ROM（宿主侧拷贝，或窗指针）*/

static uint32_t phys(uint32_t a){ uint32_t k=(a&0x80000000u)?(a&0x1FFFFFFFu):a;
    if((k&0x1FE00000u)==0x1FC00000u) return 0x1FC00000u|(k&0x7FFFFu);  /* ROM: 仅 0x1FC/0x1FD 图案(含 KSEG1 0xBFC) */
    return a&0x1FFFFFu; }  /* RAM 2MB 回绕按 A20-A0 解码——PSX 硬件忽略高位：0xFFFE0130→RAM 0x130(异常向量基数据寄存器) */
static int ram_fetch(uint32_t vpc, uint32_t *out){
    uint32_t p=phys(vpc);
    if(p>=0x1FC00000u && p<0x20000000u && psx_rom){ *out=*(const uint32_t*)(psx_rom+(p-0x1FC00000u)); return 1; }
    if(p>=0x1FC00000u && p<0x20000000u && psx_rom){ *out=*(const uint32_t*)(psx_rom+(p-0x1FC00000u)); return 1; }
    if(p+4<=0x200000u && psx_ram){ *out=*(const uint32_t*)(psx_ram+p); return 1; }
    return 0;
}
/* ---- 访存/COP helper（stdcall，生成码直接 call） ---- */
static FILE *devlog_fp(){ static FILE*f; static int done; if(!done){done=1; if(getenv("H_JITDEV")) f=fopen(getenv("H_JITDEV"),"a"); } return f; }
static int g_vecprot=0;   /* HLE 向量页写保护开关 */
static uint32_t g_lowrd=0; /* RAM 0x60-0x6C 设备槽读计数 */
static void devlog(const char*rw,uint32_t a,uint32_t v){ FILE*f=devlog_fp(); if(!f) return; if((a&0x1FFF0000u)==0x1F800000u) fprintf(f,"%s %08x %08x\n",rw,a,v); }
/* 最小设备层：0x1F800000-0x1F80FFFF 16-bit 默认开寄存器组（uninit=0xFFFFFFFF；
 * GPU/CDROM status 窗默认 0=ready 已实证无害；写全收，读按寄存器值）*/
static uint32_t dev_regs[0x10000>>2];
static uint32_t g_gpuw;   /* GP0(0x1F801810) 命令写计数(H-17) */
static uint32_t exvbd[0x1000>>2];   /* 0xFFFE0000-0xFFFEFFFF: ExceptionVectorBaseData 硬件寄存器组(RAM 0x130 类写入实为硬件窗) */
static uint32_t dev_read(uint32_t a){ uint32_t o=a&0xFFFF;
    if(o>=0x1000&&o<=0x10FF) return dev_regs[o>>2];         /* GPU/IRQ 域默认 0 = ready */
    if(o==0x1060) return dev_regs[o>>2];
    { static int dr=-1; if(dr<0) dr=!!getenv("H_DEVRST");
      if(dr && o>=0x1080 && o<=0x10FF) return dev_regs[o>>2];   /* H-21a(task-047): DMA0-7 控制块硬件复位=0(CHCR busy/trigger=0);
         旧默认 0xFFFFFFFF 使 C2 DMA7 完成位轮询 0x1F801DAC 死环(实测 10008 次/链, docs/34) */ }
    return dev_regs[o>>2]?dev_regs[o>>2]:0xFFFFFFFFu; }
static uint32_t cop0[32];
#define COP0_EPC 14
uint32_t __attribute__((stdcall)) jh_rfe(void){ cop0[12]&=~(1u<<30); return cop0[COP0_EPC]; }
uint32_t __attribute__((stdcall)) jh_cop0_read(uint32_t r0){ return cop0[r0&31]; }
static uint32_t jh_hi, jh_lo;   /* R3000A HI/LO(task-043: divu/mfhi 首用面 bfc012a8) */
void __attribute__((stdcall)) jh_mult(uint32_t a,uint32_t b){ int64_t q=(int64_t)(int32_t)a*(int32_t)(int32_t)b; jh_lo=(uint32_t)q; jh_hi=(uint32_t)((uint64_t)q>>32); }
void __attribute__((stdcall)) jh_multu(uint32_t a,uint32_t b){ uint64_t q=(uint64_t)a*(uint64_t)b; jh_lo=(uint32_t)q; jh_hi=(uint32_t)(q>>32); }
void __attribute__((stdcall)) jh_div(uint32_t a,uint32_t b){ if(b){ jh_lo=(uint32_t)((int32_t)a/(int32_t)b); jh_hi=(uint32_t)((int32_t)a%(int32_t)b); } }
void __attribute__((stdcall)) jh_divu(uint32_t a,uint32_t b){ if(b){ jh_lo=a/b; jh_hi=a%b; } }
void __attribute__((stdcall)) jh_cop0_write(uint32_t r0,uint32_t v){ r0&=31; cop0[r0]=v;
    /* SR.BEV 1->0 迁移：异常向量从 ROM 镜像到 RAM（真实 CPU 由 BIOS 复制；此处跟随状态位忠实化） */
    if(r0==12 && psx_ram && psx_rom && !(v&0x04000000u) && !cop0[31]){ cop0[31]=1; memcpy(psx_ram,psx_rom,0x100); } }
uint32_t __attribute__((stdcall)) jh_lw(uint32_t a){
    uint32_t a0=a;
    a=phys(a);
    if(a>=0x2A0100u&&a<0x2A0104u&&devlog_fp()){ extern uint32_t jit_cur_pc; fprintf(devlog_fp(),"LW2A0 pc=%08x a=%08x\n",jit_cur_pc,a0); fflush(devlog_fp()); }
    if(getenv("H_SWTRACE")&&((a>=0x80041000u&&a<0x80041004u)||(a>=0x82000u&&a<0x82004u)||(a>=0x80050000u&&a<0x80050004u))&&devlog_fp()){ static int m; if(m<10){m++; extern uint32_t jit_cur_pc; fprintf(devlog_fp(),"LWT #%d a=%08x pc=%08x\n",m,a,jit_cur_pc); fflush(devlog_fp());} }
    if((a0&0xFFFF0000u)==0xFFFE0000u){ uint32_t r=exvbd[(a0&0xFFF)>>2]; devlog("RD",a0,r); return r; }
    if((a0&0x1FFF0000u)==0x1F800000u){ uint32_t r=dev_read(a0); devlog("RD",a0,r); return r; }
    { uint32_t k=phys(a0); if(k>=0x60u&&k<=0x6Fu&&psx_ram) g_lowrd++; }
    if(a>=0x1FC00000u && a+4<=0x20000000u && psx_rom) return *(const uint32_t*)(psx_rom+(a-0x1FC00000u));
    if(a+4<=0x200000u && psx_ram) return *(const uint32_t*)(psx_ram+a);
    return 0;
}
static void watch_store(uint32_t a,uint32_t v){ static int done; static uint32_t lo,hi; FILE*f;
    if(!done){ done=1; { const char*w=getenv("H_WATCH"); if(w){ sscanf(w,"%x:%x",&lo,&hi); } } }
    if(hi&&a>=lo&&a<hi&&(f=devlog_fp())){ fprintf(f,"STR pc=%08x a=%08x v=%08x\n",jit_cur_pc,a,v); fflush(f); } }
void __attribute__((stdcall)) jh_sw(uint32_t a, uint32_t v){ watch_store(a,v); watch_store(phys(a),v);
    { static int n; if(getenv("H_SWTRACE")&&((a==0x80050000u||a==0x800507fcu||a==0x80040038u)&&n<10||(v&3&&(v>>26)==0x2Fu)&&n<26)){n++; if(jit_rc_log){fprintf(jit_rc_log,"SWTX #%d a=%08x v=%08x pc=%08x\n",n,a,v,jit_cur_pc); fflush(jit_rc_log);} } }
    if((a&0xFFFF0000u)==0xFFFE0000u){ exvbd[(a&0xFFF)>>2]=v; devlog("HW",a,v); return; }
    if((a&0x1FFF0000u)==0x1F800000u){ uint32_t o=a&0xFFFF; dev_regs[o>>2]=v; if(o==0x1810)g_gpuw++;
        { static int d2=-1; if(d2<0) d2=!!getenv("H_DEVRST2");
          if(d2 && o>=0x1080 && o<=0x10FF && (o&0xF)==0xC && (v&0x01000000u)){   /* CHCR.Trigger→自清+Finished(task-049) */
            uint32_t ch=(o-0x1080)>>4;
            dev_regs[o>>2]=(v&~0x01000000u)|0x02000000u;
            dev_regs[(0x1088+ch*0x10)>>2]=0u;                                    /* BC=0 传输完成 */
            devlog("WR",a,v); return; } }
        devlog("WR",a,v); return; } a=phys(a);
    if(g_vecprot && a>=0x80u && a<0xC0u && psx_ram && v!=0u){ *(uint32_t*)(psx_ram+0x9300u+(a&0x3Cu))=v; watch_store(a,v); return; }  /* 向量页写保护(HLE) */
    if(a+4<=0x200000u&&psx_ram) *(uint32_t*)(psx_ram+a)=v; }
uint32_t __attribute__((stdcall)) jh_lb(uint32_t a){ return (jh_lw(a&~3u)>>(8*(a&3)))&0xff; }        /* 有/无符号同低字节，lb 符号扩展在调用点做 */
uint32_t __attribute__((stdcall)) jh_lbu(uint32_t a){ return jh_lb(a); }
uint32_t __attribute__((stdcall)) jh_lhu(uint32_t a){ return (jh_lw(a&~3u)>>((a&2)?16:0))&0xffff; }
void __attribute__((stdcall)) jh_sb(uint32_t a, uint32_t v){ uint32_t w=jh_lw(a&~3u),s=8*(a&3); jh_sw(a&~3u,(w&~(0xffu<<s))|((v&0xffu)<<s)); }
void __attribute__((stdcall)) jh_sh(uint32_t a, uint32_t v){ uint32_t w=jh_lw(a&~3u),s=16*(a&2); jh_sw(a&~3u,(w&~(0xffffu<<s))|((v&0xffffu)<<s)); }
uint32_t __attribute__((stdcall)) jh_lwl(uint32_t a, uint32_t cur){ if((a&0x1FFF0000u)==0x1F800000u){ uint32_t w=dev_read(a&~3u); devlog("RD",a&~3u,w); return cur&((a&3)?((1u<<(8*(a&3)))-1):0)|(w&~((a&3)?((1u<<(8*(a&3)))-1):0)); } uint32_t w=jh_lw(a&~3u),m=(a&3)?((1u<<(8*(a&3)))-1):0; return (cur&m)|(w&~m); }
uint32_t __attribute__((stdcall)) jh_lwr(uint32_t a, uint32_t cur){ if((a&0x1FFF0000u)==0x1F800000u){ uint32_t w=dev_read(a&~3u),m=(1u<<(8*((a&3)+1)))-1; devlog("RD",a&~3u,w); return (cur&~m)|(w&m); } uint32_t w=jh_lw(a&~3u),m=(1u<<(8*((a&3)+1)))-1; return (cur&~m)|(w&m); }
void __attribute__((stdcall)) jh_swl(uint32_t a, uint32_t v){ uint32_t w=jh_lw(a&~3u),m=(a&3)?((1u<<(8*(a&3)))-1):0; jh_sw(a&~3u,(w&m)|(v&~m)); }
void __attribute__((stdcall)) jh_swr(uint32_t a, uint32_t v){ uint32_t w=jh_lw(a&~3u),m=(1u<<(8*((a&3)+1)))-1; jh_sw(a&~3u,(w&~m)|(v&m)); }
/* 分支解析：EAX 由 setCC 预置（0/1） → 返回 taken/not-taken 之一并写入 pc */
/* 铁律：helper 必须纯 stdcall 无任何栈姿态副作用（调试日志曾毁掉 ret $12 平衡）*/
void __attribute__((stdcall)) jh_dbg(uint32_t tag){ fprintf(stderr,"[CB%u] pc=%08x\n",tag,psx_state.pc); fflush(stderr); }
void __attribute__((stdcall)) jh_raw31(uint32_t v,uint32_t si,uint32_t jpc){ if(jit_rc_log){ static int c=0; if(c<400){ c++;
    fprintf(jit_rc_log,"RAW31 jpc=%08x val=%08x sp=%08x sim=%08x\n",jpc,v,psx_state.r[29],si); fflush(jit_rc_log);} } }
static int jalrz_on = 0;   /* 启动期初始化于 main 前: 见下 */
__attribute__((constructor)) static void jalrz_init(void){ jalrz_on = (getenv("H_JALRZ")||getenv("H_RAWATCH"))?1:0; }
void __attribute__((stdcall)) jh_jalrz(uint32_t v,uint32_t jpc){   /* jalr ra,$0 直写 ra=pc+8 毒种捕获(task-050a2) */
    if(v<0x40u && jalrz_on>0 && jit_rc_log){ static int c=0; if(c<60){ c++;
        fprintf(jit_rc_log,"JALRZ#%d jpc=%08x v=%08x ra=%08x sp=%08x v0=%08x t9=%08x\n",c,jpc,v,psx_state.r[31],psx_state.r[29],psx_state.r[2],psx_state.r[25]); fflush(jit_rc_log);} } }
uint32_t __attribute__((stdcall)) __jit_next(uint32_t nt, uint32_t tk, uint32_t flag){
    psx_state.pc = flag? tk : nt; return psx_state.pc;   /* 纯选路：arg1=tk arg2=nt arg3=flag */
}

/* ---------------- 发射器 ---------------- */
#define POOL_CAP (16u<<20)
#define CSLOTS 1024
#define ST ((uint32_t)(uintptr_t)&psx_state)
#define PCR (ST+128u)
static uint8_t *code_pool; static uint32_t pool_used;
static uint8_t *E, *EEND, *EMAX;
static int ovf=0;
static void b1(uint8_t v){ if(E<EEND)*E=v; if(g_dbg_emit) fprintf(stderr,"[B1w] off=%03x v=%02x\n",(unsigned)(E-code_pool),v); E++; if(E>EEND) ovf=1; }
static void b4(uint32_t v){ b1(v);b1(v>>8);b1(v>>16);b1(v>>24); }   /* 越界由 ovf 上报 */
static void set_ecx_st(void){ b1(0xB9); b4(ST); }                                /* mov ecx, &psx_state */
static void set_edx_r(int gr){ b1(0xBA); b4((uint32_t)gr); }                     /* mov edx, gr */
static void mov_eax_mem(void){ b1(0x8B); b1(0x04); b1(0x91); }/* [ecx+edx*4] *//* mov eax,[eax+ecx*4]; nop 对齐戳 */
static void mov_mem_eax(void){ b1(0x89); b1(0x04); b1(0x91); }/* [ecx+edx*4] *//* mov [eax+ecx*4],eax */
static void add_eax_mem(void){ b1(0x03); b1(0x04); b1(0x91); }/* [ecx+edx*4] *//* add eax,[eax+ecx*4] */
static void sub_eax_mem(void){ b1(0x2B); b1(0x04); b1(0x91); }/* [ecx+edx*4] */
static void and_eax_mem(void){ b1(0x23); b1(0x04); b1(0x91); }/* [ecx+edx*4] */
static void or_eax_mem(void){ b1(0x0B); b1(0x04); b1(0x91); }/* [ecx+edx*4] */
static void xor_eax_mem(void){ b1(0x33); b1(0x04); b1(0x91); }/* [ecx+edx*4] */
static void cmp_eax_mem(void){ b1(0x3B); b1(0x04); b1(0x91); }/* [ecx+edx*4] */
static void neg_eax(void){ b1(0xF7); b1(0xD8); }
static void not_eax(void){ b1(0xF7); b1(0xD0); }
static void imul_eax_ecx(void){ b1(0x0F); b1(0xAF); b1(0xC1); }
static void mov_imm_eax(uint32_t v){ b1(0xB8); b4(v); }
static void add_eax_imm(int32_t v){ b1(0x05); b4((uint32_t)v); }
static void cmp_eax_imm(uint32_t v){ b1(0x3D); b4(v); }
static void shl_eax_cl(void){ b1(0xD3); b1(0xE0); }
static void shr_eax_cl(void){ b1(0xD3); b1(0xE8); }
static void sar_eax_cl(void){ b1(0xD3); b1(0xF8); }
static void mov_ecx_imm(uint8_t v){ b1(0xB1); b1(v); }
static void setcc_cl(uint8_t cc){ b1(0x0F); b1(0x90|cc); b1(0xC1); }
static void setcc_al(uint8_t cc){ b1(0x0F); b1(0x90|cc); b1(0xC0); }
static void movzx_eax_al(void){ b1(0x0F); b1(0xB6); b1(0xC0); }
static void movzx_ecx_cl(void){ b1(0x0F); b1(0xB6); b1(0xC9); }
static void mov_eax_ecx(void){ b1(0x8B); b1(0xC1); }   /* mov eax,ecx */
static void sx_eax_al(void){ b1(0x0F); b1(0xBF); b1(0xC0); }   /* movsx eax,ax（lhu 不需要；lb 用 cwde 变体另做） */
static void sx_byte_eax(void){ b1(0x0F); b1(0xBE); b1(0xC0); } /* movsx eax,al —— lb 符号 */
static int pushcnt=0;
static void push_eax(void){ b1(0x50); }
static void push_eax_c(void){ pushcnt++; push_eax(); }
static void add_esp(uint8_t n){ b1(0x83); b1(0xC4); b1(n); }   /* 调用者清栈（stdcall 平衡）*/
static void call_ptr(uint32_t fn){ mov_imm_eax(fn); b1(0xFF); b1(0xD0); }
static void mov_pc_eax(void){ mov_imm_eax(PCR); mov_mem_eax(); }               /* [EAX_BASE+0*4]=pc → 用 reg0? 不：pc 走 disp */
static void set_pc_imm(uint32_t v){ mov_imm_eax(PCR); /* pc 不是 r[0]！ */ (void)v; }
/* pc 存取：直接 mov dword [PCR], imm32 = C7 05 <addr> <imm> */
static void store_pc_imm(uint32_t v){ b1(0xC7); b1(0x05); b4(PCR); b4(v); }
static void store_pc_eax(void){ b1(0xA3); b4(PCR); }                 /* mov [PCR],eax */
static void load_pc_eax(void){ b1(0xA1); b4(PCR); }                            /* mov eax,[PCR] */
static void ret_(void){ b1(0xC3); }
static void xor_eax_eax(void){ b1(0x31); b1(0xC0); }
static void rdd(int gr){ set_ecx_st(); set_edx_r(gr); mov_eax_mem(); }
static void wrr(int gr){ set_ecx_st(); set_edx_r(gr); mov_mem_eax(); }

#define H32(fn) ((uint32_t)(uintptr_t)(void*)(fn))
static void call3_2(uint32_t fn, uint32_t a1, uint32_t a2){
    mov_imm_eax(a2); push_eax(); mov_imm_eax(a1); push_eax(); call_ptr(fn);
}
static void call2_1(uint32_t fn, uint32_t a1){ mov_imm_eax(a1); push_eax(); call_ptr(fn); }

/* 分支/跳转终止（延迟槽之后发射；跳板自修改，零栈交互）*/
static void dbg_cb(uint32_t tag){ if(!g_dbg_emit) return; mov_imm_eax(tag); push_eax(); call_ptr(H32(jh_dbg)); }
static int emit_term(uint32_t tk, uint32_t nt, int mode){ /* 0=恒跳tk 1=条件(栈槽flag) 2=halt@tk 3=跳nt */
    if(g_dbg_emit) fprintf(stderr,"[TERM2] mode=%d tk=%08x nt=%08x\n",mode,tk,nt);
    if(mode==2){ store_pc_imm(tk); b1(0xC6); b1(0x05); b4(ST+132u); b1(1); ret_(); return !ovf; }
    if(mode==0){ store_pc_imm(tk); ret_(); return !ovf; }
    if(mode==3){ store_pc_imm(nt); ret_(); return !ovf; }
    /* flag 语义：psx_state 空闲字节 ST+124=1→跳 tk。ZF 置 → jz 落 nt，fall-through 走 tk */
    b1(0xA0); b4(ST+140u); b1(0x84); b1(0xC0); b1(0x74); /* mov al,[ST+124]; test al,al; jz <槽> */
    uint8_t *disp=E; b1(0);
                              /* 槽地址在写入前捕获（b1 先写后 ++）*/
    b1(0xC7); b1(0x05); b4(PCR); b4(tk); b1(0xC3);       /* flag=1 路径（fall-through）*/
    uint8_t *nl=E;
    b1(0xC7); b1(0x05); b4(PCR); b4(nt); b1(0xC3);       /* flag=0 路径（jz 落点）*/
    *disp=(uint8_t)(nl-(disp+1));
    if(g_dbg_emit){ fprintf(stderr,"[PATCH] disp@%03x=%02x (nl=%03x)\n",(unsigned)(disp-code_pool),*disp,(unsigned)(nl-code_pool));
      fprintf(stderr,"[VERIFY] pool[1a4]=%02x pool=%p page=%x\n",(unsigned)code_pool[0x1a4],(void*)code_pool,(unsigned)((uintptr_t)code_pool&0xfff)); }
    return !ovf; }

/* 翻译一个基本块；返回入口 ptr，失败 NULL */
static void *emit_block(uint32_t vpc0){
    uint32_t vpc=vpc0;
    int pend=0, pend_mode=0; uint32_t pend_w=0; uint32_t pend_tk=0, pend_nt=0;
    if(pool_used+4096>POOL_CAP) return NULL;
    uint8_t *buf=code_pool+pool_used; pool_used+=4096;
    E=buf; EEND=buf+4096; EMAX=EEND;
    if(g_dbg_emit) fprintf(stderr,"[BUF] vpc=%08x buf=%p pool=%p used=%03x\n",vpc0,(void*)buf,(void*)code_pool,pool_used);
    for(int n=0;n<32;n++){
        uint32_t w; if(!ram_fetch(vpc,&w)){ jit_fail_pc=vpc; jit_fail_op=0xFFFFFFFFu;
            return NULL; }
        jit_fail_pc=vpc; jit_fail_op=w;
        uint32_t op=(w>>26)&63, rs=(w>>21)&31, rt=(w>>16)&31, rd=(w>>11)&31, sa=(w>>6)&31, f3=w&63;
        int32_t si=(int32_t)(int16_t)(w&0xFFFF); uint32_t uimm=w&0xFFFF;
        if(g_dbg_emit) fprintf(stderr,"[E] vpc=%08x w=%08x pend=%d\n",vpc,w,pend);
        int is_nop=(w==0 || (op==0 && rs==0 && f3==0 && rd==0));
        if(pend && is_nop){ vpc+=4;
            if(g_dbg_emit) fprintf(stderr,"[TERMn] mode=%d tk=%08x nt=%08x\n",pend_mode,pend_tk,pend_nt);
            if(pend_mode==4){
                if((pend_w&0xFC00003Fu)==0x09u){                                   /* jalr: 链接=jalr_pc+8(vpc 现=jalr+8) */
                    uint32_t lreg=(pend_w>>11)&31u;
                    if(jalrz_on>0 && lreg){ mov_imm_eax(vpc); push_eax(); rdd((int)pend_tk); push_eax(); call_ptr(H32(jh_jalrz)); }
                    if(lreg){ mov_imm_eax(vpc); wrr((int)lreg); }   /* nop 槽 flush 时 vpc 已=jalr+8 */
                }
                rdd((int)pend_tk); store_pc_eax(); ret_();
            }
            else if(!emit_term(pend_tk,pend_nt,pend_mode)) return NULL;
            return buf; }
        if(is_nop){ vpc+=4; continue; }
        if(pend){   /* 真延迟槽：先翻译执行本条；槽内再分支=弃块（BIOS 罕见）*/
            if(op==0 && (f3==8||f3==9||f3==12||f3==13)) return NULL;
            if(op>=2 && op<=7) return NULL; }
        switch(op){
        case 0: switch(f3){
            case 0: if(rd){ rdd(rt); mov_ecx_imm(sa&31); shl_eax_cl(); wrr(rd);} vpc+=4; if(pend)goto slot_done; continue;
            case 2: if(rd){ rdd(rt); mov_ecx_imm(sa&31); shr_eax_cl(); wrr(rd);} vpc+=4; if(pend)goto slot_done; continue;
            case 3: if(rd){ rdd(rt); mov_ecx_imm(sa&31); sar_eax_cl(); wrr(rd);} vpc+=4; if(pend)goto slot_done; continue;
            case 8: case 9: {   /* jr / jalr：rs→pc（延迟槽后发射）*/
                if(pend) return NULL;                 /* 槽内再分支：弃 */
                pend=1; pend_mode=4; pend_tk=rs; pend_nt=0; pend_w=w; vpc+=4; continue; }
            case 12: case 13:   /* syscall(kind=1) / break(kind=3)：延迟槽后停机 */
                if(pend){ if(!emit_term(pend_tk,pend_nt,2)) return NULL; return buf; }
                store_pc_imm(vpc+4); b1(0xC6); b1(0x05); b4(ST+132u); b1(1); b1(0xC6); b1(0x05); b4(HK); b1(f3==12?1:3); ret_(); return ovf?NULL:buf;
            case 16: case 17: case 18: case 19: case 24: case 25: case 26: case 27: {
                /* MIPS-I SPECIAL funct: 16/17/18/19=MFHI/MTHI/MFLO/MTLO, 24..27=MULT/MULTU/DIV/DIVU
                   (勘误 task-043: 原 case16/17 标 COP0 实为死码—COP0 在 op16/17 外层, 内层劫持 MFHI/MTHI 静默 NOP) */
                if(f3==16u){ b1(0xA1); b4((uint32_t)(uintptr_t)&jh_hi); if(rd) wrr(rd); }
                else if(f3==18u){ b1(0xA1); b4((uint32_t)(uintptr_t)&jh_lo); if(rd) wrr(rd); }
                else if(f3==17u){ rdd(rs); b1(0x89); b1(0x05); b4((uint32_t)(uintptr_t)&jh_hi); }
                else if(f3==19u){ rdd(rs); b1(0x89); b1(0x05); b4((uint32_t)(uintptr_t)&jh_lo); }
                else { rdd((int)rt); push_eax(); rdd(rs); push_eax();
                       call_ptr(H32(f3==24u?jh_mult:f3==25u?jh_multu:f3==26u?jh_div:jh_divu)); }
                vpc+=4; if(pend)goto slot_done; continue; }
            case 32: case 33: if(rd){ rdd(rs); set_edx_r((int)rt); add_eax_mem(); wrr(rd);} vpc+=4; if(pend)goto slot_done; continue;   /* add/addu: [rs]+[rt] — 勘误: EDX 须指 rt(原 [rs]+[rs] 自加 bug) */
            case 34: case 35: if(rd){ rdd(rs); set_edx_r((int)rt); sub_eax_mem(); wrr(rd);} vpc+=4; if(pend)goto slot_done; continue;
            case 36: if(rd){ rdd(rs); set_edx_r((int)rt); and_eax_mem(); wrr(rd);} vpc+=4; if(pend)goto slot_done; continue;
            case 37: if(rd){ rdd(rs); set_edx_r((int)rt); or_eax_mem(); wrr(rd);} vpc+=4; if(pend)goto slot_done; continue;
            case 38: if(rd){ rdd(rs); set_edx_r((int)rt); xor_eax_mem(); wrr(rd);} vpc+=4; if(pend)goto slot_done; continue;
            case 39: if(rd){ rdd(rs); set_edx_r((int)rt); or_eax_mem(); not_eax(); wrr(rd);} vpc+=4; if(pend)goto slot_done; continue;
            case 40: if(rd){ rdd(rs); neg_eax(); wrr(rd);} vpc+=4; if(pend)goto slot_done; continue;
            case 42: if(rd){ rdd(rs); set_edx_r((int)rt); cmp_eax_mem(); setcc_cl(0x0C); movzx_ecx_cl(); mov_eax_ecx(); wrr(rd);} vpc+=4; if(pend)goto slot_done; continue;
            case 43: if(rd){ rdd(rs); set_edx_r((int)rt); cmp_eax_mem(); setcc_cl(0x02); movzx_ecx_cl(); mov_eax_ecx(); wrr(rd);} vpc+=4; if(pend)goto slot_done; continue;
            default: return NULL;
            }
        case 8: if(rt){ rdd(rs); add_eax_imm(si); wrr(rt);} vpc+=4; if(pend)goto slot_done; continue;
        case 9: if(rt){ rdd(rs); add_eax_imm(si); wrr(rt);} vpc+=4; if(pend)goto slot_done; continue;
        case 10: if(rt){ rdd(rs); cmp_eax_imm(uimm); setcc_cl(0x0C); movzx_ecx_cl(); mov_eax_ecx(); wrr(rt);} vpc+=4; if(pend)goto slot_done; continue;
        case 11: if(rt){ rdd(rs); cmp_eax_imm(uimm); setcc_cl(0x02); movzx_ecx_cl(); mov_eax_ecx(); wrr(rt);} vpc+=4; if(pend)goto slot_done; continue;
        case 12: if(rt){ rdd(rs); b1(0x25); b4(uimm); wrr(rt);} vpc+=4; if(pend)goto slot_done; continue;   /* andi */
        case 13: if(rt){ rdd(rs); b1(0x0D); b4(uimm); wrr(rt);} vpc+=4; if(pend)goto slot_done; continue;   /* ori */
        case 14: if(rt){ rdd(rs); b1(0x35); b4(uimm); wrr(rt);} vpc+=4; if(pend)goto slot_done; continue;   /* xori */
        case 15: if(rt){ mov_imm_eax(uimm<<16); wrr(rt);} vpc+=4; if(pend)goto slot_done; continue;         /* lui */
        case 35: if(rt){ rdd(rs); add_eax_imm(si); push_eax(); call_ptr(H32(jh_lw));
            if(rt==31 && jalrz_on>0){ push_eax(); mov_imm_eax((uint32_t)(int32_t)si); push_eax(); mov_imm_eax(vpc-4); push_eax(); call_ptr(H32(jh_raw31)); }   /* lw ra 现场(task-050a2) */
            wrr(rt);} vpc+=4; if(pend)goto slot_done; continue;   /* lw */
        case 33: if(rt){ rdd(rs); add_eax_imm(si); push_eax(); call_ptr(H32(jh_lhu)); sx_eax_al(); wrr(rt);} vpc+=4; if(pend)goto slot_done; continue;   /* lh */
        case 32: if(rt){ rdd(rs); add_eax_imm(si); push_eax(); call_ptr(H32(jh_lb)); sx_byte_eax(); wrr(rt);} vpc+=4; if(pend)goto slot_done; continue;   /* lb */
        case 36: if(rt){ rdd(rs); add_eax_imm(si); push_eax(); call_ptr(H32(jh_lbu)); wrr(rt);} vpc+=4; if(pend)goto slot_done; continue;
        case 37: if(rt){ rdd(rs); add_eax_imm(si); push_eax(); call_ptr(H32(jh_lhu)); wrr(rt);} vpc+=4; if(pend)goto slot_done; continue;
        case 40: { rdd(rt); push_eax(); rdd(rs); add_eax_imm(si); push_eax(); call_ptr(H32(jh_sb)); } vpc+=4; if(pend)goto slot_done; continue;
        case 41: { rdd(rt); push_eax(); rdd(rs); add_eax_imm(si); push_eax(); call_ptr(H32(jh_sh)); } vpc+=4; if(pend)goto slot_done; continue;
        case 43: { rdd(rt); push_eax(); rdd(rs); add_eax_imm(si); push_eax(); call_ptr(H32(jh_sw)); } vpc+=4; if(pend)goto slot_done; continue;
        case 34: if(rt){ rdd(rt); push_eax(); rdd(rs); add_eax_imm(si); push_eax(); call_ptr(H32(jh_lwl)); wrr(rt);} vpc+=4; if(pend)goto slot_done; continue;
        case 38: if(rt){ rdd(rt); push_eax(); rdd(rs); add_eax_imm(si); push_eax(); call_ptr(H32(jh_lwr)); wrr(rt);} vpc+=4; if(pend)goto slot_done; continue;
        case 42: { rdd(rt); push_eax(); rdd(rs); add_eax_imm(si); push_eax(); call_ptr(H32(jh_swl)); } vpc+=4; if(pend)goto slot_done; continue;
        case 46: { rdd(rt); push_eax(); rdd(rs); add_eax_imm(si); push_eax(); call_ptr(H32(jh_swr)); } vpc+=4; if(pend)goto slot_done; continue;
        case 2: case 3: {   /* j / jal：定址跳（目标不可取指→halt）*/
            if(pend) return NULL;
            uint32_t tgt=((vpc+4)&0xF0000000u)|((w&0x3FFFFFFu)<<2);
            if(op==3 && !(tgt&0x1FFFFFFu) && getenv("H_JALZ"))   /* jal→不可取指零区: ra=pc+8 毒种(task-050) */
                { static int nz=0; if(jit_rc_log && nz<40){ nz++; fprintf(jit_rc_log,"JALZ#%d from=%08x delay=%08x ra=%08x v0=%08x t9=%08x\n",nz,vpc,vpc+8,psx_state.r[31],psx_state.r[2],psx_state.r[25]); fflush(jit_rc_log); } }
            if(op==3){ mov_imm_eax(vpc+8); wrr(31); }
            uint32_t dummy;
            pend=1; pend_mode= ram_fetch(tgt,&dummy)?0:2; pend_tk=tgt; pend_nt=tgt; vpc+=4; continue; }
        case 4: case 5: case 6: case 7: {   /* beq/bne/blez/bgtz：条件置 ECX，终止延到延迟槽后 */
            if(pend) return NULL;
            uint32_t tk=(vpc+4)+(uint32_t)((si<<2)), nt=vpc+8;   /* 不跳=跳过延迟槽 */
            if(g_dbg_emit) fprintf(stderr,"[RAW] vpc=%08x si=%d shl=%08x tk=%08x nt=%08x\n",vpc,si,(si<<2),tk,nt);
            if(op==4||op==5){ rdd(rs); set_edx_r(rt); cmp_eax_mem(); setcc_al(op==4?0x04:0x05); b1(0xA2); b4(ST+140u); }
            else { rdd(rs); cmp_eax_imm(0); setcc_al(op==6?0x06:0x0F); b1(0xA2); b4(ST+140u); }
            if(g_dbg_emit) fprintf(stderr,"[BR] vpc=%08x vpc4=%08x si=%d si2=%08x tk=%08x nt=%08x w=%08x\n",vpc,vpc+4,si,(si<<2),tk,nt,w);
            pend=1; pend_mode=1; pend_tk=tk; pend_nt=nt; vpc+=4; continue; }
        case 1: { uint32_t rt=(w>>16)&31u;                        /* REGIMM: bltz=0/bgez=1/bltzal=2/bgezal=3(task-043) */
            if(rt<=3u){
                if(pend) return NULL;
                uint32_t tk=(vpc+4)+(uint32_t)((si<<2)), nt=vpc+8;
                rdd(rs); cmp_eax_imm(0); setcc_al((rt==0u||rt==2u)?0x0Cu:0x0Du); b1(0xA2); b4(ST+140u);
                if(rt>=2u){   /* AL: r[0]=(flag? vpc+8 : 0) — 链接寄存器恒为 $0(r0 写忽略, 真机同义: 副作用置 ra? MIPS BLTZAL 连到 r31) */
                    b1(0x0F); b1(0xB6); b1(0xC0); b1(0x69); b1(0xC0); b4(nt); wrr(31); }   /* MIPS: B*ZAL 链到 $31 */
                pend=1; pend_mode=1; pend_tk=tk; pend_nt=nt; vpc+=4; continue; }
            return NULL; }
        case 16: case 17: { uint32_t f3c=(w>>6)&31u, rt=(w>>16)&31u, rd=(w>>11)&31u;
                if(((w>>21)&31u)==16 && (w&63u)==0x18u){        /* R3000A RFE：块终止 pc=cop0[14] */
                    if(pend) return NULL;
                    call_ptr(H32(jh_rfe)); store_pc_eax(); ret_(); return ovf?NULL:buf; }
                if(f3c==0 && (w&63u)==0 && rd<32){ rdd((int)rt); push_eax(); mov_imm_eax(rd); push_eax(); call_ptr(H32(jh_cop0_write)); }
                else if(f3c==1 && (w&63u)==1 && rd<32){ mov_imm_eax(rd); push_eax(); call_ptr(H32(jh_cop0_read)); if(rt)wrr((int)rt); }
                vpc+=4; if(pend)goto slot_done; continue; }
        case 18: case 19: vpc+=4; if(pend)goto slot_done; continue;
        default: return NULL;
        }
    slot_done:
        /* 延迟槽指令执行完毕 → 立即发射终止 */
        if(pend_mode==4){
            if((pend_w&0xFC00003Fu)==0x09u){ uint32_t lreg=(pend_w>>11)&31u;       /* jalr+非NOP槽: 槽已执行, vpc=jalr+8 */
                if(lreg){ mov_imm_eax(vpc); wrr((int)lreg); } }
            rdd((int)pend_tk); store_pc_eax(); ret_(); return ovf?NULL:buf; }
        if(!emit_term(pend_tk,pend_nt,pend_mode)) return NULL;
        return buf;
    }
    if(pend){ if(pend_mode==4){
            if((pend_w&0xFC00003Fu)==0x09u){ uint32_t lreg=(pend_w>>11)&31u;       /* jalr 槽=分支: 弃(上文 return NULL), 此处仅 jr 族 */
                if(lreg){ mov_imm_eax(vpc); wrr((int)lreg); } }
            rdd((int)pend_tk); store_pc_eax(); ret_(); } else if(!emit_term(pend_tk,pend_nt,pend_mode)) return NULL; return buf; }
    store_pc_imm(vpc); ret_();
    return ovf?NULL:buf;
}


/* ---------------- 调度器 ---------------- */
static struct { uint32_t vpc; void *code; } cc[CSLOTS];
static void cache_inval(void){ memset(cc,0,sizeof cc); pool_used=0; }

int jit_init(void){
    if(!code_pool){   /* 本机允许匿名 RWX（xtest2 实证）；memfd 路线因页缓存可见性怪癖放弃 */
        code_pool=mmap(NULL,POOL_CAP,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(code_pool==MAP_FAILED){ code_pool=0; return 1; } }
    if(!psx_ram){ psx_ram=mmap(NULL,8u<<20,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
                  if(psx_ram==MAP_FAILED) return 1; }
    cache_inval(); memset(&psx_state,0,sizeof psx_state); return 0;
}
int jit_run(uint32_t max_blocks){
    for(uint32_t i=0;i<max_blocks;i++){
        if(psx_state.halted) return 2;
        uint32_t h=(psx_state.pc>>2)&(CSLOTS-1), p=psx_state.pc;
        if(cc[h].vpc!=p || !cc[h].code){
            void *c=emit_block(p);
            if(g_dbg_emit) fprintf(stderr,"[EMIT] p=%08x -> %08x\n",p,(unsigned)(uintptr_t)c);
            if(!c){ psx_state.halted=1; psx_state.halt_kind=2; { FILE*f=jit_rc_log?jit_rc_log:devlog_fp(); if(f){fprintf(f,"EMITFAIL pc=%08x op=%08x ra=%08x sp=%08x v0=%08x t9=%08x\n",jit_fail_pc,jit_fail_op,psx_state.r[31],psx_state.r[29],psx_state.r[2],psx_state.r[25]); fflush(f);} } return 2; }
            cc[h].vpc=p; cc[h].code=c;
        } else if(g_dbg_emit) fprintf(stderr,"[HIT] p=%08x code=%08x h=%d\n",p,(unsigned)(uintptr_t)cc[h].code,h);
        { static int step=0; if(getenv("JIT_DEBUG")&&step<300){ uint32_t *u=(uint32_t*)cc[h].code;
          fprintf(stderr,"[B%u pc=%08x code=%p]",step,psx_state.pc,cc[h].code);
          for(int q=0;q<12;q++) fprintf(stderr," %08x",u[q]); fprintf(stderr,"\n");
          fflush(stderr); }
      step++;
      if(g_dbg_emit){ fflush(stderr); fprintf(stderr,"[DSP%u] pc=%08x code=%08x h=%d pu=%03x esp=%08x\n",step,p,(unsigned)(uintptr_t)cc[h].code,h,pool_used,(unsigned)(uintptr_t)__builtin_frame_address(0)); }
      if(g_dbg_emit){ fflush(stderr); }
      ((void(*)())cc[h].code)();
      if(g_dbg_emit){ fflush(stdout); fprintf(stderr,"[RDSP%u] pc=%08x halted=%d\n",step,psx_state.pc,psx_state.halted); fflush(stderr); } }
    }
    return 0;
}


int jit_init_dbg(void){ return jit_init(); }
/* 调试钩子（MJTEST/mbtest 共用） */
static int emit_block_dbg_any(uint32_t vpc, uint8_t **out, uint32_t *len){
    if(pool_used+4096>POOL_CAP) return 0;
    uint8_t *before=code_pool+pool_used;
    void *c=emit_block(vpc);
    if(!c) return 0;
    *out=(uint8_t*)c; *len=(uint32_t)(E-before); return 1;
}
#ifdef MJTEST
int emit_block_dbg(uint32_t vpc, uint8_t **out, uint32_t *len){ return emit_block_dbg_any(vpc,out,len); }
#endif
int emit_block_dbg_wrap(uint32_t v,uint8_t**o,uint32_t*l){ return emit_block_dbg_any(v,o,l); }

/* ---------------- htrace 库接口（H-10b BIOS-JIT 激活） ---------------- */
void jit_lib_flush_blocks(void){ cache_inval(); }
void jit_flush_range(uint32_t lo,uint32_t hi,int n){   /* n: 0=phys 基址, 1=0x80000000, 2=0xBFC00000 (task-050) */
    uint32_t base = n==1?0x80000000u:(n==2?0xBFC00000u:0u);
    for(int i=0;i<1024;i++){ if(cc[i].code && cc[i].vpc>=base+lo && cc[i].vpc<base+hi) cc[i].code=0; } }
uint32_t jit_lib_gpuw(void){ return g_gpuw; }
int  jit_lib_load_rom(const uint8_t *rom_shadow){ psx_rom=rom_shadow; return jit_init(); }
static int jit_rootcall_serve(void);
int  jit_lib_trace_boot(FILE*log,uint32_t entry,uint32_t max_blocks,uint8_t*win,uint32_t win_base,int backfill){
    if(!psx_ram) jit_init();
    psx_state.pc=entry; psx_state.halted=0; psx_state.halt_kind=0; cache_inval();
    static uint32_t seq[65536]; uint32_t nseq=0,done=0; uint32_t lastrep=entry, rep=0;
    static uint32_t uq[65536]; static uint32_t nuq=0; static uint8_t bloom[1048576];   /* task-048a2 UNIQUE */
    jit_rc_log=log;
    { const char*mb=getenv("H_MAXB"); if(mb) max_blocks=(uint32_t)atoi(mb); }   /* task-045: 大 memcpy 需 >425k */
    if(getenv("H_CDHLE")&&psx_ram){ extern void hlecd_install_pub(void); hlecd_install_pub(); }
    for(;done<max_blocks;done++){
        if(psx_state.halted) break;
        uint32_t p=psx_state.pc, h=(p>>2)&(CSLOTS-1);
        { static int rc=-1; static uint32_t pra4=0; static int nc=0;
          if(rc<0){ rc=!!getenv("H_RACATCH"); pra4=psx_state.r[31]; }
          if(rc && psx_state.r[31]==4u && pra4!=4u && nc<4){ nc++;
            if(log){ uint32_t i; fprintf(log,"RACATCH#%d pc=%08x sp=%08x oldra=%08x prev:",nc,p,psx_state.r[29],pra4);
              for(i=(nseq>8?nseq-8:0); i<nseq; i++) fprintf(log," %08x",seq[i&(65536-1)]);
              fprintf(log,"\n"); fflush(log);} }
          if(rc) pra4=psx_state.r[31]; }
        { static int ch=-1; if(ch<0) ch=!!getenv("H_C2HOME");   /* task-043 homebrew C2 根协议面 min 供给(docs/32) */
          if(ch && p==0xb0u && psx_ram){ uint32_t ci=psx_state.r[9];        /* 0xB0 根call向量: t1=cmd, 表 RAM[0x100+cmd*4](task-044) */
            if(ci<=0x47u){ uint32_t *sl=(uint32_t*)(psx_ram+0x100u+ci*4u);
                if(*sl==0u||*sl>0x200000u){ memcpy(psx_ram+0x95100u,c2stub_words,sizeof c2stub_words); *sl=0x95100u;
                    if(log){fprintf(log,"C2HOME rootvec [100+%x*4]->0x95100 cmd=%02x\n",ci,ci); fflush(log);} } } }
          if(ch){ static int cg=-1,gdone=0; if(cg<0) cg=!!getenv("H_C2GOLD");   /* 自研 C2 区切片消费(docs/32 §5, .analysis 不入 git) */
            if(cg && !gdone && psx_ram){ const char*gpath=getenv("H_C2GOLD"); if(gpath[0]!='/') gpath="/home/gill/bleem/.analysis/golden/golden_c2_slice_30000_90000.bin";
                FILE*fp=fopen(gpath,"rb");
                gdone=1;

                if(fp){ static uint32_t *scr; if(!scr) scr=malloc(0x60000);
                    size_t rd=fread(scr,1,0x60000,fp); fclose(fp); unsigned nf=0;
                    uint32_t *live=(uint32_t*)(psx_ram+0x30000);
                    { static int eg=-1; uint32_t endgate=0x60000u;   /* H_C2END: 切片上界门(task-048a3, 默认 0x89E00 phys→off 0x59E00) */
                      if(eg<0){ const char*e=getenv("H_C2END"); endgate=e?(uint32_t)strtoul(e,0,0):0x89E00u; eg=1; }
                      uint32_t ng=0;
                      for(size_t i=0;i+4<=rd;i+=4){ if(0x30000u+(uint32_t)i>=endgate){ if(scr[i/4]&&!live[i/4]) ng++; continue; } if(!live[i/4] && scr[i/4]){ live[i/4]=scr[i/4]; nf++; } }   /* 只填零洞 + phys 上界门(task-048a3) */
                    if(log){fprintf(log,"C2GOLD fillhole bytes=%u words_filled=%u gated=%u end=+%05x sp=%08x\n",(unsigned)rd,nf,ng,endgate,psx_state.r[29]); fflush(log);} } }
                { FILE*f2=fopen("/home/gill/bleem/.analysis/golden/golden_c2_slice_low_500_900.bin","rb");   /* 低区派发表终态填洞(task-048) */
                  if(f2){ uint32_t lw2[4096]; size_t r2=fread(lw2,1,sizeof lw2,f2); fclose(f2); unsigned nf2=0;
                    for(size_t i=0;i+4<=r2;i+=4){ uint32_t*a=(uint32_t*)(psx_ram+0x500+i/4*4); if(!*a && lw2[i/4]){ *a=lw2[i/4]; nf2++; } }
                    if(log){fprintf(log,"C2GOLD lowfill words=%u [674]=%08x\n",nf2,*(uint32_t*)(psx_ram+0x674)); fflush(log);} } }
                { FILE*f3=fopen("/home/gill/bleem/.analysis/golden/c2_state_patch.bin","rb");   /* C2 init 后状态词强写(653 差集, task-048) */
                  if(f3){ uint32_t cnt=0,nw=0; if(fread(&cnt,4,1,f3)==1){
                      for(uint32_t k=0;k<cnt && k<4096;k++){ uint32_t o,v; if(fread(&o,4,1,f3)!=1||fread(&v,4,1,f3)!=1) break;
                        { static int eg2=-1; uint32_t egate=0xFFFFFFFFu; if(eg2<0){ const char*e=getenv("H_C2END"); if(e) egate=(uint32_t)strtoul(e,0,0); eg2=1; }
                          if(o>=0x30000&&o<0x30000+0x67FF0 && o<egate){ *(uint32_t*)(psx_ram+o)=v; nw++; } } } }
                    fclose(f3);
                    { const char*cs=getenv("H_C2STATE");
                      if(cs && psx_ram){ *(uint32_t*)(psx_ram+0x89e2c)=(uint32_t)strtoul(cs,0,0);
                        if(log){fprintf(log,"C2GOLD state-override [89e2c]=%08x\n",*(uint32_t*)(psx_ram+0x89e2c)); fflush(log);} } }
                    if(log){fprintf(log,"C2GOLD statepatch applied=%u [89e2c]=%08x\n",nw,*(uint32_t*)(psx_ram+0x89e2c)); fflush(log);} }
                if(getenv("H_C2SENT") && psx_ram){   /* 帧槽哨兵预置+自研续跑 handler(task-050a3) */
                    static const uint32_t h[7]={0x3C028002u,0x3442C1C0u,0xAC420000u,0x24020001u,0x03E00008u,0x00000000u,0x00000000u};
                    memcpy(psx_ram+0x4C1B0u,h,28);
                    *(uint32_t*)(psx_ram+0x1FFED4u)=0x8004C1B0u;
                    extern void jit_flush_range(uint32_t lo,uint32_t hi,int n);
                    jit_flush_range(0x4C1B0u,0x4C1D0u,0); jit_flush_range(0x4C1B0u,0x4C1D0u,1); jit_flush_range(0x4C1B0u,0x4C1D0u,2);
                    jit_flush_range(0u,16u,0); jit_flush_range(0u,16u,1); jit_flush_range(0u,16u,2);   /* 清 4 中毒块 */
                    if(log){fprintf(log,"C2SENT installed slot[1ffed4]=8004C1B0 handler@4C1B0\n"); fflush(log);} }
                if(getenv("H_JZSLED") && psx_ram){   /* 表改写后: RAM 0x0 ack 跳板 + 向量页定向 flush(task-050) */
                    uint32_t s[4]={0x24020001u,0x03E00008u,0x24020001u,0x00000000u};
                    if(!*(uint32_t*)psx_ram) memcpy(psx_ram,s,16);
                    extern void jit_flush_range(uint32_t lo,uint32_t hi,int n);
                    jit_flush_range(0x000u,0x440u,1); jit_flush_range(0x000u,0x440u,2); jit_flush_range(0x000u,0x440u,0);
                    if(log){fprintf(log,"JZSLED installed+vecflush\n"); fflush(log);} } } } }
          if(ch && p==0x600u && psx_ram){ uint32_t s=*(uint32_t*)(psx_ram+0x674u);
            uint32_t idx=psx_state.r[9];            /* 派发块: v0=0x674; t1=cmd 偏移; t0=[v0+t1]; jr t0 */
            uint32_t *slot=(uint32_t*)(psx_ram+((0x674u+idx)&0x1FFFFFu));
            uint32_t *slot2=(uint32_t*)(psx_ram+((0x674u+(idx<<2))&0x1FFFFFu));
            if(log && idx){ fprintf(log,"C2HOME visit [674+%x]=%08x [674+%x*4]=%08x v0=%08x\n",idx,*slot,*slot2,psx_state.r[2]); fflush(log); }
            if(*slot==0u || *slot>0x200000u){
              static const uint32_t hw[]={0x3C028008u,0x34429F00u,0x03E00008u,0x00000000u}; /* lui v0,0x8008; ori v0,0x9F00; jr ra; nop */
              memcpy(psx_ram+0x95000u,hw,16); *slot=0x95000u; *slot2=0x95000u;
              if(!getenv("H_C2GOLD")) *(uint32_t*)(psx_ram+0x89E7Cu)=0x80089F00u;   /* 非 GOLD: 自装 CB 指针 */
              if(log){fprintf(log,"C2HOME install [674+%x]->0x95000\n",idx); fflush(log);} } } }
        { static int rh=-1, nred=0; if(rh<0) rh=!!getenv("H_ROOTHLE");
          if(rh && (p>>28)==3 && p<0x3C160000u && psx_state.r[2]==0x80000010u && nred<8){
              nred++; p=0xBFC059E0u; psx_state.pc=p; h=(p>>2)&(CSLOTS-1);   /* NewRootCmd → ROM+0x59E0 handler */
              if(log){fprintf(log,"ROOTHLE redirect->%08x (#%d)\n",p,nred); fflush(log);} } }
        { static int lrep=-1; if(lrep<0){ const char*e=getenv("H_LOOPREP"); lrep=e?atoi(e):20000; } if(p==lastrep){ if(++rep>lrep){ if(log){fprintf(log,"LOOP-DETECT pc=%08x\n",p); fflush(log);} break; } } else { lastrep=p; rep=0; } }
        { static uint32_t pra=0xDEAD0001u; static int rw=-1; if(rw<0) rw=getenv("H_RAWATCH")?1:0; if(rw&&psx_state.r[31]!=pra){ FILE*f=jit_rc_log?jit_rc_log:devlog_fp(); if(f){fprintf(f,"RAW pc=%08x ra:%08x->%08x\n",p,pra,psx_state.r[31]); fflush(f);} pra=psx_state.r[31]; } }
        if(cc[h].vpc!=p || !cc[h].code){ void*c=emit_block(p);            if(!c){ psx_state.halted=1; psx_state.halt_kind=2; { FILE*f=jit_rc_log?jit_rc_log:devlog_fp(); if(f){fprintf(f,"EMITFAIL pc=%08x op=%08x ra=%08x sp=%08x v0=%08x t9=%08x\n",jit_fail_pc,jit_fail_op,psx_state.r[31],psx_state.r[29],psx_state.r[2],psx_state.r[25]);
                      for(uint32_t q=(nseq>8?nseq-8:0); q<nseq && q<65536;q++) fprintf(f,"  PREV[%u]=%08x\n",q,seq[q]);
                      fprintf(f,"  UNIQ-TAIL(%u):",nuq); for(uint32_t q=(nuq>12?nuq-12:0);q<nuq;q++) fprintf(f," %08x",uq[q]); fprintf(f,"\n");

                      fprintf(f,"  ra=%08x t9=%08x v0=%08x\n",psx_state.r[31],psx_state.r[25],psx_state.r[2]); fflush(f);} } break; } cc[h].vpc=p; cc[h].code=c; }
        if(nseq<65536) seq[nseq++]=p; else done=1;
        { static int irq=-1; static uint32_t nirq=0; static uint32_t lastirqb=0;   /* H_IRQ: vblank 节拍注入(task-049a2) */
          if(irq<0){ const char*e=getenv("H_IRQ"); irq=e?(int)strtol(e,0,0):0; }
          if(irq>0 && (int)nseq>irq && nseq-lastirqb>(uint32_t)irq && nirq<8u){
            lastirqb=nseq; nirq++;
            dev_regs[0x1070>>2] |= 2u;               /* I_STAT bit1 VBlank=raised */
            dev_regs[0x1090>>2] |= 2u;               /* I_CTRL bit1 enable */
            cop0[13] |= 8u;                          /* Cause.IP2 */
            if(log){fprintf(log,"IRQINJ#%u at=%08x blk=%u\n",nirq,p,nseq); fflush(log);}
            uint32_t h=jh_lw(0xFFFE0130u); if(!h) h=jh_lw(0x130); if(!h) h=0x1E988u;
            h&=0x1FFFFFFFu; if(h<0x80000u) h+=0xBFC00000u;
            cop0[COP0_EPC]=psx_state.pc; cop0[12]|=1u<<30;
            psx_state.pc=h; if(log){fprintf(log,"IRQINJ -> h=%08x\n",h); fflush(log);} } }
        { uint32_t bk=((p*2654435761u)>>15)&(1048576u-1);
          if(!bloom[bk] && nuq<65536){ bloom[bk]=1; uq[nuq++]=p; if(getenv("H_JITUNIQ")) fprintf(log,"UNIQ %08x\n",p); } }
        { static int st2=0; if(getenv("JIT_DEBUG")&&st2<400){ uint32_t *u=(uint32_t*)cc[h].code;
            fprintf(stderr,"[T%u pc=%08x code=%p]",st2,p,cc[h].code);
            for(int q=0;q<60;q++) fprintf(stderr," %08x",u[q]); fprintf(stderr,"\n"); fflush(stderr); }
          st2++; }
        ((void(*)())cc[h].code)();
        if(psx_state.halted && (psx_state.halt_kind==1||psx_state.halt_kind==3)){
            if(jit_rootcall_serve()) break; }
    }
    if(log){
        /* 压缩 PC 序列：连续 vpc 段合并为 lo..hi */
        fprintf(log,"BIOS-JIT-CHAIN blocks=%u final=%08x halted=%d hk=%u pc0=%08x s0=%08x s2=%08x s5=%08x t7=%08x ra=%08x t9=%08x v0=%08x\n",nseq,psx_state.pc,psx_state.halted,psx_state.halt_kind,psx_state.r[0],psx_state.r[16],psx_state.r[18],psx_state.r[21],psx_state.r[15],psx_state.r[31],psx_state.r[25],psx_state.r[2]);
        { uint32_t a=0x80040038u,h2=(a>>2)&(CSLOTS-1);
          if(log&&psx_ram){ fprintf(log,"DUMP38 ram=%08x %08x %08x %08x\n",*(uint32_t*)(psx_ram+0x38),0,0,0);
            fprintf(log,"DUMP38 body:"); for(int q=14;q<22;q++) fprintf(log," %08x",((uint32_t*)(psx_ram))[q]);
            fprintf(log,"\nDUMP38 slot vpc=%08x code=%p\n",cc[h2].vpc,(void*)cc[h2].code);
            if(cc[h2].code){ fprintf(log,"DUMP38 x86:"); for(int q=0;q<12;q++) fprintf(log," %08x",((uint32_t*)cc[h2].code)[q]); fprintf(log,"\n"); }
            fflush(log); } }
        if(getenv("H_JITBLOCKS")){ uint32_t st=0; for(uint32_t i=st;i<nseq;i++) fprintf(log,"BLK %08x\n",seq[i&(65536-1)]);
            if(psx_ram){ uint32_t base[]={0x0A0100u,0x0A0140u,0x40000u,0x50000u,0x41000u};
                for(unsigned q2=0;q2<5;q2++) for(uint32_t o=0;o<0x20;o+=4) fprintf(log,"RAMB %06x=%08x\n",base[q2]+o,*(uint32_t*)(psx_ram+base[q2]+o)); }
            fflush(log); }
        uint32_t i=0; int segs=0;
        while(i<nseq && segs<64){
            uint32_t lo=seq[i],hi=lo; uint32_t j=i+1;
            while(j<nseq && seq[j]==hi+4){ hi=seq[j]; j++; }
            if(hi==lo) fprintf(log,"BIOS-JIT-PC %08x\n",lo); else fprintf(log,"BIOS-JIT-PC %08x..%08x\n",lo,hi);
            segs++; i=j;
        }
        fflush(log);
    }
    if(backfill && win){
        int n=0; uint32_t first=0,last=0;
        for(int k=0;k<CSLOTS;k++){
            uint32_t v=cc[k].vpc; void*code=cc[k].code;
            if(!code || v<win_base || v>=win_base+0x80000u) continue;
            uint32_t off=v-win_base;
            int32_t rel=(int32_t)((char*)code-(char*)(win+off+5));
            win[off]=0xE9; memcpy(win+off+1,&rel,4);      /* jmp rel32 → JIT 生成块 */
            if(!first) first=v; last=v; n++;
        }
        if(log){ fprintf(log,"BIOS-BACKFILL n=%d first=%08x last=%08x\n",n,first,last); fflush(log); }
    }
    return (int)nseq;
}
/* 引擎期 BIOS 调用：从窗入口执行到停机面（syscall），返回块数与 v0 */
/* rootcall/syscall 停机面处置：cmd=a0(r4)。当前最小集 = 全 no-op 放行
 * （pc 已在 syscall 下一条=jr $31，续跑自然归位）。返回 0=可续跑 1=停机。 */
static void hleboot_install(void);
static int g_hle_done=0;
uint32_t jit_lib_lowrd(void){ return g_lowrd; }
static int jit_rootcall_serve(void){
    if(!g_hle_done && getenv("H_HLEBOOT") && !getenv("H_HLE_EARLY")){ hleboot_install(); g_hle_done=1; }  /* boot_init 之后再终态预放(0x3cc 哨兵覆盖后) */
    if(getenv("H_EXNVEC")){   /* 金标准终态语义: 异常走 RAM 向量页 0x80(真机安装态), 非 boot 期 0x130 表 */
        uint32_t epc=psx_state.pc;
        cop0[14]=epc+8; cop0[13]=(cop0[13]&~0x7Fu)|0x20u; cop0[12]|=(1u<<30);
        psx_state.pc=0x80u; psx_state.halted=0; psx_state.halt_kind=0;
        if(jit_rc_log){ fprintf(jit_rc_log,"EXNVEC epc=%08x\n",epc); fflush(jit_rc_log); }
        return 0;
    }
    if(psx_state.halt_kind!=1 && psx_state.halt_kind!=3) return 1;
    uint32_t cmd=psx_state.r[4];
    static int exn=-1;
    if(exn<0) exn=!!getenv("H_BIOSEXN");
    if(exn && (cop0[12]&(1u<<30))){   /* CEL=1 分发器内再 syscall = R3000 直接递归执行入口(不重置 EPC) */
        uint32_t h2=jh_lw(0xFFFE0130u); if(!h2) h2=jh_lw(0x130); h2&=0x1FFFFFFFu;
        if(h2<0x80000u) h2+=0xBFC00000u;
        if(h2){ psx_state.pc=h2; psx_state.halted=0; psx_state.halt_kind=0;
            if(jit_rc_log){ fprintf(jit_rc_log,"EXN2 cmd=%u -> h=%08x\n",cmd,h2); fflush(jit_rc_log); }
            return 0; }
    }
    if(exn){   /* 真异常语义：CPU 通用异常 → [RAM 0x130] handler（BIOS 自装分发器）*/
        uint32_t h=jh_lw(0xFFFE0130u);
        if(!h) h=jh_lw(0x130);
        if(!h){ static int chh=-1; if(chh<0) chh=!!getenv("H_C2HOME");
            if(chh) h=0x1E988u; }   /* C2HOME(task-044): 表被 BIOS 自清后忠实回退 ROM 异常分发器 */
        h&=0x1FFFFFFFu;
        if(h<0x80000u) h+=0xBFC00000u;   /* 金标准: 表内为 BIOS ROM 相对偏移(trap/entry, 如 0x1E988=异常分发器 ROM 直执) */
        if(h && (h<0x200000u || (h&0x1FC00000u)==0x1FC00000u)){
            cop0[COP0_EPC]=psx_state.pc+4;                    /* EPC = syscall+8 */
            cop0[13]=(psx_state.halt_kind==3)?0xC0u:0x80u;    /* Cause: break 0x30 / syscall 0x20 (bits7-2) */
            cop0[12]|=1u<<30;                                 /* SR.CEL=1 */
            psx_state.pc=h; psx_state.halted=0; psx_state.halt_kind=0;
            if(jit_rc_log){ fprintf(jit_rc_log,"EXN cmd=%u epc=%08x -> h=%08x sp=%08x ra=%08x hk=%u v0=%08x t9=%08x\n",cmd,cop0[COP0_EPC],h,psx_state.r[29],psx_state.r[31],psx_state.halt_kind,psx_state.r[2],psx_state.r[25]); fflush(jit_rc_log); }
            return 0;
        }
    }
    if(jit_rc_log){ fprintf(jit_rc_log,"ROOTCALL cmd=%u pc=%08x ra=%08x pass\n",cmd,psx_state.pc,psx_state.r[31]); fflush(jit_rc_log); }
    psx_state.halted=0; psx_state.halt_kind=0;
    return 0;
}
int jit_lib_bios_call_log(uint32_t entry_pc,uint32_t budget,uint32_t *ret_v0,FILE*log);
int jit_lib_serve(void){ return jit_rootcall_serve(); }   /* probe 用: 单步 syscall/rootcall 放行 */

/* H-13b 路线 b: HLE kernel-RAM 终态预放（docs/21）。分发器(ROM 0x1E988)的
 * boot 状态机 callee 全在 RAM C2 区(0x3xxxx-0x5xxxx)——真机由 boot DMA 装载。
 * HLE: 内核镜像复制 + 9 个 callee 打可控返回桩 + DAT 字段终态。 */
static void hle_stub(uint32_t a,int v){ if(!psx_ram) return;
    uint32_t *p=(uint32_t*)(psx_ram+a);
    p[0]= v>=0 ? (0x24020000u|(uint32_t)(v&0xffff)) : 0u;  /* addiu v0,z0,v (或 nop) */
    p[1]=0x03E00008u; p[2]=0u;                              /* jr ra; nop */
}
void hleboot_install(void){
    if(!psx_ram||!psx_rom) return;
    const char*gp=getenv("H_GOLDEN");
    if(gp&&*gp){   /* 金标准: Beetle 跑通 Sony 码+autorun 失败的终态 RAM(取证区文件, 不入库) */
        FILE*gf=fopen(gp,"rb");
        if(gf){ fseek(gf,0,SEEK_END); long n=ftell(gf); fseek(gf,0,SEEK_SET);
            if(n>0x200000) n=0x200000;
            if(fread(psx_ram,1,n,gf)==(size_t)n){ fclose(gf); return; }
            fclose(gf);
        }
    }
    memcpy(psx_ram+0x500,(const uint8_t*)psx_rom+0x10000,0x8BF0);           /* FUN_bfc00420 复制 */
    memcpy(psx_ram+0x9100,"Licensed by Sony Computer Entertainment Inc.",41);  /* model 串(避开向量页) */
    hle_stub(0x32E24,2);   /* root2 线程入口 → boot 成功推进 */
    hle_stub(0x33D58,0);   /* 线程枚举 → 0 */
    hle_stub(0x34998,0);   /* 设备枚举 → 0 */
    hle_stub(0x336EC,-1);  /* TCB free → 空 */
    hle_stub(0x376AC,0);   /* 设备 init 路径 */
    hle_stub(0x37FA8,0);   /* devCommand 转发 */
    hle_stub(0x37CC8,0);
    hle_stub(0x5AA00,0);
    hle_stub(0x5719C,0);
    /* cdrom: devsw 注册(内核设备表)——open/read 返"无盘"错误, 让 autorun 走失败继续 */
    uint32_t cds=0x66B00;
    uint32_t cfn=0x66C00;
    for(int i=0;i<8;i++){ hle_stub(cfn+i*12,-1); }   /* 全返 -1(addiu v0,z0,0xffff = -1) */
    uint32_t *c=(uint32_t*)(psx_ram+cds);
    #define G(i) (0x80000000u|cfn+i*12)
    c[0]=0x00008001u; c[1]=G(0); c[2]=G(1); c[3]=G(2); c[4]=G(3); c[5]=G(4); c[6]=G(5); c[7]=G(6); c[8]=G(7); c[9]=G(0);
    memcpy(psx_ram+0x66A80,"cdrom:",7); c[10]=0x80000000u|0x66A80;
    #undef G
    /* 设备状态字段(分发器反编译): DevCtl 就绪态 */
    *(uint32_t*)(psx_ram+0x66940)=0x3B;
    *(uint32_t*)(psx_ram+0x66948)=0x32;
    /* RAM 向量表(金标准 docs/18): 0x80 通用异常 → j RAM 0xC80(复制块内真 handler) */
    *(uint32_t*)(psx_ram+0x80)=0x08000320u;              /* j 0xC80 */
    for(uint32_t o=0x84;o<0xC0;o+=4) *(uint32_t*)(psx_ram+o)=0x03E00008u;  /* 向量页 fence: jr ra */
    g_vecprot=1;
    *(uint32_t*)(psx_ram+0xA0)=0x08000320u;              /* A0h 表入口→同 handler */
    *(uint32_t*)(psx_ram+0xB0)=0x08000320u;
    *(uint32_t*)(psx_ram+0xC0)=0x08000320u;
    *(uint32_t*)(psx_ram+0x79E44)=2;      /* boot 完成态(_DAT_80079e44) */
    *(uint32_t*)(psx_ram+0x79E5C)=1;
    *(uint32_t*)(psx_ram+0x150)=0x80032E24u; *(uint32_t*)(psx_ram+0x158)=0x801FFF00u;
    /* TTY devsw 注册（PSX deviceSwitch 0x28 字节布局；桩函数=jr ra 返 1） */
    uint32_t ds=0x90000;                  /* devsw 结构 @RAM 0x90000, 函数体 @0x90100+ */
    uint32_t fn=0x90100;
    for(int i=0;i<8;i++){ hle_stub(fn+i*12,1); }   /* 8 个入口桩(返 1) */
    #define F(i) (0x80000000u|fn+i*12)
    uint32_t *d=(uint32_t*)(psx_ram+ds);
    d[0]=0x00008001u;      /* version/magic */
    d[1]=F(0);             /* function(cmd,args) */
    d[2]=F(1);             /* init  */
    d[3]=F(2);             /* open  */
    d[4]=F(3);             /* close */
    d[5]=F(4);             /* read  */
    d[6]=F(5);             /* write */
    d[7]=F(6);             /* ioctln */
    d[8]=F(7);             /* input  */
    d[9]=F(0);             /* output */
    d[10]=0x80000000u|0x86; /* dev name "tty:" @RAM 0x86 区? 用独立名串 */
    memcpy(psx_ram+0x86,"tty:",5);
    *(uint32_t*)(psx_ram+0x64)=0x80000000u|ds;   /* stdin devsw */
    *(uint32_t*)(psx_ram+0x68)=0x80000000u|ds;   /* stdout devsw(替 0xFF 哨兵) */
    *(uint32_t*)(psx_ram+0x6c)=0x80000000u|ds;   /* stderr devsw */
    #undef F
}
/* H-18a hlecd: 自研 C2 rootcall 后端(生成码 host/hlecd_code.h, ABI 见 docs/29) */
#include "hlecd_code.h"
static uint8_t g_cd_sec[2048]; static uint32_t g_cd_secno=0x10;
void hlecd_install(void){
    if(!psx_ram||!psx_rom) return;
    if(getenv("H_CDHLE_EXE")){   /* 直供第一方 homebrew EXE 首 2048B(=PS-X EXE 头扇区, docs/29) */
        FILE*fd=fopen(getenv("H_CDHLE_EXE"),"rb");
        if(fd){ size_t got=fread(g_cd_sec,1,2048,fd); fclose(fd);
                if(jit_rc_log){ fprintf(jit_rc_log,"CDHLE exe bytes=%zu magic=%.4s\n",got,(char*)g_cd_sec); fflush(jit_rc_log); } }
    } else if(getenv("H_CDHLE_DISC")){
        FILE*fd=fopen(getenv("H_CDHLE_DISC"),"rb");
        if(fd){ fseek(fd,0,SEEK_END); long sz=ftell(fd);
                long off=(g_cd_secno+150)*2352;
                if(off+2048<=sz){ fseek(fd,off,SEEK_SET); fread(g_cd_sec,1,2048,fd); }
                fclose(fd); }
    }
    memcpy(psx_ram+0x500,(const uint8_t*)psx_rom+0x10000,0x8BF0);
    memcpy(psx_ram+0x41000u, g_cd_sec, 2048u);
    memcpy(psx_ram+0x40000u, g_hlecd_code, sizeof g_hlecd_code);
    uint32_t *q=(uint32_t*)(psx_ram+0x32E24u);
    q[0]=0x08010000u; q[1]=0; q[2]=0; q[3]=0;
    *(uint32_t*)(psx_ram+0x150u)=0x80032E24u; *(uint32_t*)(psx_ram+0x154u)=0u; *(uint32_t*)(psx_ram+0x158u)=0x801FFF00u;
    *(uint32_t*)(psx_ram+0x0A0100u)=0x1001u;   /* 0x802A0100 经 phys 2MB 回绕=物理 0xA0100(真机同义) */
    if(jit_rc_log){ fprintf(jit_rc_log,"CDHLE install sec=%u disc=%s magic=%.8s\n",g_cd_secno,getenv("H_CDHLE_DISC")?getenv("H_CDHLE_DISC"):"(none)",(char*)g_cd_sec); fflush(jit_rc_log); }
}

void hlecd_install_pub(void){ hlecd_install(); }
unsigned char* jit_lib_ram_pub(void){ return psx_ram; }
int jit_lib_bios_call2(uint32_t entry_pc,uint32_t budget,uint32_t*v0,uint32_t*kind,uint32_t*endpc){
    if(!psx_ram) jit_init();   /* htrace 路径不显式 init: 惰性补齐(否则 RAM fetch 全灭) */
    if(getenv("H_HLEBOOT")&&getenv("H_HLE_EARLY")) hleboot_install();
    if(getenv("H_CDHLE")&&psx_ram&&!getenv("H_CDHLE_DONE")) hlecd_install();
    if(getenv("H_ROOT2")&&psx_ram&&!getenv("H_ROOT2_DONE")){   /* docs/19: root2 线程入口 HLE 桩 + RAM 0x150 头 */
        uint32_t *q=(uint32_t*)(psx_ram+0x32E24u);
        q[0]=0x24020001u; q[1]=0x03E00008u; q[2]=0u;
        *(uint32_t*)(psx_ram+0x150u)=0x80032E24u; *(uint32_t*)(psx_ram+0x154u)=0u; *(uint32_t*)(psx_ram+0x158u)=0x801FFF00u;
    }
    int n=jit_lib_bios_call_log(entry_pc,budget,v0,jit_rc_log);
    if(kind)*kind=psx_state.halt_kind; if(endpc)*endpc=psx_state.pc;
    return n;
}
int jit_lib_bios_call_log(uint32_t entry_pc,uint32_t budget,uint32_t *ret_v0,FILE*log){
    if(!psx_ram) jit_init();   /* htrace 路径不显式 init: 惰性补齐(否则 RAM fetch 全灭) */
    psx_state.pc=entry_pc; psx_state.halted=0; psx_state.halt_kind=0; jit_rc_log=log;
    int n=0; uint32_t lastrep=entry_pc, rep=0; static uint32_t hb[65536];
    for(;n<(int)budget;n++){
        uint32_t p=psx_state.pc,h=(p>>2)&(CSLOTS-1); hb[n&(65536-1)]=p; jit_cur_pc=p;
        { static int lrep=-1; if(lrep<0){ const char*e=getenv("H_LOOPREP"); lrep=e?atoi(e):20000; } if(p==lastrep){ if(++rep>lrep){ if(log){fprintf(log,"LOOP-DETECT pc=%08x\n",p); fflush(log);} break; } } else { lastrep=p; rep=0; } }
        { static uint32_t pra=0xDEAD0001u; static int rw=-1; if(rw<0) rw=getenv("H_RAWATCH")?1:0; if(rw&&psx_state.r[31]!=pra){ FILE*f=jit_rc_log?jit_rc_log:devlog_fp(); if(f){fprintf(f,"RAW pc=%08x ra:%08x->%08x\n",p,pra,psx_state.r[31]); fflush(f);} pra=psx_state.r[31]; } }
        if(cc[h].vpc!=p || !cc[h].code){ void*c=emit_block(p);            if(!c){ psx_state.halted=1; psx_state.halt_kind=2; { FILE*f=jit_rc_log?jit_rc_log:devlog_fp(); if(f){fprintf(f,"EMITFAIL pc=%08x op=%08x ra=%08x sp=%08x v0=%08x t9=%08x\n",jit_fail_pc,jit_fail_op,psx_state.r[31],psx_state.r[29],psx_state.r[2],psx_state.r[25]);
                      { for(int q=((int)n>8?(int)n-8:0); q<(int)n;q++) fprintf(f,"  PREV[%d]=%08x\n",q,hb[q&(65536-1)]); }
                      fprintf(f,"  ra=%08x t9=%08x v0=%08x\n",psx_state.r[31],psx_state.r[25],psx_state.r[2]); fflush(f);} } break; } cc[h].vpc=p; cc[h].code=c; }
        ((void(*)())cc[h].code)();
        if(psx_state.halted){ if(jit_rootcall_serve()) break; }
    }
    if(getenv("H_JITBLOCKS")&&log&&psx_ram){ for(uint32_t o=0x00;o<0x100;o+=4) fprintf(log,"RAMB %04x=%08x\n",o,*(uint32_t*)(psx_ram+o)); fflush(log); }
    if(getenv("H_JITBLOCKS")&&log){ uint32_t st=0; for(uint32_t i=st;i<(uint32_t)n;i++) fprintf(log,"BLK %08x\n",hb[i&(65536-1)]); fprintf(log,"BLK end=%08x\n",psx_state.pc); fflush(log); }
    if(ret_v0)*ret_v0=psx_state.r[2];
    return n;
}

/* ---------------- 自测 ---------------- */
#ifdef MJTEST
int emit_block_dbg(uint32_t vpc, uint8_t **out, uint32_t *len);
int main(void){
    if(jit_init()){ printf("init fail\n"); return 1; }
    uint32_t prog[]={0x24020005u, 0x24091234u, 0xAC092000u, 0x8C0A2000u};
    *(uint32_t*)(psx_ram+0x2000)=0xDEADBEEF;
    for(int i=0;i<4;i++) *(uint32_t*)(psx_ram+0x100000+i*4)=prog[i];
    psx_state.pc=0x100000;
    uint8_t *code; uint32_t len;
    if(emit_block_dbg(0x100000,&code,&len)){
        printf("block len=%u:",len);
        for(uint32_t i=0;i<len && i<48;i++) printf(" %02x",code[i]);
        printf("\n"); fflush(stdout);
        ((void(*)())code)();
        printf("direct: r2=%u r9=%#x r10=%#x mem2000=%#x pc=%#x\n",
               psx_state.r[2],psx_state.r[9],psx_state.r[10],*(uint32_t*)(psx_ram+0x2000),psx_state.pc);
    } else printf("emit fail\n");
    return 0;
}
#endif
#ifdef JIT_MAIN
#include <fcntl.h>
#include <unistd.h>
static int fails=0;
static void ck(const char*what,uint32_t got,uint32_t want){
    if(got!=want){ printf("FAIL %s got=%08x want=%08x\n",what,got,want); fails++; }
}
static int run_prog(const uint32_t*p,int n,uint32_t entry){
    for(int i=0;i<n;i++) jh_sw(0x100000+i*4,p[i]);
    if(getenv("JIT_EMIT")){ fprintf(stderr,"[WR]"); for(int i=0;i<n;i++) fprintf(stderr," %08x",jh_lw(0x100000+i*4)); fprintf(stderr,"\n"); }          /* 程序放 RAM 0x100000 */
    psx_state.pc=entry; psx_state.halted=0; cache_inval();
    return jit_run(10000);
}
static void unit_tests(void){
    if(jit_init()){ printf("FAIL jit_init\n"); fails++; return; }
    /* —— 组 1：数据区 0x2000 —— */
    jh_sw(0x2000,0xAABBCCDD); for(uint32_t i=0;i<5;i++) jh_sb(0x2004+i,i+1);
    const uint32_t P1[]={
      0x24020005,              /* addiu $v0,$zero,5 */
      0x24091234,              /* addiu $t1,0x1234 */
      0xAC092000,              /* sw $t1,0x2000($zero) */
      0x8C0A2000,              /* lw $t2,0x2000($zero) */
      0x012A3021,              /* addu $a2,$t1,$t2 = 0x2468 — 勘误: 原机器码 rt 字段误为 v0(0x01223021), 恰与旧自加 bug 互相掩盖 */
      0x900B2004,              /* lbu $t3,0x2004 = 1 */
      0x900C2007,              /* lbu $t4,0x2007 = 4 */
      0x300D000F,              /* andi $t5,5? base zero=0 → 0 */
      0x000A3880,              /* sll $a3,$t2,2 = 0x48D0 */
      0x000A40C3,              /* sra $t0,$t2,3 = 0x246 */
      0x00000000,              /* nop */
      0x14400001,              /* bne $v0,$zero,+1 */
      0x24190099,              /* addiu $t9,0x99（跳过）*/
      0x00000000,
      0x0000000D, 0x00000000,  /* break → halt */
    };
    run_prog(P1,16,0x100000);
    ck("addiu v0",psx_state.r[2],5);
    ck("sw/lw t2",psx_state.r[10],0x1234);
    ck("addu a2",psx_state.r[6],0x2468);
    ck("lbu t3",psx_state.r[11],1);
    ck("lbu t4",psx_state.r[12],4);
    ck("andi t5",psx_state.r[13],0);
    ck("sll a3",psx_state.r[7],0x48D0);
    ck("sra t0",psx_state.r[8],0x246);
    ck("bne slot t9",psx_state.r[25],0x99);
    ck("halt@",psx_state.pc,0x10003c); ck("halted",psx_state.halted,1);
    ck("sw mem",jh_lw(0x2000),0x1234);
    /* —— 组 2：sh/sb/lhu，数据区 0x3000 —— */
    jh_sw(0x3004,0x11223344);
    const uint32_t P2[]={
      0x3402AAAA,              /* ori $v0,0xAAAA */
      0xA4023010,              /* sh $v0,0x3010 */
      0x94033010,              /* lhu $v1,0x3010 = 0xAAAA */
      0xA0023012,              /* sb $v0,0x3012 (=0xAA) */
      0x94043012,              /* lhu $a0,0x3012 = 0x00AA */
      0x8C053004,              /* lw $a1,0x3004 = 0x11223344 */
      0x0C200000, 0x00000000,
    };
    run_prog(P2,8,0x100000);
    ck("sh/lhu v1",psx_state.r[3],0xAAAA);
    ck("sb/lhu2 a0",psx_state.r[4],0x00AA);
    ck("lw2 a1",psx_state.r[5],0x11223344);
    /* —— 组 3：lb 符号，数据区 0x4000 —— */
    jh_sb(0x4000,0xFF);
    const uint32_t P3[]={ 0x80024000, 0x0C200000, 0x00000000 };  /* lb $v0,0x4000 符号扩展 */
    run_prog(P3,3,0x100000);
    ck("lb sign",psx_state.r[2],0xFFFFFFFF);
    /* —— 组 4：lwl/swl，数据区 0x5000 —— */
    jh_sw(0x5000,0xAABBCCDD); jh_sw(0x5004,0x11223344);
    const uint32_t P4[]={
      0x88105001,              /* lwl $s0,0x5001 → 0xAABBCC00 */
      0x98115001,              /* lwr $s1,0x5001 → 0x0000CCDD */
      0xA8115001,              /* swl $s1,0x5001 → [0x5000]=0x0000CCDD */
      0x8C045000,              /* lw $a0,0x5000 = 0x0000CCDD */
      0x0C200000, 0x00000000,
    };
    run_prog(P4,6,0x100000);
    ck("lwl",psx_state.r[16],0xAABBCC00);
    ck("lwr",psx_state.r[17],0x0000CCDD);
    ck("swl mem",jh_lw(0x5000),0x0000CCDD);
    ck("swl lw",psx_state.r[4],0x0000CCDD);
    printf(fails? "JIT-CHECK: %d FAILS\n":"JIT-CHECK: PASS\n",fails);
}
static int bios_smoke(const char*path){
    FILE*f=fopen(path,"rb"); if(!f){printf("open fail\n");return 1;}
    static uint8_t rom[0x80000]; if(fread(rom,1,sizeof rom,f)!=sizeof rom) return 1; fclose(f);
    psx_rom=rom; if(jit_init()) return 1;
    psx_state.pc=0xBFC00000;
    int rc=0; uint32_t blocks=0;
    static uint32_t hist[4096];
    for(;blocks<500000;blocks++){ hist[blocks&4095]=psx_state.pc; rc=jit_run(1); if(rc) break; }
    if(getenv("JIT_TRACE_BIOS")){ for(uint32_t i=(blocks>60?blocks-60:0); i<blocks; i++) fprintf(stderr,"[HB] %08x\n",hist[i&4095]); }
    printf("JIT-BIOS steps(blocks)=%u rc=%d pc=%08x halted=%u sp=%08x ra=%08x v0=%08x\n",blocks,rc,psx_state.pc,psx_state.halted,psx_state.r[29],psx_state.r[31],psx_state.r[2]);
    /* task-041 重锚(新 JIT 语义): 旧基线"ROM 内 syscall 浅停机"系 addu/ra-flag bug 伪像;
       新基线 = 链过 kernel 自装+清 RAM 循环, 抵达 rootcall NewRootCmd(cmd=0x10, RAM 处理槽未装=冷链边界) */
    int ok=(rc==2 && blocks>=15000u && psx_state.r[2]==0x80000010u);
    printf("JIT-CHECK: %s\n",ok?"PASS":"FAIL");
    return ok?0:1;
}
int main(int argc,char**argv){
    setvbuf(stdout,NULL,_IONBF,0);
    g_dbg_emit=!!getenv("JIT_EMIT");
    if(argc>1 && argv[1][0]!='-') return bios_smoke(argv[1]);
    unit_tests(); return fails?1:0;
}
#endif
void* jit_dbg_lastblock(void){ uint32_t h=(psx_state.pc>>2)&(CSLOTS-1); return cc[h].code; }
