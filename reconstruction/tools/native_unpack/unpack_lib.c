// unpack_lib.c — self-contained C decoder for the bleem! RCR carry-chain LZ format.
// Hardware-faithful transcription of the plaintext stub @file-offset 0x55338 region
// (decoder core 0x5577A..0x5599E). Golden reference = native execution output.
//
// Bit engine:
//   descending dword stream (esi decrements 4 per load)
//   EBX = bit buffer, CF persistent. Consume:
//     EBX!=0: CF = EBX&1; EBX >>= 1
//     EBX==0: EBX = [esi]; esi-=4; n=EBX&1; EBX=(CF<<31)|(EBX>>1); CF=n   (phantom bit)
//   get8 (@0x55960): if EBX>=0x100: v=EBX&0xFF; CF=(EBX>>7)&1; EBX>>=8   (bit0 = value LSB)
//     else 8 chained consumes, value bits appended as: acc = acc*2|CF     (bit-first = value MSB)
//       with the 0x5599f escape: if buffer zeroes on the FIRST shift: load dword, return new&0xFF,
//       rcr+shr7 as per hardware.
// Token tree (@0x5577A main loop, EDI walks DOWN from dst+L to dst):
//   c0 = bit
//   c0==0: c1=bit
//     c1==0: n=bit (0/1); write (n+1) get8 bytes                    [short literal run]
//     c1==1: EDI-=2; d=get8; copy 2 bytes from [EDI+d+1..+2]        [word match]
//   c0==1: c1=bit
//     c1==0: c2=bit
//       c2==1: EDI-=4; d = EBX>=0x400 ? take10() : (get8()chain1); copy 4 from [EDI+d+1..+4]
//       c2==0: EDI-=3; d = EBX>=0x200 ? take9()  : (get8()chain1); copy 3 from [EDI+d+1..+3]
//     c1==1: c2=bit
//       c2==0: len=get8()+5; d = EBX>=0x1000 ? take12() : (get8()chain1); copy len from [EDI+d] downward
//       c2==1: len=9+get8(); literal bytes
//   loop ends when EDI < EBP(=dst)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct { const uint8_t *img; uint32_t io; uint32_t dst; uint8_t *out; size_t cap;
                 uint32_t esi, ebx, cf, ebp, edi; } lz_t;

// token trace (env BLEEM_TRACE=1): [edi, ebx, esi, cf] at each loop-head (=hw 0x4557a0)
int bleem_trace_on = 0;
unsigned bleem_trace_n = 0;
#define TRCAP 400000
static unsigned tr_edi[TRCAP], tr_ebx[TRCAP], tr_esi[TRCAP]; static unsigned char tr_cf[TRCAP];
static const char* tr_nm[TRCAP];
static void trace(unsigned edi,unsigned ebx,unsigned esi,unsigned cf){
    if(!bleem_trace_on || bleem_trace_n>=TRCAP) return;
    tr_edi[bleem_trace_n]=edi; tr_ebx[bleem_trace_n]=ebx; tr_esi[bleem_trace_n]=esi; tr_cf[bleem_trace_n]=cf;
    tr_nm[bleem_trace_n]=0;
    bleem_trace_n++;
}
static void leaf(const char*nm,lz_t*L){
    if(!bleem_trace_on || bleem_trace_n>=TRCAP) return;
    tr_edi[bleem_trace_n]=L->edi; tr_ebx[bleem_trace_n]=L->ebx; tr_esi[bleem_trace_n]=L->esi; tr_cf[bleem_trace_n]=L->cf;
    tr_nm[bleem_trace_n]=nm;
    bleem_trace_n++;
}
const char* bleem_trace_name(unsigned i){ return i<bleem_trace_n?tr_nm[i]:"END?"; }
int bleem_trace_get(unsigned i,unsigned*e0,unsigned*e1,unsigned*e2,unsigned*e3){
    if(i>=bleem_trace_n) return -1;
    *e0=tr_edi[i];*e1=tr_ebx[i];*e2=tr_esi[i];*e3=tr_cf[i];return 0;
}

