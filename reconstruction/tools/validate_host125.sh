#!/usr/bin/env bash
# validate_host125.sh — 清洁房楔6 终验门 (task-133):
# 端到端零 BIOS 零 payload + 镜像零版权工件 + README 能力表 + GitHub 同步
set -u
fail=0; note(){ echo "HOST125: $*"; }
cd /home/gill/bleem/port

# A) 全仓源码零版权运行时依赖: 清洁房八面在无 BIOS/无 payload 环境跑通
res=$(env -i PATH=/usr/bin:/bin HOME=/tmp bash -c '
  ok=1; tot=""
  for t in psx cpu dev eng hle cd boot gpu; do
    ./build/bleem_port --$t-selftest >/tmp/h125_$t.log 2>&1 || ok=0
    f=$(grep -c ": FAIL" /tmp/h125_$t.log); p=$(grep -c ": PASS" /tmp/h125_$t.log)
    tot="$tot ${t}=${p}P/${f}F"; [ "$f" = 0 ] || ok=0
  done
  echo "PURE$ok $tot"')
case "$res" in PURE1\ *) note "A) 纯净环境(env -i)八面回归${res#PURE1}";; *) note "A) FAIL: $res"; fail=1;; esac

# B) 端到端: ISO→冷跳→Execute→GP0 流→Vulkan 硬件出画 (X11)
rm -f /tmp/hb_window.iso /tmp/hb_window.iso.cue
export DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
out=$(env -u BLEEM_BIOS timeout 60 ./build/bleem_port --psx-window --seconds 2 2>&1 | tail -1)
echo "$out" | grep -q "hw=1" && echo "$out" | grep -qE "fill=76800 tri=[0-9]+ spr=256 xfer=256 gp0=2[0-9][0-9]" && \
  echo "$out" | grep -qE "frames=[1-9][0-9]*" && note "B) 端到端 hw 出画 PASS: $out" || { note "B) FAIL: $out"; fail=1; }

# C) README 能力表在账
grep -q "能力表" README.md && grep -q "Vulkan" README.md && grep -q "零版权纪律" README.md && \
  note "C) README 能力表+零版权声明 PASS" || { note "C) FAIL README 缺能力表"; fail=1; }

# D) 镜像发布审计: 已跟踪文件零版权工件 (BIOS/payload/盘镜像/解密流/golden)
M=/home/gill/bleem/linbleePSX
hits=$(git -C $M ls-files | grep -iE "\.EXE$|payload|\.bin$|\.iso$|\.pbp$|\.chd$|\.cue$|\.zip$|SCPH|decrypt|golden|dump" | grep -viE "\.comp$" || true)
[ -z "$hits" ] && note "D) 镜像 ls-files 零版权工件 ($(git -C $M ls-files | wc -l) 文件全清)" || { note "D) FAIL 命中: $hits"; fail=1; }
# 镜像 README = port README 同步在账
grep -q "零版权纪律" $M/README.md 2>/dev/null || grep -rq "零版权纪律" $M --include="README.md" && \
  note "D2) 镜像 README 能力表在账" || { note "D2) 镜像 README 未同步"; fail=1; }

# E) GitHub 强制同步 (含本次 README/账本)
if timeout 300 bash /home/gill/bleem/rewrite/tools/sync_to_github.sh "task-133 e2e acceptance: README capability table, pure-env 8-surface regression, zero-copyright mirror audit" >/tmp/h125_sync.log 2>&1; then
  ahead=$(git -C $M rev-list --count origin/main..HEAD)
  [ "$ahead" = 0 ] && note "E) GitHub 同步 PASS (ahead=0)" || { note "E) FAIL ahead=$ahead"; fail=1; }
else note "E) sync FAIL: $(tail -2 /tmp/h125_sync.log)"; fail=1; fi

# F) 负测试: 镜像塞 BIOS 必须被拒 (rc=2)
printf 'FAKEBIOS' > $M/SCPH1001.BIN && git -C $M add -A
rc=0; bash /home/gill/bleem/rewrite/tools/sync_to_github.sh --check-staged >/dev/null 2>&1 || rc=$?
git -C $M reset -q && rm -f $M/SCPH1001.BIN
[ "$rc" = 2 ] && note "F) 版权负测试拦截 rc=2 PASS" || { note "F) FAIL rc=$rc"; fail=1; }

[ $fail = 0 ] && echo "HOST125: ALL PASS" || echo "HOST125: FAIL"
exit $fail
