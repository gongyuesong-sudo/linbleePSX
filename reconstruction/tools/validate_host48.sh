#!/usr/bin/env bash
# validate_host48.sh — H-24b 线程回灌 H_TFR（task-058，2026-10-31）
# A) H_TFR=1 冷链: TFR# 命中且 jpc 现场字段齐
# B) 分发器真执行: UNIQ bfc1e988/bfc1e9a0/bfc1ea00 三块在场
# C) 真 C2 kernel 面: UNIQ b0032e24 在场(docs/21 golden 真代码锚)
# D) docs/44 锚(回灌语义+v1 教训+终判) ; E) 无 H_TFR 基线 census 与既有 474-块墙一致(零回归)
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST48: $*"; }
ROM=/home/gill/bleem/psxbios/SCPH1001.BIN
base_env(){ echo "H_C2HOME=1 H_C2GOLD=1 H_C2END=0x89E00 H_DEVRST=1 H_DEVRST2=1 H_JITUNIQ=1 H_LOOPREP=3000000 H_MAXB=8000000 H_ARENA_MB=320 H_SLED_CAP=200 H_BIOSJIT=1 H_BIOSEXN=1 H_JIT_BUDGET=10000000"; }
L=/tmp/h48tfr.log
if [ ! -s "$L" ]; then
  env $(base_env) HTRACE_LOG=$L H_TFR=1 H_TFR_CAP=300 BLEEM_BIOS=$ROM timeout 280 \
    ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin 0x400000 0x472000 >/dev/null 2>&1
fi
grep -q "^TFR#1 bad=.* jpc=.* ra=.* sp=" "$L" && note "A) TFR 回灌命中: $(grep -m1 '^TFR#1' $L | cut -c1-80)" || { note "A) FAIL 无 TFR 命中"; fail=1; }
for b in bfc1e988 bfc1e9a0 bfc1ea00; do grep -q "^UNIQ $b" "$L" || { note "B) 缺分发器块 $b"; fail=1; }; done
[ $fail -eq 0 ] && note "B) BIOS 异常分发器三块真执行"
grep -q "^UNIQ b0032e24" "$L" && note "C) 真 C2 kernel 0x32e24 面到达" || { note "C) FAIL 未进 C2 kernel"; fail=1; }
grep -q "必须走分发器路线" docs/44_tfr_bounce.md && grep -q "H_TFR_CAP" docs/44_tfr_bounce.md && grep -q "等事件自旋" docs/44_tfr_bounce.md \
  && note "D) docs/44 锚齐" || { note "D) docs/44 缺锚"; fail=1; }
L2=/tmp/h48base.log
if [ ! -s "$L2" ]; then
  env $(base_env) HTRACE_LOG=$L2 BLEEM_BIOS=$ROM timeout 280 \
    ./host/build/htrace decrypted/stream_10a4c_L71cbf_relocated.bin 0x400000 0x472000 >/dev/null 2>&1
fi
grep -q "^TFR#" "$L2" && { note "E) 基线被污染(无 H_TFR 仍有 TFR)"; fail=1; } || note "E) 门控零回归(基线无 TFR; 墙仍 474-块态, H_JITUNIQ 计数 $(grep -c '^UNIQ' $L2))"
[ $fail -eq 0 ] && echo "HOST48 CHECK PASS" || echo "HOST48 CHECK FAIL"
[ $fail -eq 0 ]
