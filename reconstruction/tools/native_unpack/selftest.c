// selftest.c — decode engine stream via unpack_lib and diff against golden native output
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
int bleem_lz_decompress(const uint8_t*,uint32_t,uint32_t,uint8_t*,size_t,uint32_t*);
int main(void){
    FILE *f=fopen("/home/gill/bleem/demo-v1_4.EXE","rb");
    if(!f){perror("exe");return 2;}
    fseek(f,0,SEEK_END); long fs=ftell(f); rewind(f);
    uint8_t *img=malloc(fs); if(fread(img,1,fs,f)!=fs) return 2; fclose(f);
    // golden
    f=fopen("/home/gill/bleem/.analysis/native_dec.bin","rb");
    if(!f){printf("no golden (run needseed first)\n");return 3;}
    fseek(f,0,SEEK_END); long gl=ftell(f); rewind(f);
    uint8_t *gold=malloc(gl); if(fread(gold,1,gl,f)!=gl) return 2; fclose(f);

    static uint8_t out[0x80000];
    uint32_t L=0;
    extern int bleem_trace_on;
    bleem_trace_on = getenv("BLEEM_TRACE")?1:0;
    int rc=bleem_lz_decompress(img,0,0x10a4c,out,sizeof out,&L);
    if(bleem_trace_on){
        extern int bleem_trace_get(unsigned,unsigned*,unsigned*,unsigned*,unsigned*);
        FILE*w=fopen("/tmp/c_tokens.txt","w");
        extern const char* bleem_trace_name(unsigned);
        for(unsigned i=0;;i++){unsigned a,b,c,d; if(bleem_trace_get(i,&a,&b,&c,&d))break; fprintf(w,"%u %u %u %u %s\n",a,b,c,d,bleem_trace_name(i)?bleem_trace_name(i):"END");}
        fclose(w); printf("trace tokens written\n"); return 0;
    }
    if(rc){printf("decompress rc=%d\n",rc);return 1;}
    if((long)L!=gl){printf("LEN mismatch %u vs %ld\n",L,gl);}
    long n=L<(uint32_t)gl?L:gl, bad=0, first=-1;
    for(long i=0;i<n;i++) if(out[i]!=gold[i]){ if(first<0)first=i; bad++; }
    if(bad==0){ printf("selftest PASS: %ld bytes bit-exact vs native golden\n",n); return 0; }
    printf("selftest FAIL: bad=%ld/%ld first=%ld\n",bad,n,first);
    printf("  ours: %02x %02x %02x %02x\n",out[first],out[first+1],out[first+2],out[first+3]);
    printf("  gold: %02x %02x %02x %02x\n",gold[first],gold[first+1],gold[first+2],gold[first+3]);
    return 1;
}
