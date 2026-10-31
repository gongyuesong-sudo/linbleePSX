// reloc.c — for each module stream, locate + apply bleem relocation chain
// Format (from 0x5556d walk): pair stream: dword hdr = (count<<1)|? ; entries:
//   lods eax; shr eax,1; je end;  -> scratch_slot_index*4 (+ flag CF: [ecx], alloc+val)
//   lods eax; -> patch value index ; then jae/jb pick addend source table
// We instead brute-force: scan file [lo..hi) for a stream S where
//   dwords[i]=(k<<1), dwords[i+1]=(v): applying out[4k]+=base_delta... too indirect.
// Simplification: bleem patches out[4k] += (alloc2 + v*?) — but empirically all code
// references are 0x401xxx-0x455xxx = orig_image+X. So likely delta = NEWBASE-0x400000
// applied at slots listed by the chain; find the chain by pattern: pairs (even dword, small).
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
int main(void){
    FILE*f=fopen("/home/gill/bleem/demo-v1_4.EXE","rb");
    fseek(f,0,SEEK_END);uint32_t fs=ftell(f);rewind(f);
    uint8_t*img=mmap(NULL,fs,PROT_READ,MAP_PRIVATE,fileno(f),0);
    // modules: 0x0facc L=0xba3, 0x1015c L=0x10e7, 0x10a4c L=0x71cbf
    // reloc stream for module j located at 0x5555f: add esi,[esi-2] — pointer to stream is
    // (esi&~3) and reloc table = esi - [esi-2]. Try: for each module stream region, the
    // table lies just BEFORE its header (dword before sb = reloc_len).
    struct {uint32_t sb, L;} mods[]={{0xfacc,0xba3},{0x1015c,0x10e7},{0x10a4c,0x71cbf}};
    for(int m=0;m<3;m++){
        uint32_t sb=mods[m].sb, L=mods[m].L;
        printf("mod sb=%#x L=%#x: dwords before sb:", sb, L);
        for(int k=-6;k<0;k++) printf(" %#x", *(uint32_t*)(img+sb+4*k));
        printf("\n after end:");
        for(int k=0;k<6;k++) printf(" %#x", *(uint32_t*)(img+sb+L+4*k));
        printf("\n");
    }
    return 0;
}
