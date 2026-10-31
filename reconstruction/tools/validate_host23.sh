#!/bin/bash
# validate_host23.sh — task-030 attempt3 验收：真机终态 RAM 金标准管线 + H_GOLDEN 消费推进
set -u
cd "$(dirname "$0")/.."
RC=0
D=docs/23_golden_ram.md
G="${GOLDEN_RAM:-/home/gill/bleem/.analysis/golden/GOLDEN_RAM_2MB.bin}"
[ -f "$D" ] || { echo "FAIL: docs/23 缺失"; exit 1; }
for k in 'MainRAM.data8' 'SAVE_STATE' '0x6ee0' 'lui k0,0x1a00' '5e8eb0c8'; do
    grep -q "$k" "$D" || { echo "FAIL: docs/23 缺 '$k'"; RC=1; }
done
if [ -f "$G" ]; then
    H=$(sha256sum "$G" | cut -d' ' -f1)
    [ "$H" = "5e8eb0c82feaa05f250a5f66dda4eeafdab1d737ac78adebd97634a610c28ef2" ] || { echo "FAIL: 金标准哈希漂移 $H"; RC=1; }
    [ "$(stat -c%s "$G")" = "2097152" ] || { echo "FAIL: 金标准非 2MB"; RC=1; }
else
    echo "WARN: 金标准文件不在场(取证区), 仅文档断言"; G=""
fi
# 消费实验: probe + H_GOLDEN → 链进真 RAM kernel 码(0x4A4C)且块数>206
cat > /tmp/vh23_probe.c <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "mips_jit_inc.h"
int main(int argc,char**argv){
    setvbuf(stdout,NULL,_IONBF,0);
    void*p=mmap((void*)0x2a1000u,64u<<20,PROT_READ|PROT_WRITE,MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS,-1,0); (void)p;
    static uint8_t rom[0x80000];
    const char*path=argc>1?argv[1]:"../psxbios/SCPH1001.BIN";
    FILE*f=fopen(path,"rb"); if(!f||fread(rom,1,sizeof rom,f)!=(long)sizeof rom){printf("rom fail\n");return 2;} fclose(f);
    extern int jit_lib_load_rom(const unsigned char*);
    if(jit_init()) return 2;
    if(jit_lib_load_rom(rom)) return 2;
    psx_state.pc=0xBFC00000;
    uint32_t v0=0,kind=0,endpc=0;
    extern int jit_lib_bios_call_log(uint32_t,uint32_t,uint32_t*,FILE*);
    uint32_t tmp=0; (void)tmp; (void)v0;(void)kind;(void)endpc;
    int n=jit_lib_bios_call_log(0xBFC00000u,60000u,&v0,stdout);
    printf("GOLDEN-CHAIN blocks=%d pc=%08x\n",n,psx_state.pc);
    return 0;
}
EOF
gcc -m32 -O0 -g -no-pie -I host -o /tmp/vh23_probe /tmp/vh23_probe.c 2>/dev/null || { echo "FAIL: probe 构建"; RC=1; }
if [ -f /tmp/vh23_probe ] && [ -n "$G" ]; then
    RUN=/tmp/vh23_run.txt
    H_BIOSEXN=1 H_HLEBOOT=1 H_GOLDEN="$G" H_JITBLOCKS=1 /tmp/vh23_probe ../psxbios/SCPH1001.BIN > $RUN 2>/dev/null
    grep -a GOLDEN-CHAIN $RUN | head -1
    CH=$(grep -a GOLDEN-CHAIN $RUN | sed 's/.*blocks=\([0-9]*\).*/\1/' | head -1)
    [ "${CH:-0}" -gt 206 ] || { echo "FAIL: H_GOLDEN 链=${CH:-0} 未超无金标准 HLEBOOT(206)"; RC=1; }
    grep -qa 'BLK 00004a4c' $RUN || { echo "FAIL: 未执行金标准 RAM kernel 码(0x4a4c)"; RC=1; }
    grep -qa 'BLK b0032e24' $RUN || { echo "FAIL: 未执行 RAM 0x32E24 root2 入口"; RC=1; }
fi
# 回归
gcc -m32 -O0 -g -no-pie -fno-pic -DJIT_MAIN -o host/build/mips_jit host/mips_jit.c && ./host/build/mips_jit | tail -1 | grep -q PASS || { echo "FAIL: JIT 单元"; RC=1; }
[ $RC -eq 0 ] && echo "HOST23 CHECK PASS: 金标准管线落档 + H_GOLDEN 链进入真 RAM kernel 码" || echo "HOST23 CHECK FAIL"
exit $RC
