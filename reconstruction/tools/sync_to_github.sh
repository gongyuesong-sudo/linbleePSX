#!/usr/bin/env bash
# sync_to_github.sh — 清洁房战役强制同步管线 (task-128, 2026-11-01)
#
# 职责: port/ + rewrite/ → linbleePSX 镜像 → GitHub。每楔强制跑一遍。
#   1) rsync 两棵源树进镜像 (排除版权工件/构建产物/取证区)
#   2) 版权前缀扫描: 暂存区命中任何二进制版权模式 = 硬失败 (git reset 回滚, rc=2)
#   3) commit (harness 署名) + push origin main
#
# 用法:
#   sync_to_github.sh "docs/109 baseline + 首发 sync"   # 全流程
#   sync_to_github.sh --check-staged                    # 仅扫当前暂存区 (验收门负测试用)
set -u

ROOT=/home/gill/bleem
MIRROR="$ROOT/linbleePSX"
[ -d "$MIRROR/.git" ] || { echo "SYNC: FATAL mirror missing"; exit 1; }

# 版权工件禁列 (二进制/镜像/解密密文/取证 dump)。文档文本(.md/.c/.py)不在此列。
BANNED_RE='(\.EXE|\.exe|\.bin|\.BIN|\.iso|\.ISO|\.pbp|\.PBP|\.chd|\.CHD|\.zip|\.7z|\.tar(\.gz)?$|^decrypted/|/decrypted/|^data/|/data/|payload\.bin|SCPH[0-9]+\.BIN|native_dec\.bin|tokens_gold|leaves_gold|steps516\.json|/dumps/|^dumps/|\.ppm$)'

check_staged() {
  # 扫暂存区新增/修改文件; 命中禁列 → 回滚暂存并 rc=2
  local hits
  hits=$(git -C "$MIRROR" diff --cached --name-only | grep -E "$BANNED_RE" || true)
  if [ -n "$hits" ]; then
    echo "SYNC: COPYRIGHT SCAN FAIL — 以下禁列工件进入暂存区:"
    echo "$hits" | sed 's/^/  /'
    git -C "$MIRROR" reset -q
    return 2
  fi
  echo "SYNC: copyright scan PASS (staged=$(git -C "$MIRROR" diff --cached --name-only | wc -l) files)"
  return 0
}

if [ "${1:-}" = "--check-staged" ]; then
  check_staged; exit $?
fi

MSG="${1:?usage: sync_to_github.sh \"commit suffix\"}"

RSYNC_EXCL=(--exclude .git --exclude build/ --exclude data/ --exclude dumps/
  --exclude decrypted/ --exclude sandbox/ --exclude analysis/
  --exclude .harness-active --exclude '__pycache__/' --exclude '*.o' --exclude '*.pyc'
  --exclude '*.EXE' --exclude '*.exe' --exclude '*.bin' --exclude '*.BIN'
  --exclude '*.iso' --exclude '*.ISO' --exclude '*.pbp' --exclude '*.chd'
  --exclude '*.zip' --exclude '*.ppm' --exclude '*.7z')

rsync -a --delete "${RSYNC_EXCL[@]}" "$ROOT/port/"      "$MIRROR/port/" || exit 1
# rewrite → reconstruction 的既有发布映射: docs/host/include/src/tools + 顶层账本文件;
# 用 --delete 保持与源一致, 但保留镜像独有的历史目录 (port_src/ rc/ 为归档快照)
rsync -a "${RSYNC_EXCL[@]}" --exclude 'port_src/' --exclude 'rc/' \
  --delete "$ROOT/rewrite/" "$MIRROR/reconstruction/" || exit 1
# 镜像独有的归档目录被 --delete 清掉时恢复 (rsync 无对应源=会删; 上面 --filter 保护):
git -C "$MIRROR" checkout -- port_src rc 2>/dev/null || true

git -C "$MIRROR" add -A || exit 1
check_staged || exit 2

if git -C "$MIRROR" diff --cached --quiet; then
  echo "SYNC: no changes to commit"
else
  git -C "$MIRROR" -c user.name=harness -c user.email=harness@local \
    commit -q -m "sync SESSION-139: $MSG" || exit 1
  echo "SYNC: committed $(git -C "$MIRROR" rev-parse --short HEAD)"
fi

git -C "$MIRROR" push -q origin main || { echo "SYNC: PUSH FAIL (commit kept)"; exit 3; }
ahead=$(git -C "$MIRROR" rev-list --count origin/main..HEAD)
[ "$ahead" = 0 ] && echo "SYNC: pushed, mirror == origin/main" || { echo "SYNC: still ahead=$ahead"; exit 3; }
exit 0
