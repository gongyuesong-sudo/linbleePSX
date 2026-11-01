#!/usr/bin/env bash
# validate_host126.sh — 清洁房楔7 门 (task-134): 零售盘装载面
# 真 PSX-EXE 布局 + $t1/$t2 正 ABI + ECMA 0x9C 根记录 + CNF BOOT=/STACK= 宽容解析
# + 2352 帧型自探(16/24/32) + A(0x49) GPU_cw + retail 诊断面。零版权。
set -u
fail=0; note(){ echo "HOST126: $*"; }
cd /home/gill/bleem/port

# A) 重建 + 八面 selftest (cd 面须 ≥10 = 含 t5-t8 零售型)
cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/dev/null 2>&1 && note "A1) 重建 PASS" || { note "A1) 重建 FAIL"; fail=1; }
ok=1; tot=""
for t in psx cpu dev eng hle cd boot gpu; do
  env -u BLEEM_BIOS timeout 60 ./build/bleem_port --$t-selftest >/tmp/h126_$t.log 2>&1 || ok=0
  p=$(grep -c ': PASS' /tmp/h126_$t.log); f=$(grep -c ': FAIL' /tmp/h126_$t.log)
  eval "p_$t=$p"; tot="$tot ${t}=${p}P/${f}F"; [ "$f" = 0 ] || ok=0
done
[ $ok = 1 ] && [ "${p_cd}" -ge 10 ] && [ "${p_hle}" -ge 20 ] && [ "${p_boot}" -ge 7 ] && \
  note "B) 八面回归$tot" || { note "B) FAIL$tot"; fail=1; }

# C) 零售型合成链诊断面: t5 盘 (data@24+CNF 空格变体) 跑 coldboot→rootcall 计数
out=$(env -u BLEEM_BIOS timeout 60 ./build/bleem_port --retail-probe /tmp/hb_retail24.bin 200000 2>&1)
echo "$out" | grep -q "entry=80040100" && echo "$out" | grep -q "exc=0" && \
  note "C) 合成零售盘 probe PASS: $(echo "$out" | grep ran=)" || { note "C) FAIL: $out"; fail=1; }

# D) 真 ABI 钉字面: 派发用 $t1(gpr9)/$t2(gpr10), 旧错位不得回潮
grep -q "gpr\[10\], cmd = cpu->gpr\[9\]" src/psx_hle.c && \
  grep -q "ADDIU(10, 0xA0)" src/psx_hle.c && \
  note "D) ABI 正字 t1=9/t2=10 在账" || { note "D) FAIL ABI 错位回潮"; fail=1; }

# E) 真 PSX-EXE 布局钉字: 夹具头 "PS-X EXE" + 加载器单块字段
grep -q 'memcpy(img, "PS-X EXE", 8)' src/psx_cdrom.c && \
  grep -q "dsize = hle_rd32(h, hdr_gpa + 0x1C)" src/psx_hle.c && \
  note "E) 真 EXE 布局在账 (单块 0x18/0x1C, SP基@0x30)" || { note "E) FAIL"; fail=1; }

# F) 可选真盘观测面 (用户自有库存在时, 只读零入库): 引导成功 = exc 不炸链
G="/home/gill/bleem/psgame/B 北斗神拳.bin"
if [ -f "$G" ]; then
  out2=$(env -u BLEEM_BIOS timeout 120 ./build/bleem_port --retail-probe "$G" 3000000 2>&1)
  echo "$out2" | grep -q "boot=SLPS_029.93" && echo "$out2" | grep -qE "exc=0 |served=[1-9]" && \
    note "F) 真盘观测 PASS(本地私): $(echo "$out2" | grep ran=)" || note "F) 真盘观测退化(不判死): $(echo "$out2" | grep ran= | head -1)"
else note "F) 真盘缺席跳过 (库可选)"; fi

# G) GitHub 强制同步
if timeout 300 bash /home/gill/bleem/rewrite/tools/sync_to_github.sh "task-134 retail disc loader: real PSX-EXE layout, t1/t2 ABI correction, ECMA PVD root, CNF BOOT/STACK, 2352 autodetect, GPU_cw A(0x49), retail probe" >/tmp/h126_sync.log 2>&1; then
  ahead=$(git -C /home/gill/bleem/linbleePSX rev-list --count origin/main..HEAD)
  [ "$ahead" = 0 ] && note "G) GitHub 同步 PASS" || { note "G) FAIL ahead=$ahead"; fail=1; }
else note "G) sync FAIL: $(tail -2 /tmp/h126_sync.log)"; fail=1; fi

[ $fail = 0 ] && echo "HOST126: ALL PASS" || echo "HOST126: FAIL"
exit $fail