static uint32_t rd32(lz_t*L,uint32_t a){
    uint32_t v=0,o=a-L->io;
    if(o < 0x56000u) memcpy(&v,L->img+o,4);
    else if(a>=L->dst && a-L->dst+4<=L->cap) memcpy(&v,L->out+(a-L->dst),4);
    return v;
}
static inline void shr1(lz_t*L){ L->cf=L->ebx&1; L->ebx>>=1; }
static inline void rcr1(lz_t*L){ uint32_t n=L->ebx&1; L->ebx=(L->cf<<31)|(L->ebx>>1); L->cf=n; }
// HARDWARE tree bit (shr@0x557a4/0x55787 + je-tails 0x5578b/0x557b2/0x5580e/0x55826/0x558c6):
// SHR first; if ebx BECOMES zero, load dword + RCR (lost pending bit replays from bit31
// later); the tree decision is the POST-RCR CF (= new dword bit0).
static inline uint32_t bitn(lz_t*L){
    if(L->ebx==0){ L->ebx=rd32(L,L->esi); L->esi-=4; rcr1(L); return L->cf; }
    shr1(L);
    if(L->ebx==0){ L->ebx=rd32(L,L->esi); L->esi-=4; rcr1(L); return L->cf; }
    return L->cf;
}
static inline uint8_t rbyte(lz_t*L,uint32_t a){ return (a>=L->dst&&a-L->dst<L->cap)?L->out[a-L->dst]:0; }
static inline void wbyte(lz_t*L,uint32_t a,uint8_t v){ if(a>=L->dst&&a-L->dst<L->cap) L->out[a-L->dst]=v; }

// slow-chain: hardware get8 body (0x5596E..0x559E9) unrolled semantics:
//  - entry with ebx==0: shr(0)→je 0x5599F: load dword, VALUE BYTE = dword&0xFF
//    (mov al,bl keeps acc high bits), then rcr+shr7.
//  - mid-chain shr zeroes ebx: load+rcr, THIS adc is SKIPPED (pending bit replays
//    from bit31 31 shifts later).
//  - last-position shr zeroes ebx (0x559E2 path): load+rcr then ADC the post-rcr CF.
static uint32_t chainN(lz_t*L,uint32_t acc,int n){
    for(int j=0;j<n;j++){
        if(L->ebx==0){
            L->ebx=rd32(L,L->esi); L->esi-=4;
            uint32_t v=L->ebx&0xFF; rcr1(L); L->ebx>>=7;      // 0x5599F
            return (acc & ~0xFFu) | v;
        }
        shr1(L);
        if(L->ebx==0){
            if(j==0 && n==8){                                   // 0x5599F belongs to the 8-bit get8 chain ONLY
                L->ebx=rd32(L,L->esi); L->esi-=4;
                uint32_t v=L->ebx&0xFF; rcr1(L); L->ebx>>=7;
                return (acc & ~0xFFu) | v;
            }
            L->ebx=rd32(L,L->esi); L->esi-=4; rcr1(L);
            if(j==n-1){ acc=((acc<<1)|L->cf); break; }          // 0x559E2 tail (1-bit chain)
            continue;                                            // mid je-tail: skip adc
        }
        acc=((acc<<1)|L->cf);
    }
    return acc;
}
static uint32_t get8(lz_t*L){
    if(L->ebx>=0x100){ uint32_t v=L->ebx&0xFF; L->cf=(L->ebx>>7)&1; L->ebx>>=8; return v; }
    // NOTE hardware: cmp ebx,0x100 / jb slow. Slow path starts with acc=0 (caller xor eax,eax)
    return chainN(L,0,8);
}
// fast takeN when ebx has >= n bits guaranteed by gate; else get8 + chain(needed-8)
static uint32_t takeGate(lz_t*L,int gate,int n){
    if(L->ebx>=gate){ uint32_t v=L->ebx&((1u<<n)-1); L->cf=(L->ebx>>(n-1))&1; L->ebx>>=n; return v; }
    uint32_t v=get8(L); return chainN(L,v&0xFF,n-8);
}

