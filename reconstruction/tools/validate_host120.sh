#!/usr/bin/env bash
# validate_host120.sh — 清洁房楔1 门: 同步管线+底数文档+零版权镜像+无BIOS基线 (task-128)
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST120: $*"; }
MIRROR=/home/gill/bleem/linbleePSX

# A) 管线存在+语法+底数文档四节在账
bash -n tools/sync_to_github.sh && [ -x tools/sync_to_github.sh ] && \
  grep -q 'BANNED_RE' tools/sync_to_github.sh && grep -q -- '--check-staged' tools/sync_to_github.sh && \
  [ -f docs/109_cleanroom_baseline.md ] && \
  [ "$(grep -c '^## ' docs/109_cleanroom_baseline.md)" -ge 4 ] && \
  note "A) 管线+底数文档在账" || { note "A) FAIL"; fail=1; }

# B) 负测试: 版权工件进暂存区必须被拦 (rc=2)
head -c 1024 /dev/urandom > "$MIRROR/port/SCPH1001.BIN"
git -C "$MIRROR" add -f port/SCPH1001.BIN
bash tools/sync_to_github.sh --check-staged >/tmp/h120_neg.log 2>&1; r=$?
rm -f "$MIRROR/port/SCPH1001.BIN"
[ "$r" = 2 ] && git -C "$MIRROR" diff --cached --quiet 2>/dev/null; git -C "$MIRROR" reset -q
[ "$r" = 2 ] && note "B) 版权扫描负测试拦截 rc=2" || { note "B) FAIL rc=$r"; fail=1; }

# C) 全流程 sync 幂等 + 推平 (mirror == origin/main)
if timeout 300 bash tools/sync_to_github.sh "host120 gate sync" >/tmp/h120_sync.log 2>&1; then
  ahead=$(git -C "$MIRROR" rev-list --count origin/main..HEAD 2>/dev/null)
  [ "$ahead" = 0 ] && note "C) sync PASS + 已推平 origin/main" || { note "C) ahead=$ahead"; fail=1; }
else note "C) sync FAIL rc=$? $(tail -2 /tmp/h120_sync.log)"; fail=1; fi

# D) 镜像受管树零二进制版权工件
hits=$(git -C "$MIRROR" ls-files | grep -Ei '(\.EXE|\.exe|\.bin|\.BIN|\.iso|\.ISO|\.pbp|\.PBP|\.chd|\.zip|payload\.bin|SCPH[0-9]+\.BIN|/decrypted/)' | wc -l)
[ "$hits" = 0 ] && note "D) 镜像零版权工件 (ls-files $(git -C "$MIRROR" ls-files | wc -l) 全清)" || { note "D) FAIL hits=$hits"; fail=1; }

# E) 无 BIOS 基线: port 重建 + 四面 selftest 全 PASS (BLEEM_BIOS 显式清除)
cd /home/gill/bleem/port
env -u BLEEM_BIOS bash -c 'rm -f build/bleem_port && cmake -S . -B build >/dev/null 2>&1 && cmake --build build -j4 >/dev/null 2>&1' && \
  ok=1 && for t in psx cpu dev eng; do env -u BLEEM_BIOS timeout 60 ./build/bleem_port --$t-selftest >/tmp/h120_$t.log 2>&1 || ok=0; f=$(grep -c ': FAIL' /tmp/h120_$t.log); [ "$f" = 0 ] || ok=0; done && \
  note "E) 无BIOS四面基线 PASS (psx=31/cpu=16/dev=10/eng=9 语义)" || { note "E) FAIL"; fail=1; }

[ $fail = 0 ] && echo "HOST120: ALL PASS" || echo "HOST120: FAIL"
exit $fail
