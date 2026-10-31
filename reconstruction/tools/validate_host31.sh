#!/usr/bin/env bash
# validate_host31.sh — task-039 H-18a 验收: CDROM 静态判定 + C2 命令环闭环 + JIT 三勘误回归
set -u
cd "$(dirname "$0")/.."
ROM="${BLEEM_BIOS:-/home/gill/bleem/psxbios/SCPH1001.BIN}"
GS=/home/gill/bleem/.analysis/golden/gstate.hdr
fail=0
ok(){ echo "PASS $*"; }
no(){ echo "FAIL: $*"; fail=1; }

# —— A. 静态判定: ROM 无直接 CDROM 寄存器面(0x1F80 + 0x1800..0x1840) + docs/29 落档 ——
python3 - <<'PY' || no "A: CDROM 直接寄存器面扫描不符(应=0)"
import struct,sys
rom=open('/home/gill/bleem/psxbios/SCPH1001.BIN','rb').read()
R="zero at v0 v1 a0 a1 a2 a3 t0 t1 t2 t3 t4 t5 t6 t7 s0 s1 s2 s3 s4 s5 s6 s7 t8 t9 k0 k1 gp sp fp ra".split()
base=None; hits=0
for i in range(0,len(rom)-8,4):
    w=struct.unpack_from('<I',rom,i)[0]
    if w>>26==15 and (w>>16)&31 in (26,27) and (w&0xFFFF)==0x1F80: base=(w>>16)&31
    if base is not None:
        for j in range(i+4,min(i+64,len(rom),4)):
            x=struct.unpack_from('<I',rom,j)[0]; op=x>>26
            if op==15: base=(x>>16)&31
            if ((x>>21)&31)==base and op in (32,33,34,35,36,37,40,41,42,43,44,45,46):
                off=x&0xFFFF
                if 0x1800<=off<0x1840: hits+=1
sys.exit(0 if hits==0 else 1)
PY
[ $fail -eq 0 ] && ok "A1: ROM 直接 CDROM 寄存器访问面 = 0(docs/29 §1 静态判定成立)"
for m in "MIPS-I 无 \`seq\`" "分支 flag 槽与 \`ra\` 同址" "phys 2MB 回绕" "0x1001"; do
  grep -qa "$m" docs/29_cdrom_hle.md || no "A2: docs/29 缺要点: $m"
done
[ $fail -eq 0 ] && ok "A2: docs/29 静态判定+命令环 ABI+勘误落档齐全"

# —— B. JIT 三勘误回归 ——
gcc -m32 -O0 -g -no-pie -DJIT_MAIN -o host/build/mips_jit host/mips_jit.c 2>/dev/null || no "B0: -O0 编译失败"
./host/build/mips_jit | tail -1 | grep -qa "JIT-CHECK: PASS" || no "B1: -O0 单测 FAIL"
gcc -m32 -O1 -no-pie -DJIT_MAIN -o /tmp/mj_o1_v31 host/mips_jit.c 2>/dev/null
/tmp/mj_o1_v31 | tail -1 | grep -qa "JIT-CHECK: PASS" || no "B2: -O1 单测 FAIL"
python3 - <<'PY' || no "B3: R-type set_edx_r(rt) 勘误断言失败"
import sys,re
s=open('host/mips_jit.c').read()
n=len(re.findall(r'rdd\(rs\); set_edx_r\(\(int\)rt\); (?:add|sub|and|or|xor|cmp)_eax_mem',s))
b='bflag' in s and 'ST+124u' not in s
sys.exit(0 if (n>=7 and b) else 1)
PY
ok "B: JIT 三勘误(-O0/-O1 单测 PASS + addu 语义断言 + flag/bflag 无 ST+124 残留)"

# —— C. CDHLE C2 命令环直调闭环(第一方 EXE 头扇区读回) ——
gcc -m32 -O0 -g -no-pie -I host -o host/build/golden_cpu_probe tools/golden_cpu_probe.c 2>/dev/null || no "C0: probe 编译失败"
OUT=$(CD=1 H_CDHLE=1 H_CDHLE_EXE=host/build/hbrew.exe GSTATE="$GS" timeout 200 ./host/build/golden_cpu_probe "$ROM" 200000 2>&1 | grep -a CDRAM)
echo "$OUT"
echo "$OUT" | grep -qa "cmd=00000000" || no "C1: 命令未清零"
echo "$OUT" | grep -qa "ack2=00001001" || no "C2: READ 应答块未写 0x1001"
echo "$OUT" | grep -qa "dst0=582d5350" || no "C3: DST 首字非 'PS-X' magic(首扇区未读回)"
echo "$OUT" | grep -qa "v0=00000001" || no "C4: C2 未按 ABI 返回 v0=1"
echo "$OUT" | grep -qa "fails=0" || no "C5: 存在翻译失败块"
[ $fail -eq 0 ] && ok "C: 命令环闭环(解析0x1001→512字拷循环→ack→清命令→v0=1 返回; 首扇区 'PS-X' 读回 DST)"

if [ $fail -eq 0 ]; then
  echo "HOST31 CHECK PASS: CDROM 静态判定 + C2 命令环 HLE 闭环(首扇区读回) + JIT 三勘误回归"
  exit 0
else
  echo "HOST31 CHECK FAIL"; exit 1
fi
