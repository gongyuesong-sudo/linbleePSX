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
#include <unistd.h>

int g_dbg_emit=0;
static FILE *jit_rc_log;
typedef struct { uint32_t r[32]; uint32_t pc; uint32_t halted; uint32_t halt_kind; } PsxState;  /* halt_kind: 1=syscall/break 2=emit-fail */
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
static void devlog(const char*rw,uint32_t a,uint32_t v){ FILE*f=devlog_fp(); if(!f) return; if((a&0x1FFF0000u)==0x1F800000u) fprintf(f,"%s %08x %08x\n",rw,a,v); }
/* 最小设备层：0x1F800000-0x1F80FFFF 16-bit 默认开寄存器组（uninit=0xFFFFFFFF；
 * GPU/CDROM status 窗默认 0=ready 已实证无害；写全收，读按寄存器值）*/
static uint32_t dev_regs[0x10000>>2];
static uint32_t exvbd[0x1000>>2];   /* 0xFFFE0000-0xFFFEFFFF: ExceptionVectorBaseData 硬件寄存器组(RAM 0x130 类写入实为硬件窗) */
static uint32_t dev_read(uint32_t a){ uint32_t o=a&0xFFFF;
    if(o>=0x1000&&o<=0x10FF) return dev_regs[o>>2];         /* GPU/IRQ 域默认 0 = ready */
    if(o==0x1060) return dev_regs[0x1060>>2];
    return dev_regs[o>>2]?dev_regs[o>>2]:0xFFFFFFFFu; }
static uint32_t cop0[32];
#define COP0_EPC 14
uint32_t __attribute__((stdcall)) jh_rfe(void){ cop0[12]&=~(1u<<30); return cop0[COP0_EPC]; }
uint32_t __attribute__((stdcall)) jh_cop0_read(uint32_t r0){ return cop0[r0&31]; }
void __attribute__((stdcall)) jh_cop0_write(uint32_t r0,uint32_t v){ r0&=31; cop0[r0]=v;
    /* SR.BEV 1->0 迁移：异常向量从 ROM 镜像到 RAM（真实 CPU 由 BIOS 复制；此处跟随状态位忠实化） */
    if(r0==12 && psx_ram && psx_rom && !(v&0x04000000u) && !cop0[31]){ cop0[31]=1; memcpy(psx_ram,psx_rom,0x100); } }
uint32_t __attribute__((stdcall)) jh_lw(uint32_t a){
    uint32_t a0=a;
    a=phys(a);
    if((a0&0xFFFF0000u)==0xFFFE0000u){ uint32_t r=exvbd[(a0&0xFFF)>>2]; devlog("RD",a0,r); return r; }
    if((a0&0x1FFF0000u)==0x1F800000u){ uint32_t r=dev_read(a0); devlog("RD",a0,r); return r; }
    if(a>=0x1FC00000u && a+4<=0x20000000u && psx_rom) return *(const uint32_t*)(psx_rom+(a-0x1FC00000u));
    if(a+4<=0x200000u && psx_ram) return *(const uint32_t*)(psx_ram+a);
    return 0;
}
static void watch_store(uint32_t a,uint32_t v){ static int done; static uint32_t lo,hi; FILE*f;
    if(!done){ done=1; { const char*w=getenv("H_WATCH"); if(w){ sscanf(w,"%x:%x",&lo,&hi); } } }
    if(hi&&a>=lo&&a<hi&&(f=devlog_fp())){ fprintf(f,"STR pc=%08x a=%08x v=%08x\n",jit_cur_pc,a,v); fflush(f); } }
