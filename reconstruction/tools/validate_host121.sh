#!/usr/bin/env bash
# validate_host121.sh — 清洁房楔2 门: HLE 内核 v1 (task-129)
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST121: $*"; }

# A) port 全量重建 + 五面 selftest (BLEEM_BIOS 显式清除 = 零 BIOS 基线)
cd /home/gill/bleem/port
if env -u BLEEM_BIOS bash -c 'rm -f build/bleem_port && cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/dev/null 2>&1'; then
  note "A) 重建 PASS"; else note "A) 重建 FAIL"; fail=1; fi
ok=1; tot=""
for t in psx cpu dev eng hle; do
  env -u BLEEM_BIOS timeout 60 ./build/bleem_port --$t-selftest >/tmp/h121_$t.log 2>&1 || ok=0
  p=$(grep -c ': PASS' /tmp/h121_$t.log); f=$(grep -c ': FAIL' /tmp/h121_$t.log)
  eval "p_$t=$p"
  tot="$tot ${t}=${p}P/${f}F"; [ "$f" = 0 ] || ok=0
done
[ $ok = 1 ] && [ "${p_psx}" -ge 31 ] && [ "${p_cpu}" -ge 16 ] && [ "${p_dev}" -ge 10 ] && \
  [ "${p_eng}" -ge 9 ] && [ "${p_hle}" -ge 18 ] && note "B) 五面回归$tot" || { note "B) FAIL$tot"; fail=1; }

# C) 零 BIOS 纪律 grep 面: psx_hle.c 不引用 BLEEM_BIOS 装载路径, 且装载器存在
grep -q "BLEEM_BIOS" /home/gill/bleem/port/src/psx_hle.c && { note "C) FAIL hle 引用 BLEEM_BIOS"; fail=1; } || \
  grep -q "hle_load_exe" /home/gill/bleem/port/src/psx_hle.c && note "C) HLE 零 BIOS 纪律面 PASS" || { note "C) FAIL load_exe 缺"; fail=1; }

# D) 命令号来源登记在账 (公开 ABI 三源标记注释)
c=$(grep -cE '\[S|\[R|\[D|\[P' /home/gill/bleem/port/src/psx_hle.c)
[ "$c" -ge 4 ] && note "D) 号表来源登记 $c 处" || { note "D) FAIL 登记=$c"; fail=1; }

# E) 强制同步: sync PASS + mirror==origin/main + 镜像零版权工件
if timeout 300 bash /home/gill/bleem/rewrite/tools/sync_to_github.sh "task-129 HLE kernel v1 (rootcall A/B/C + selftest 19)" >/tmp/h121_sync.log 2>&1; then
  ahead=$(git -C /home/gill/bleem/linbleePSX rev-list --count origin/main..HEAD)
  hits=$(git -C /home/gill/bleem/linbleePSX ls-files | grep -Eic '(\.EXE|\.exe|\.bin|\.BIN|\.iso|\.ISO|\.pbp|payload\.bin|SCPH[0-9]+\.BIN)' || true)
  [ "$ahead" = 0 ] && [ "$hits" = 0 ] && note "E) GitHub 同步 PASS (推平+零版权工件)" || { note "E) FAIL ahead=$ahead hits=$hits"; fail=1; }
else note "E) sync FAIL: $(tail -2 /tmp/h121_sync.log)"; fail=1; fi

[ $fail = 0 ] && echo "HOST121: ALL PASS" || echo "HOST121: FAIL"
exit $fail
