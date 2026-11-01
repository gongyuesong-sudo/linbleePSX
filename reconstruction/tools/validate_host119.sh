#!/usr/bin/env bash
# validate_host119.sh — ③战役楔5 payload 内容面 REA 全量反编译门(task-127, 2026-11-01)
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST119: $*"; }
R=/home/gill/bleem/rewrite; D=$R/decrypted/decompiled

# A) 管线确定性复跑: 入口扫描 + ELF 封装(结构不变量判, 不钉再生工件 sha)
python3 tools/engine_entries_scan.py >/tmp/h119_scan.log 2>&1 || { note "A) scan FAIL"; fail=1; }
z=$(python3 -c "import json;print(json.load(open('$D/engine_entries.json'))['stats']['zone172_hits'])")
t=$(python3 -c "import json;print(json.load(open('$D/engine_entries.json'))['stats']['total'])")
a154=$(python3 -c "import json;print(json.load(open('$D/engine_entries.json'))['stats']['archived'])")
python3 tools/wrap_engine_elf.py decrypted/stream_10a4c_L71cbf.bin $D/engine_entries.json /tmp/h119_engine.elf >/dev/null 2>&1
nsym=$(python3 - <<'EOF2'
import struct
b=open('/tmp/h119_engine.elf','rb').read()
e_shoff,=struct.unpack_from('<I',b,0x20); e_shentsize,=struct.unpack_from('<H',b,0x2e); e_shnum,=struct.unpack_from('<H',b,0x30)
n=0
for i in range(e_shnum):
    sh=b[e_shoff+i*e_shentsize:e_shoff+(i+1)*e_shentsize]
    typ,=struct.unpack_from('<I',sh,4)
    if typ==2:  # SHT_SYMTAB
        off,size,link,ents=struct.unpack_from('<IIII',sh,16)[0],struct.unpack_from('<I',sh,20)[0],None,struct.unpack_from('<I',sh,36)[0]
        n=(size)//(ents or 16)-1
print(n)
EOF2
)
sz=$(stat -c%s /tmp/h119_engine.elf)
{ [ "$z" -ge 24 ] && [ "$t" -ge 519 ] && [ "$a154" -ge 154 ] && [ "$nsym" -ge 519 ] && [ "$sz" -ge 486000 ]; } && note "A) 管线复现(zone172>=24 total>=519 legacy=154 sym=$nsym)" || { note "A) FAIL z=$z t=$t a=$a154 nsym=$nsym"; fail=1; }

# B) 存档齐全(>= 界) + 索引/统计在账
neng=$(ls $D/eng_all/eng_*.json 2>/dev/null | wc -l); nst=$(ls $D/stub/stub_*.json 2>/dev/null | wc -l)
nm1=$(ls $D/mod1/mod1_*.json 2>/dev/null | wc -l); nm2=$(ls $D/mod2/mod2_*.json 2>/dev/null | wc -l)
{ [ "$neng" -ge 519 ] && [ "$nst" -ge 20 ] && [ "$nm1" -ge 55 ] && [ "$nm2" -ge 28 ] && [ -s $D/index.md ] && [ -s $D/w127_stats.json ]; } \
  && note "B) 四目标存档(eng=$neng stub=$nst mod1=$nm1 mod2=$nm2)+index+stats" || { note "B) FAIL $neng/$nst/$nm1/$nm2"; fail=1; }
# 索引自洽: 表行数不超过盘上文件数(快照 vs 活态 >= 判)
irl=$(grep -c '^| 0x' $D/index.md)
{ [ "$irl" -le $((neng+nst+nm1+nm2)) ]; } && note "B2) 索引自洽(真码行=$irl<=存档)" || { note "B2) FAIL"; fail=1; }

# C) 关键事实断言(不可变载荷性质)
grep -q 'DAT_002a0000' $D/stub/stub_0x55895.json && grep -q 'in_FS_OFFSET' $D/stub/stub_0x55586.json \
  && note "C) 检测链实码在账(0x55895 svc→0x2a0000 间接调用 + 0x55586 PEB fs 直读)" || { note "C) FAIL"; fail=1; }
r6=$(grep -rl 'return 6;' $D/eng_all/ 2>/dev/null | wc -l)
[ "$r6" = 0 ] && note "C2) 引擎明文零静态 return 6(xref 负结果再证)" || { note "C2) FAIL r6=$r6"; fail=1; }

# D) port 四面回归(不回归语义改动)
cd /home/gill/bleem/port
rm -f build/bleem_port
if cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/tmp/h119_b.log 2>&1 && [ -x build/bleem_port ]; then note "D0) 重建 PASS"; else note "D0) FAIL"; grep -i error /tmp/h119_b.log | head -2; fail=1; fi
export BLEEM_BIOS=/home/gill/bleem/psxbios/SCPH1001.BIN
ok=1; fsum=0; tot=""
for x in psx cpu dev eng; do ./build/bleem_port --$x-selftest >/tmp/h119_$x.log 2>&1; r=$?; p=$(grep -c ": PASS" /tmp/h119_$x.log); f=$(grep -c ": FAIL" /tmp/h119_$x.log); tot="$tot ${x}=${p}P/${f}F"; eval "p_$x=$p"; fsum=$((fsum+f)); [ $r -eq 0 ] || ok=0; done
[ $ok -eq 1 ] && [ "$fsum" = "0" ] && [ "$p_psx" -ge 31 ] && [ "$p_cpu" -ge 16 ] && [ "$p_dev" -ge 10 ] && [ "$p_eng" -ge 9 ] && note "D) 四面回归$tot" || { note "D) FAIL$tot"; fail=1; }

# E) 门链 + final_check(前置产物不回归)
if timeout 590 bash $R/tools/validate_host118.sh 2>&1 | grep -q "HOST118 CHECK PASS"; then note "E1) host118 回归 PASS"; else note "E1) FAIL"; fail=1; fi
cd /home/gill/bleem/port
if timeout 240 bash tools/final_check.sh 2>&1 | grep -q "FINAL CHECK PASS"; then note "E2) final_check PASS"; else note "E2) FAIL"; fail=1; fi

# F) 文档账 + 金标准恒 + 版权工件不入 git
cd $R
dd=$(ls docs/*.md | wc -l); grep -q "108_payload_w5_rea" README.md && [ "$dd" -ge 109 ] && note "F1) docs=$dd+README" || { note "F1) FAIL docs=$dd"; fail=1; }
e=$(sha256sum /home/gill/bleem/demo-v1_4.EXE | cut -c1-8)
g2=$(sha256sum decrypted/stream_10a4c_L71cbf.bin | cut -c1-8)
bad=$(git ls-files | grep -icE "SCPH|psxbios|demo-v1_4|demo_patched|demo_v2|demo_v3|GOLDEN_RAM_2MB\.bin$|\.PBP$|mem_rwx|mem_view|payload_view|engine_w127\.elf|bundle_" || true)
[ "$e" = "b61ee13e" ] && [ "$g2" = "98752b98" ] && [ "$bad" = "0" ] && note "F2) 金标准恒+取证工件不入 git" || { note "F2) FAIL $e/$g2/bad=$bad"; fail=1; }

[ $fail -eq 0 ] && echo "HOST119 CHECK PASS" || echo "HOST119 CHECK FAIL"
[ $fail -eq 0 ]
