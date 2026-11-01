#!/usr/bin/env bash
# validate_host122.sh — 清洁房楔3 门: ISO 装载面 (task-130)
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST122: $*"; }

# A) 重建 + 六面 selftest 全绿 (零 BIOS)
cd /home/gill/bleem/port
env -u BLEEM_BIOS bash -c 'rm -f build/bleem_port && cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/dev/null 2>&1' && note "A) 重建 PASS" || { note "A) 重建 FAIL"; fail=1; }
ok=1; tot=""
for t in psx cpu dev eng hle cd; do
  env -u BLEEM_BIOS timeout 60 ./build/bleem_port --$t-selftest >/tmp/h122_$t.log 2>&1 || ok=0
  p=$(grep -c ': PASS' /tmp/h122_$t.log); f=$(grep -c ': FAIL' /tmp/h122_$t.log)
  eval "p_$t=$p"; tot="$tot ${t}=${p}P/${f}F"; [ "$f" = 0 ] || ok=0
done
[ $ok = 1 ] && [ "${p_hle}" -ge 20 ] && [ "${p_cd}" -ge 6 ] && [ "${p_dev}" -ge 10 ] && \
  note "B) 六面回归$tot" || { note "B) FAIL$tot"; fail=1; }

# C) 自研读取器存在且零外部镜像依赖 (不链 libchd/libcdio)
ldd build/bleem_port | grep -Ei "libcdio|libchd|libcue" && { note "C) FAIL 外部光盘库链接"; fail=1; } || \
  grep -q "psx_disc_open" src/psx_cdrom.c && note "C) 自研读取器(零 libcdio/libchd) PASS" || { note "C) FAIL"; fail=1; }

# D) 合成盘不入库: 镜像树无 .iso/.cue/测试盘
hits=$(git -C /home/gill/bleem/linbleePSX ls-files | grep -Eic '\.(iso|cue|chd|pbp)$' || true)
[ "$hits" = 0 ] && note "D) 测试盘零入库" || { note "D) FAIL hits=$hits"; fail=1; }

# E) 强制同步 GitHub
if timeout 300 bash /home/gill/bleem/rewrite/tools/sync_to_github.sh "task-130 CD reader (BIN/CUE/ISO9660 + cmd state machine + file API)" >/tmp/h122_sync.log 2>&1; then
  ahead=$(git -C /home/gill/bleem/linbleePSX rev-list --count origin/main..HEAD)
  [ "$ahead" = 0 ] && note "E) GitHub 同步 PASS" || { note "E) FAIL ahead=$ahead"; fail=1; }
else note "E) sync FAIL: $(tail -2 /tmp/h122_sync.log)"; fail=1; fi

[ $fail = 0 ] && echo "HOST122: ALL PASS" || echo "HOST122: FAIL"
exit $fail