int bleem_lz_decompress(const uint8_t *img, uint32_t io, uint32_t sb, uint8_t *out, size_t cap, uint32_t *Lout){
    lz_t L={img,io,sb,out,cap,0,0,0,0,0};
    uint32_t L0=rd32(&L,sb), jmp=rd32(&L,sb+4);
    if(L0==0||L0>cap) return -1;
    L.ebp=sb; L.edi=sb+L0; L.esi=sb+jmp;
    L.ebx=rd32(&L,L.esi); L.esi-=4;
    int first=1;
    for(;;){
        // root consume: entry shr@0x55787 / back-edge shr@0x557a4 (identical semantics)
        uint32_t c0;
        c0=bitn(&L);
        if(c0==0){
            uint32_t c1=bitn(&L);
            if(c1==0){
                // short literal run: len = 3-bit + 1  (hw 0x557de: call 0x5598c(3b); ecx=+0? then add eax; loop ecx+1)
                leaf("SHORT",&L);
                uint32_t len=chainN(&L,0,3);   // hw: xor eax,eax; call 0x5598c (3-bit chain, no fast gate)
                len+=1;
                for(uint32_t i=0;i<len;i++){ wbyte(&L,L.edi,get8(&L)&0xFF); L.edi--; }
            } else {
                // 2-byte match, offset = get8 (hw 0x557c6: sub edi,2; d=get8; copy [edi+d+1],[edi+d+2])
                leaf("WORD2",&L);
                L.edi-=2; uint32_t d=get8(&L)&0xFF;
                wbyte(&L,L.edi+1,rbyte(&L,L.edi+d+1));
                wbyte(&L,L.edi+2,rbyte(&L,L.edi+d+2));
            }
        } else {
            uint32_t c1=bitn(&L);
            if(c1==0){
                uint32_t c2=bitn(&L);
                if(c2==0){
                    // 3-byte match (gate 0x200 / 9b; slow 8+1)  hw 0x5591f
                    leaf("M3",&L);
                    L.edi-=3; uint32_t d=takeGate(&L,0x200,9);
                    wbyte(&L,L.edi+1,rbyte(&L,L.edi+d+1));
                    wbyte(&L,L.edi+2,rbyte(&L,L.edi+d+2));
                    wbyte(&L,L.edi+3,rbyte(&L,L.edi+d+3));
                } else {
                    // 4-byte match (gate 0x400 / 10b; slow 8+2)  hw 0x558d7
                    leaf("M4",&L);
                    L.edi-=4; uint32_t d=takeGate(&L,0x400,10);
                    wbyte(&L,L.edi+1,rbyte(&L,L.edi+d+1));
                    wbyte(&L,L.edi+2,rbyte(&L,L.edi+d+2));
                    wbyte(&L,L.edi+3,rbyte(&L,L.edi+d+3));
                    wbyte(&L,L.edi+4,rbyte(&L,L.edi+d+4));
                }
            } else {
                uint32_t c2=bitn(&L);
                if(c2==1){
                    // long literal run 9..264: ecx=8, add get8 -> len=8+get8, writes len+1?? hw: ecx=8;557e9 get8;ecx+=eax; loop writes ecx+1
                    leaf("LIT9",&L);
                    uint32_t len=8+get8(&L); len+=1;
                    for(uint32_t i=0;i<len;i++){ wbyte(&L,L.edi,get8(&L)&0xFF); L.edi--; }
                } else {
                    // long match: len=get8+5; offset gate 0x1000/12b slow 8+4 (hw 0x5584b/0x55875)
                    leaf("LONG",&L);
                    uint32_t len=(get8(&L)&0xFF)+5;
                    uint32_t d=takeGate(&L,0x1000,12);
                    for(uint32_t k=0;k<len;k++) wbyte(&L,L.edi-k,rbyte(&L,L.edi+d-k));
                    L.edi-=len;
                }
            }
        }
        trace(L.edi,L.ebx,L.esi,L.cf);
        if((int32_t)(L.edi) < (int32_t)L.ebp && L.edi < L.ebp) break;   // cmp edi,ebp; jb exit (unsigned)
        if(L.edi < L.ebp) break;
    }
    if(Lout) *Lout=L0;
    return 0;
}
