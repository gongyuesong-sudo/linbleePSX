#!/usr/bin/env bash
# validate_host95.sh — AH楔1 金 harness 抢救(task-104, 2026-10-31)
# A) 目录装载 37+manifest+README+build_check  B) build_check.sh --verify PASS
# C) build_check.sh 全流程 PASS(scan2 golden)  D) 版权红线: 无 captured/pkl/二进制/快照入库
# E) docs/86 锚 F) README docs=87 G) 原件 sha 与 manifest 抽 3 一致(.analysis 对照)
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST95: $*"; }
H=tools/analysis_harness
n=$(ls $H | grep -v -E "^(manifest.sha256|README.md|build_check.sh)$" | wc -l)
[ -f $H/manifest.sha256 ] && [ -f $H/README.md ] && [ -f $H/build_check.sh ] && [ "$n" -eq 34 ] && note "A) 装载 $n 源/元数据 + 三件套齐" || { note "A) FAIL n=$n"; fail=1; }
timeout 60 bash $H/build_check.sh --verify >/dev/null 2>&1 && note "B) manifest --verify PASS" || { note "B) FAIL"; fail=1; }
timeout 300 bash $H/build_check.sh > /tmp/h95_bc.log 2>&1 && grep -q "BUILD-CHECK PASS" /tmp/h95_bc.log && note "C) build_check 全流程 PASS(含 scan2 golden)" || { note "C) FAIL"; tail -3 /tmp/h95_bc.log; fail=1; }
bad=$(git status --porcelain $H 2>/dev/null | grep -cE "captured|\.pkl|snapshot|\.o$" || true)
leak=$(grep -c -E "captured|decoded\.pkl|snapshot\.json" $H/README.md $H/build_check.sh | grep -v ":0" | wc -l)
dirty=$(ls $H | grep -cE "\.pkl$|\.o$|^captured|snapshot\.json|^boot$|^boot32$|^scan2x$" || true)
[ "${dirty:-0}" = "0" ] && note "D) 红线: 目录内无 captured/pkl/二进制/快照" || { note "D) FAIL dirty=$dirty"; fail=1; }
grep -q "不可恢复" docs/86_analysis_harness_rescue.md && grep -q "不谎称" docs/86_analysis_harness_rescue.md && grep -q "2,979/4,327/466,111\|2979/4327/466111" docs/86_analysis_harness_rescue.md && note "E) docs/86 锚齐" || { note "E) 缺锚"; fail=1; }
dd=$(ls docs/*.md | wc -l); if grep -q "86_analysis_harness_rescue" README.md && [ "$dd" -eq 90 ]; then note "F) README 与 docs=$dd 一致"; else note "F) FAIL docs=$dd"; fail=1; fi
k=0; for f in scan2.c boot2.c shims.s; do a=$(sha256sum $H/$f | cut -c1-16); b=$(grep "  $f\$" $H/manifest.sha256 | cut -c1-16); [ "$a" = "$b" ] && k=$((k+1)); done
[ "$k" = "3" ] && note "G) manifest 抽 3 一致(scan2/boot2/shims)" || { note "G) FAIL k=$k"; fail=1; }
[ $fail -eq 0 ] && echo "HOST95 CHECK PASS" || echo "HOST95 CHECK FAIL"
[ $fail -eq 0 ]
