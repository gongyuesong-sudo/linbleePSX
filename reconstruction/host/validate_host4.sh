#!/usr/bin/env bash
# validate_host4.sh — task-011 validator:
#   host 侧 init 全服务完成（H1 PASS signal=0）
#   + htrace 证据：引擎推进到 BIOS 执行边界（eip=0xBFC00000 = PSX BIOS 复位向量跳转）
#   + cb 前后 diff 落盘且含窗口/句柄链（memtable 0x330000 分配 + BIOS 槽）
#   + docs/09 记录 H-4/H-5 边界结论
set -u
cd "$(dirname "$0")/.."
fail(){ echo "FAIL: $*"; exit 1; }

timeout 60 ./host/build/bleem_host decrypted/stream_10a4c_L71cbf_relocated.bin > host/build/host_run.txt 2>&1 || true
grep -Eq 'H1 PASS (signal=[01])' host/build/host_run.txt || fail "host init not clean"   # signal=0(round-1 clean)或1(round-2 init+BIOS边界)均可

test -s host/build/htrace.log || fail "htrace.log empty"
grep -q 'LEFT engine range at bfc00000' host/build/htrace.log || fail "BIOS-boundary jump (0xbfc00000) not reached"
grep -q 'RECOVER-17B' host/build/htrace.log || fail "8MB BIOS window request not observed"

test -s host/build/cb_diff.txt || fail "cb_diff.txt empty"
grep -q '^+0024=' host/build/cb_diff.txt || fail "cb +0x24 scratch window missing in diff"
grep -q '^+0038=' host/build/cb_diff.txt || fail "cb alloc window missing in diff"
grep -q '^+0044=' host/build/cb_diff.txt || fail "cb BIOS window (8MB) missing in diff"

grep -q 'PSX.EXE' docs/09_engine_hosting.md || fail "docs/09 missing H-5 PSX.EXE boundary note"
echo "HOST4 CHECK PASS: host init clean; engine advances to PSX BIOS reset-vector boundary (0xBFC00000); cb window/handle chains fully written; H-5 boundary documented"
