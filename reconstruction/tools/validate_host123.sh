#!/usr/bin/env bash
# validate_host123.sh — 清洁房楔4 门: 冷启链贯通 (task-131)
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST123: $*"; }
cd /home/gill/bleem/port

# A) 重建 + 七面 selftest 全绿 (零 BIOS)
env -u BLEEM_BIOS bash -c 'rm -f build/bleem_port && cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/dev/null 2>&1' && note "A) 重建 PASS" || { note "A) 重建 FAIL"; fail=1; }
ok=1; tot=""
for t in psx cpu dev eng hle cd boot; do
  env -u BLEEM_BIOS timeout 60 ./build/bleem_port --$t-selftest >/tmp/h123_$t.log 2>&1 || ok=0
  p=$(grep -c ': PASS' /tmp/h123_$t.log); f=$(grep -c ': FAIL' /tmp/h123_$t.log)
  eval "p_$t=$p"; tot="$tot ${t}=${p}P/${f}F"; [ "$f" = 0 ] || ok=0
done
[ $ok = 1 ] && [ "${p_boot}" -ge 7 ] && [ "${p_hle}" -ge 20 ] && [ "${p_cd}" -ge 6 ] && \
  note "B) 七面回归$tot" || { note "B) FAIL$tot"; fail=1; }

# C) 冷启链语义钉: stdout 含内核 putchar 输出 'B' + BOOT 断言名在账
grep -q "^B" /tmp/h123_boot.log || grep -q "BBOOT_TEST\|BBBB" /tmp/h123_boot.log && \
  grep -q "b3_putchar_via_kernel: PASS" /tmp/h123_boot.log && \
  grep -q "b4_marker_ram: PASS" /tmp/h123_boot.log && \
  note "C) 冷跳→Execute→内核调用→RAM 写回链 PASS" || { note "C) FAIL"; fail=1; }

# D) 零 BIOS 全链证据: boot 面在未设 BLEEM_BIOS 环境跑过 + 无 SCPH 装载日志
grep -qi "bios.*load\|SCPH" /tmp/h123_boot.log && { note "D) FAIL boot 日志见 BIOS 装载"; fail=1; } || \
  note "D) boot 链零 BIOS 日志面 PASS"

# E) GitHub 强制同步
if timeout 300 bash /home/gill/bleem/rewrite/tools/sync_to_github.sh "task-131 cold boot chain (PSX.EXE from disc -> HLE Execute, zero BIOS)" >/tmp/h123_sync.log 2>&1; then
  ahead=$(git -C /home/gill/bleem/linbleePSX rev-list --count origin/main..HEAD)
  [ "$ahead" = 0 ] && note "E) GitHub 同步 PASS" || { note "E) FAIL ahead=$ahead"; fail=1; }
else note "E) sync FAIL: $(tail -2 /tmp/h123_sync.log)"; fail=1; fi

[ $fail = 0 ] && echo "HOST123: ALL PASS" || echo "HOST123: FAIL"
exit $fail
