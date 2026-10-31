/* golden_cpu_probe.c — task-033/034: 金标准 CPU+RAM 终态自洽 + serve 续跑 + printf 注入达设备面
 * GSTATE=<hdr> [blocks]                  : 直跳自洽(停留金标准 PC 页)
 * GSTATE SERVE=1 [blocks]                : 异常走 RAM 向量 0x80 真 handler 的续跑活性(配 H_EXNVEC)
 * GSTATE INJ=1 [blocks]                  : 注入 A(42) printf("CDTEST") → 设备派发面证据(lowrd)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "mips_jit_inc.h"

uint32_t jit_lib_lowrd(void);
void jit_lib_flush_blocks(void);
int jit_lib_serve(void);

int main(int argc, char** argv){
    setvbuf(stdout,NULL,_IONBF,0);
    const char* hs = getenv("GSTATE");
    if(!hs){ printf("usage: GSTATE=<out.hdr> %s [blocks]\n", argv[0]); return 2; }
    void*p=mmap((void*)0x2a1000u,64u<<20,PROT_READ|PROT_WRITE,MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS,-1,0); (void)p;
    static uint8_t rom[0x80000];
    const char*rpath=argc>1?argv[1]:"../psxbios/SCPH1001.BIN";
    FILE*f=fopen(rpath,"rb"); if(!f||fread(rom,1,sizeof rom,f)!=(long)sizeof rom){printf("rom fail\n");return 2;} fclose(f);
    extern int jit_lib_load_rom(const unsigned char*);
    if(jit_init()) return 2;
    if(jit_lib_load_rom(rom)) return 2;

    char ramp[512]; strncpy(ramp,hs,sizeof ramp); ramp[sizeof ramp-1]=0;
    char*dot=strrchr(ramp,'.'); if(dot) *dot=0; strncat(ramp,".ram",sizeof ramp-1-strlen(ramp));
    FILE*rf=fopen(ramp,"rb"); if(!rf){printf("ram fail %s\n",ramp);return 2;}
    if(fread(psx_ram,1,0x200000,rf)!=0x200000){printf("ram short\n");return 2;} fclose(rf);
    FILE*hf=fopen(hs,"rb"); if(!hf){printf("hdr fail\n");return 2;}
    unsigned char hdr[4+4+4+128]; if(fread(hdr,1,sizeof hdr,hf)!=sizeof hdr){printf("hdr short\n");return 2;} fclose(hf);
    if(memcmp(hdr,"GS1",3)){printf("hdr magic\n");return 2;}
    uint32_t pc=*(uint32_t*)(hdr+8);
    memcpy(psx_state.r,hdr+12,128);   /* magic "GS1\0"(4B)+npc(4)+pc(4) → GPR 自 byte12 */
    psx_state.pc=pc; psx_state.halted=0; psx_state.halt_kind=0;

    if(getenv("TAIL")) g_dbg_emit=1;
    extern void* jit_dbg_lastblock(void); extern uint32_t jit_lib_gpuw_probe(void);
    int hb = !!getenv("HB");
    if(getenv("CD")){                    /* H-18a: CDHLE C2 体独立直调(命令环+首扇区读回, docs/29) */
        extern void hlecd_install_pub(void); hlecd_install_pub();
        psx_state.pc=0x80040000u; psx_state.r[31]=0xBFC4C1B0u; psx_state.r[7]=0x80041000u;
        psx_state.halted=0; psx_state.halt_kind=0;
    }
    if(hb){                        /* H-17b: 第一方 homebrew EXE 装载(等价 BIOS Execute! 终态) */
        const char* xp = getenv("HBEXE") ? getenv("HBEXE") : "host/build/hbrew.exe";
        FILE*fe=fopen(xp,"rb"); if(!fe){printf("HBEXE open fail\n");return 2;}
        static unsigned char ex[8192]; size_t xn=fread(ex,1,sizeof ex,fe); fclose(fe);
        if(xn<2049){printf("HBEXE short %zu\n",xn);return 2;}
        uint32_t pc0=*(uint32_t*)(ex+0x10), ta=*(uint32_t*)(ex+0x18), tsz=*(uint32_t*)(ex+0x1C),
                 sa=*(uint32_t*)(ex+0x20), ssz=*(uint32_t*)(ex+0x24);
        if(getenv("HBBASE")){ ta=0; ssz=0; ta=(uint32_t)strtoul(getenv("HBBASE"),0,16); tsz=(uint32_t)xn-2048; }
        memcpy(psx_ram+(sa&0x1FFFFFu), ex+2048, ssz);
        memcpy(psx_ram+(ta&0x1FFFFFu), ex+2048+ssz, tsz);
        psx_state.pc=pc0; psx_state.r[29]=0x801FFF00u; psx_state.r[31]=0xBFC4C1B0u;
        psx_state.halted=0; psx_state.halt_kind=0;
        jit_lib_flush_blocks();   /* 关键: 清残留 BIOS 期块缓存(htrace/serve 装载过 0x4C000 页) */
        printf("HB load pc0=%08x t=%08x/%u s=%08x/%u\n",pc0,ta,tsz,sa,ssz);
    }
    int poke = !!getenv("POKE");
    if(poke){                     /* 时基注入: 设备状态字节 RAM 0x83C58=1 → 轮询环进入主循环体 */
        *(uint8_t*)(psx_ram+0x83C58u)=1u;
        printf("POKE 83C58=1\n");
    }
    int inj = !!getenv("INJ");
    if(inj){
        uint32_t a42=0x00000000u;
        { const char*ee=getenv("INJENTRY"); a42=ee?(uint32_t)strtoul(ee,0,16):0xBFC0D540u; }  /* 默认=ROM 唯一设备槽读锚 */
        memcpy(psx_ram+0x9100,"CDTEST\n",8);
        psx_state.r[29]-=0x40; *(uint32_t*)(psx_ram+(psx_state.r[29]&0x1FFFFFu))=0xBFC4C1B0u; /* 返回哨兵=ROM break(bfc4c1b0, 干净停机) */
        psx_state.r[4]=0x80009100u;                       /* a0 = 格式串 */
        psx_state.pc=a42;
        printf("INJ printf entry=%08x a0=%08x sp=%08x\n",a42,psx_state.r[4],psx_state.r[29]);
    }
    uint32_t cycles=0;
    uint32_t budget=argc>2?(uint32_t)strtoul(argv[2],0,10):(inj?200000u:4000u);

    uint32_t pages=pc&~0xFFFu; uint32_t i,serves=0,fails=0,devwalk=0,vect=0; int outside=0;
    PsxState cp = psx_state;   /* 休眠快照: 必须在 fails=0 初始化之后(否则 cp.fails=垃圾, TICK 恢复即污染) */
    uint32_t hbphase=0, hbbreaks=0;
    for(i=0;i<budget;i++){
        /* 阶段推进仅在哨兵 break 停机时(见下), 不在 strcpy 中途抢 */
        if(jit_run(1)){
            if(hb){   /* break 停机: 程序内 break 或 ROM 哨兵 = 周期收尾 */
                if((psx_state.pc>=0xBFC00000u && psx_state.pc<0xBFC80000u) || (psx_state.halted && psx_state.halt_kind==3)){
                    hbbreaks++; hbphase=1;                  /* 程序自然 break / ROM 哨兵 = 正常收尾 */
                    psx_state.halted=0; psx_state.halt_kind=0; break;
                }
                fails=1; break;
            } if(getenv("HLOG")) fprintf(stderr,"HALT pc=%08x hk=%u\n",psx_state.pc,psx_state.halt_kind);
            if(getenv("TICK") && !inj && (psx_state.halt_kind==2 || (psx_state.halt_kind==3 && psx_state.pc>=0xBFC4C1B0u && psx_state.pc<0xBFC4C1C0u))){  /* task-041: 新基线 break(hk3)亦为唤醒点; hk2=旧 emit-fail 伪哨兵 */
                /* ROM 窗内停机=break 哨兵命中 → 一次完整唤醒周期收尾; RAM 零页停机=死模块滑行 → 单纯重导引 */
                if(psx_state.pc>=0xBFC00000u && psx_state.pc<0xBFC80000u) cycles++;
                psx_state=cp; psx_state.r[31]=0xBFC4C1B0u; psx_state.halted=0; psx_state.halt_kind=0; jit_lib_flush_blocks(); continue;
            }
            if(!(psx_state.halted && psx_state.halt_kind!=2)){ fails=1; break; }
            if(getenv("H_EXNVEC")||inj){ if(jit_lib_serve()){ fails=1; break; } serves++; }
            else break;
        }
        uint32_t c=psx_state.pc;
        if(getenv("DBLK")){ extern void* jit_dbg_lastblock(void); extern uint32_t jit_lib_gpuw_probe(void); printf("LASTBLK@%08x ptr=%p\n",c,jit_dbg_lastblock()); fflush(stdout); }
        if(getenv("TAIL")){
            printf("P %08x",c);
            if(getenv("TREG")) for(int q=2;q<8;q++) printf(" r%d=%08x",q,psx_state.r[q]); for(int q=16;q<32;q++) printf(" r%d=%08x",q,psx_state.r[q]);
            printf("\n");
        }
        if((c&0xF0000000u)==0x80000000u||c<0x200000u){ uint32_t k=c&0x1FFFFFu;
            if(k>=0xDE8u&&k<0xE10u) devwalk=1;
            if(k>=0x80u&&k<0xC0u) vect=1;
        }
        if(!inj && (c&~0xFFFu)!=pages) outside++;
        if(getenv("TICK") && !inj){     /* 宿主时基: 环回空闲页即再唤醒, 计周期 */
            static uint64_t last=0; static int idle=0;
            if((c&0xFFFFF000u)==0x8003D000u){ if(++idle>=100){ *(uint8_t*)(psx_ram+0x83C58u)=1u;
                psx_state.r[31]=0xBFC4C1B0u;                   /* ra(叶函数活值)=休眠调用者(已死模块)→改写 break 哨兵, 唤醒周期干净返回宿主 */
                idle=0; jit_lib_flush_blocks(); } }
            else idle=0; (void)last;
        }
    }
    if(hb){
        uint32_t cnt=*(uint32_t*)(psx_ram+0x1FE000u), dst=*(uint32_t*)(psx_ram+0x1FE010u);
        printf("GOLDEN-HB blocks=%u pc=%08x s0=%08x s1=%08x t8=%08x cnt=%u gpuw=%u dst=%08x breaks=%u phase=%u fails=%u\n",
               i, psx_state.pc, psx_state.r[16], psx_state.r[17], psx_state.r[24], cnt, jit_lib_gpuw_probe(), dst, hbbreaks, hbphase, fails);
        return (cnt>=2000 && jit_lib_gpuw_probe()>0 && !fails && hbphase==1)?0:1;   /* dst 串由 host30 单独断言(FULL) */
    }
    printf("GOLDEN-%s cycles=%u blocks=%u pc=%08x outside=%d halted=%u hk=%u sp=%08x serves=%u devwalk=%u vect=%u lowrd=%u fails=%u\n",
           inj?"INJ":"RUN", cycles, i, psx_state.pc, outside, psx_state.halted, psx_state.halt_kind,
           psx_state.r[29], serves, devwalk, vect, jit_lib_lowrd(), fails);
    
    if(getenv("CD")){ extern unsigned char* jit_lib_ram_pub(void); unsigned char*R=jit_lib_ram_pub();
        printf("CDRAM cmd=%08x ack=%08x ack2=%08x dst0=%08x v0=%08x pc=%08x fails=%u\n",
               *(unsigned*)(R+0xA0100u),*(unsigned*)(R+0xA0140u),*(unsigned*)(R+0xA0200u),*(unsigned*)(R+0x50000u),psx_state.r[2],psx_state.pc,fails); fflush(stdout); }
    if(getenv("TICK")) return (cycles>=3 && !fails)?0:1;
    if(inj) return (jit_lib_lowrd()>0)?0:1;   /* 设备槽读=验收锚; fails 允许(链滑入未装区属预期) */
    return (!fails && outside==0)?0:1;
}