void __attribute__((stdcall)) jh_sw(uint32_t a, uint32_t v){ watch_store(a,v); watch_store(phys(a),v);
    if((a&0xFFFF0000u)==0xFFFE0000u){ exvbd[(a&0xFFF)>>2]=v; devlog("HW",a,v); return; }
    if((a&0x1FFF0000u)==0x1F800000u){ uint32_t o=a&0xFFFF; dev_regs[o>>2]=v; devlog("WR",a,v); return; } a=phys(a); if(a+4<=0x200000u&&psx_ram) *(uint32_t*)(psx_ram+a)=v; }
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
    b1(0xA0); b4(ST+124u); b1(0x84); b1(0xC0); b1(0x74); /* mov al,[ST+124]; test al,al; jz <槽> */
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
    int pend=0, pend_mode=0; uint32_t pend_tk=0, pend_nt=0;
    if(pool_used+4096>POOL_CAP) return NULL;
    uint8_t *buf=code_pool+pool_used; pool_used+=4096;
    E=buf; EEND=buf+4096; EMAX=EEND;
    if(g_dbg_emit) fprintf(stderr,"[BUF] vpc=%08x buf=%p pool=%p used=%03x\n",vpc0,(void*)buf,(void*)code_pool,pool_used);
    for(int n=0;n<32;n++){
        uint32_t w; if(!ram_fetch(vpc,&w)){ jit_fail_pc=vpc; jit_fail_op=0xFFFFFFFFu; return NULL; }
        jit_fail_pc=vpc; jit_fail_op=w;
        uint32_t op=(w>>26)&63, rs=(w>>21)&31, rt=(w>>16)&31, rd=(w>>11)&31, sa=(w>>6)&31, f3=w&63;
        int32_t si=(int32_t)(int16_t)(w&0xFFFF); uint32_t uimm=w&0xFFFF;
        if(g_dbg_emit) fprintf(stderr,"[E] vpc=%08x w=%08x pend=%d\n",vpc,w,pend);
        int is_nop=(w==0 || (op==0 && rs==0 && f3==0 && rd==0));
        if(pend && is_nop){ vpc+=4;
            if(g_dbg_emit) fprintf(stderr,"[TERMn] mode=%d tk=%08x nt=%08x\n",pend_mode,pend_tk,pend_nt);
            if(pend_mode==4){ rdd((int)pend_tk); store_pc_eax(); ret_(); }        /* jr 延迟槽=NOP */
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
                if(f3==9){ mov_imm_eax(vpc+8); wrr(rd?rd:31); }
                if(pend) return NULL;                 /* 槽内再分支：弃 */
                pend=1; pend_mode=4; pend_tk=rs; pend_nt=0; vpc+=4; continue; }
            case 12: case 13:   /* syscall(kind=1) / break(kind=3)：延迟槽后停机 */
                if(pend){ if(!emit_term(pend_tk,pend_nt,2)) return NULL; return buf; }
                store_pc_imm(vpc+4); b1(0xC6); b1(0x05); b4(ST+132u); b1(1); b1(0xC6); b1(0x05); b4(HK); b1(f3==12?1:3); ret_(); return ovf?NULL:buf;
            case 16: case 17: {   /* COP0: mfc0/mtc0（rd<32），其余 NOP */
                uint32_t f3c=(w>>6)&31u, rt=(w>>16)&31u, rd=(w>>11)&31u;
                if(f3c==0 && rd<32){ rdd((int)rt); push_eax(); mov_imm_eax(rd); push_eax(); call_ptr(H32(jh_cop0_write)); }
                else if(f3c==1 && rd<32){ mov_imm_eax(rd); push_eax(); call_ptr(H32(jh_cop0_read)); if(rt)wrr((int)rt); }
                vpc+=4; if(pend)goto slot_done; continue; }
            case 18: case 19: vpc+=4; if(pend)goto slot_done; continue;  /* COP1/COP2 NOP */
            case 32: case 33: if(rd){ rdd(rs); add_eax_mem(); wrr(rd);} vpc+=4; if(pend)goto slot_done; continue;
            case 34: case 35: if(rd){ rdd(rs); sub_eax_mem(); wrr(rd);} vpc+=4; if(pend)goto slot_done; continue;
            case 36: if(rd){ rdd(rs); and_eax_mem(); wrr(rd);} vpc+=4; if(pend)goto slot_done; continue;
            case 37: if(rd){ rdd(rs); or_eax_mem(); wrr(rd);} vpc+=4; if(pend)goto slot_done; continue;
            case 38: if(rd){ rdd(rs); xor_eax_mem(); wrr(rd);} vpc+=4; if(pend)goto slot_done; continue;
            case 39: if(rd){ rdd(rs); or_eax_mem(); not_eax(); wrr(rd);} vpc+=4; if(pend)goto slot_done; continue;
            case 40: if(rd){ rdd(rs); neg_eax(); wrr(rd);} vpc+=4; if(pend)goto slot_done; continue;
            case 42: if(rd){ rdd(rs); cmp_eax_mem(); setcc_cl(0x0C); movzx_ecx_cl(); mov_eax_ecx(); wrr(rd);} vpc+=4; if(pend)goto slot_done; continue;
            case 43: if(rd){ rdd(rs); cmp_eax_mem(); setcc_cl(0x02); movzx_ecx_cl(); mov_eax_ecx(); wrr(rd);} vpc+=4; if(pend)goto slot_done; continue;
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
        case 35: if(rt){ rdd(rs); add_eax_imm(si); push_eax(); call_ptr(H32(jh_lw)); wrr(rt);} vpc+=4; if(pend)goto slot_done; continue;   /* lw */
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
            if(op==3){ mov_imm_eax(vpc+8); wrr(31); }
            uint32_t dummy;
            pend=1; pend_mode= ram_fetch(tgt,&dummy)?0:2; pend_tk=tgt; pend_nt=tgt; vpc+=4; continue; }
        case 4: case 5: case 6: case 7: {   /* beq/bne/blez/bgtz：条件置 ECX，终止延到延迟槽后 */
            if(pend) return NULL;
            uint32_t tk=(vpc+4)+(uint32_t)((si<<2)), nt=vpc+8;   /* 不跳=跳过延迟槽 */
            if(g_dbg_emit) fprintf(stderr,"[RAW] vpc=%08x si=%d shl=%08x tk=%08x nt=%08x\n",vpc,si,(si<<2),tk,nt);
            if(op==4||op==5){ rdd(rs); set_edx_r(rt); cmp_eax_mem(); setcc_al(op==4?0x04:0x05); b1(0xA2); b4(ST+124u); }
            else { rdd(rs); cmp_eax_imm(0); setcc_al(op==6?0x06:0x0F); b1(0xA2); b4(ST+124u); }
            if(g_dbg_emit) fprintf(stderr,"[BR] vpc=%08x vpc4=%08x si=%d si2=%08x tk=%08x nt=%08x w=%08x\n",vpc,vpc+4,si,(si<<2),tk,nt,w);
            pend=1; pend_mode=1; pend_tk=tk; pend_nt=nt; vpc+=4; continue; }
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
        if(pend_mode==4){ rdd((int)pend_tk); store_pc_eax(); ret_(); return ovf?NULL:buf; }
        if(!emit_term(pend_tk,pend_nt,pend_mode)) return NULL;
        return buf;
    }
    if(pend){ if(pend_mode==4){ rdd((int)pend_tk); store_pc_eax(); ret_(); } else if(!emit_term(pend_tk,pend_nt,pend_mode)) return NULL; return buf; }
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
            if(!c){ psx_state.halted=1; psx_state.halt_kind=2; { FILE*f=devlog_fp(); if(f){fprintf(f,"EMITFAIL pc=%08x op=%08x\n",jit_fail_pc,jit_fail_op); fflush(f);} } return 2; }
            cc[h].vpc=p; cc[h].code=c;
        } else if(g_dbg_emit) fprintf(stderr,"[HIT] p=%08x code=%08x h=%d\n",p,(unsigned)(uintptr_t)cc[h].code,h);
        { static int step=0; if(getenv("JIT_DEBUG")&&step<40){ uint32_t *u=(uint32_t*)cc[h].code;
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
int  jit_lib_load_rom(const uint8_t *rom_shadow){ psx_rom=rom_shadow; return jit_init(); }
static int jit_rootcall_serve(void);
int  jit_lib_trace_boot(FILE*log,uint32_t entry,uint32_t max_blocks,uint8_t*win,uint32_t win_base,int backfill){
    psx_state.pc=entry; psx_state.halted=0; psx_state.halt_kind=0; cache_inval();
    static uint32_t seq[8192]; uint32_t nseq=0,done=0; uint32_t lastrep=entry, rep=0;
    jit_rc_log=log;
    for(;done<max_blocks;done++){
        if(psx_state.halted) break;
        uint32_t p=psx_state.pc, h=(p>>2)&(CSLOTS-1);
        if(p==lastrep){ if(++rep>4000){ if(log){fprintf(log,"LOOP-DETECT pc=%08x\n",p); fflush(log);} break; } }
        else { lastrep=p; rep=0; }
        if(cc[h].vpc!=p || !cc[h].code){ void*c=emit_block(p); if(!c){ psx_state.halted=1; psx_state.halt_kind=2; { FILE*f=devlog_fp(); if(f){fprintf(f,"EMITFAIL pc=%08x op=%08x\n",jit_fail_pc,jit_fail_op); fflush(f);} } break; } cc[h].vpc=p; cc[h].code=c; }
        if(nseq<8192) seq[nseq++]=p;
        ((void(*)())cc[h].code)();
        if(psx_state.halted && (psx_state.halt_kind==1||psx_state.halt_kind==3)){
            if(jit_rootcall_serve()) break; }
    }
    if(log){
        /* 压缩 PC 序列：连续 vpc 段合并为 lo..hi */
        fprintf(log,"BIOS-JIT-CHAIN blocks=%u final=%08x halted=%d\n",nseq,psx_state.pc,psx_state.halted);
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
static int jit_rootcall_serve(void){
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
        h&=0x1FFFFFFFu;
        if(h<0x80000u) h+=0xBFC00000u;   /* 金标准: 表内为 BIOS ROM 相对偏移(trap/entry, 如 0x1E988=异常分发器 ROM 直执) */
        if(h && (h<0x200000u || (h&0x1FC00000u)==0x1FC00000u)){
            cop0[COP0_EPC]=psx_state.pc+4;                    /* EPC = syscall+8 */
            cop0[13]=(psx_state.halt_kind==3)?0xC0u:0x80u;    /* Cause: break 0x30 / syscall 0x20 (bits7-2) */
            cop0[12]|=1u<<30;                                 /* SR.CEL=1 */
            psx_state.pc=h; psx_state.halted=0; psx_state.halt_kind=0;
            if(jit_rc_log){ fprintf(jit_rc_log,"EXN cmd=%u epc=%08x -> h=%08x\n",cmd,cop0[COP0_EPC],h); fflush(jit_rc_log); }
            return 0;
        }
    }
    if(jit_rc_log){ fprintf(jit_rc_log,"ROOTCALL cmd=%u pc=%08x ra=%08x pass\n",cmd,psx_state.pc,psx_state.r[31]); fflush(jit_rc_log); }
    psx_state.halted=0; psx_state.halt_kind=0;
    return 0;
}
int jit_lib_bios_call_log(uint32_t entry_pc,uint32_t budget,uint32_t *ret_v0,FILE*log);
int jit_lib_serve(void){ return jit_rootcall_serve(); }   /* probe 用: 单步 syscall/rootcall 放行 */
int jit_lib_bios_call2(uint32_t entry_pc,uint32_t budget,uint32_t*v0,uint32_t*kind,uint32_t*endpc){
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
    psx_state.pc=entry_pc; psx_state.halted=0; psx_state.halt_kind=0; jit_rc_log=log;
    int n=0; uint32_t lastrep=entry_pc, rep=0; static uint32_t hb[8192];
    for(;n<(int)budget;n++){
        uint32_t p=psx_state.pc,h=(p>>2)&(CSLOTS-1); hb[n&8191]=p; jit_cur_pc=p;
        if(p==lastrep){ if(++rep>4000){ if(log){fprintf(log,"LOOP-DETECT pc=%08x\n",p); fflush(log);} break; } }
        else { lastrep=p; rep=0; }
        if(cc[h].vpc!=p || !cc[h].code){ void*c=emit_block(p); if(!c){ psx_state.halted=1; psx_state.halt_kind=2; { FILE*f=devlog_fp(); if(f){fprintf(f,"EMITFAIL pc=%08x op=%08x\n",jit_fail_pc,jit_fail_op); fflush(f);} } break; } cc[h].vpc=p; cc[h].code=c; }
        ((void(*)())cc[h].code)();
        if(psx_state.halted){ if(jit_rootcall_serve()) break; }
    }
    if(getenv("H_JITBLOCKS")&&log&&psx_ram){ for(uint32_t o=0x00;o<0x100;o+=4) fprintf(log,"RAMB %04x=%08x\n",o,*(uint32_t*)(psx_ram+o)); fflush(log); }
    if(getenv("H_JITBLOCKS")&&log){ uint32_t st=0; for(uint32_t i=st;i<(uint32_t)n;i++) fprintf(log,"BLK %08x\n",hb[i&8191]); fprintf(log,"BLK end=%08x\n",psx_state.pc); fflush(log); }
    if(ret_v0)*ret_v0=psx_state.r[2];
    return n;
}


__asm__(
".text\n"
".globl trampoline_call\n"
".type trampoline_call,@function\n"
"trampoline_call:\n"
"\tpushl %ebp\n\tmovl %esp,%ebp\n\tmovl 8(%ebp),%eax\n\tcall *%eax\n\tpopl %ebp\n\tret\n"
".size trampoline_call,.-trampoline_call\n");
