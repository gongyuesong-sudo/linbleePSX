#!/usr/bin/env bash
# validate_host124.sh — 清洁房楔5 门: GPU v1 + Vulkan 硬件加速 (task-132)
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST124: $*"; }
cd /home/gill/bleem/port

# A) 重建 + 八面 selftest 全绿 (零 BIOS)
env -u BLEEM_BIOS bash -c 'rm -f build/bleem_port && cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/dev/null 2>&1' && note "A) 重建 PASS" || { note "A) 重建 FAIL"; fail=1; }
ok=1; tot=""
for t in psx cpu dev eng hle cd boot gpu; do
  env -u BLEEM_BIOS timeout 60 ./build/bleem_port --$t-selftest >/tmp/h124_$t.log 2>&1 || ok=0
  p=$(grep -c ': PASS' /tmp/h124_$t.log); f=$(grep -c ': FAIL' /tmp/h124_$t.log)
  eval "p_$t=$p"; tot="$tot ${t}=${p}P/${f}F"; [ "$f" = 0 ] || ok=0
done
[ $ok = 1 ] && [ "${p_gpu}" -ge 9 ] && [ "${p_hle}" -ge 20 ] && [ "${p_boot}" -ge 7 ] && \
  note "B) 八面回归$tot" || { note "B) FAIL$tot"; fail=1; }

# C) Vulkan 硬件加速面: SPIR-V 内嵌在账 + X11 实窗 hw 通道出画
grep -q "psx_scanout_spv\[\]" src/psx_gpu_scanout_spv.h && grep -q "vk_host_present_gpu16" src/vk_host.c && \
  grep -q "psx_gpu_attach_hw" src/psx_gpu.c && note "C1) 硬件扫描通道代码面(SPIR-V+present_gpu16+attach_hw)" || { note "C1) FAIL"; fail=1; }
rm -f /tmp/hb_window.iso /tmp/hb_window.iso.cue
export DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
out=$(env -u BLEEM_BIOS timeout 60 ./build/bleem_port --psx-window --seconds 2 2>&1 | tail -1)
echo "$out" | grep -q "hw=1" && echo "$out" | grep -qE "fill=76800" && echo "$out" | grep -qE "frames=[1-9][0-9]*" && \
  note "C2) hw 实窗 PASS: $out" || { note "C2) FAIL: $out"; fail=1; }
# soft 双路对照
out2=$(env -u BLEEM_BIOS timeout 60 ./build/bleem_port --psx-window --gpumode soft --seconds 1 2>&1 | tail -1)
echo "$out2" | grep -q "hw=0" && echo "$out2" | grep -qE "fill=76800" && note "C3) soft 对照 PASS" || { note "C3) FAIL: $out2"; fail=1; }

# D) 零 BIOS 纪律: 全部运行日志无 BIOS 装载痕迹
grep -lqi "SCPH\|bios load" /tmp/h124_*.log && { note "D) FAIL 日志见 BIOS"; fail=1; } || note "D) 八面日志零 BIOS"

# E) GitHub 强制同步
if timeout 300 bash /home/gill/bleem/rewrite/tools/sync_to_github.sh "task-132 GPU v1 + Vulkan compute hw scanout (zero BIOS, window proven)" >/tmp/h124_sync.log 2>&1; then
  ahead=$(git -C /home/gill/bleem/linbleePSX rev-list --count origin/main..HEAD)
  [ "$ahead" = 0 ] && note "E) GitHub 同步 PASS" || { note "E) FAIL ahead=$ahead"; fail=1; }
else note "E) sync FAIL: $(tail -2 /tmp/h124_sync.log)"; fail=1; fi

[ $fail = 0 ] && echo "HOST124: ALL PASS" || echo "HOST124: FAIL"
exit $fail
