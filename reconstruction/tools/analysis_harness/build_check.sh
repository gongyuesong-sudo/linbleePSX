#!/usr/bin/env bash
# build_check.sh — .analysis 金 harness 可复跑门(SESSION-115 抢救入 git 的守卫)
# 1) scan2 编译+bitness+golden 三流尺寸复现  2) boot32 编译+bitness(运行态=已知回归, 不 assert 运行)
# 3) manifest.sha256 一致性(--verify 模式)
set -u
cd "$(dirname "$0")"
fail=0; note(){ echo "AHCHK: $*"; }
if [ "${1:-}" = "--verify" ]; then
  if sha256sum -c manifest.sha256 --quiet 2>/dev/null; then note "manifest 一致"; else note "manifest FAIL——源码漂移, 需评审后重钉基线"; exit 1; fi
  exit 0
fi
# scan2
rm -f scan2x boot32
if gcc -m32 -O2 -o scan2x scan2.c tramp.s 2>/tmp/ah_scan.err; then
  file scan2x | grep -q "ELF 32-bit" && note "scan2 编译 OK + i386 bitness OK" || { note "scan2 bitness FAIL"; fail=1; }
  g=$(timeout 120 ./scan2x 2>/dev/null)
  okg=$(echo "$g" | grep -cE "STREAM sb=0x0facc L=0x00ba3\(2979\)|STREAM sb=0x1015c L=0x010e7\(4327\)|STREAM sb=0x10a4c L=0x71cbf\(466111\)")
  [ "$okg" = "3" ] && note "scan2 golden 三流尺寸复现 PASS" || { note "scan2 golden FAIL n=$okg"; fail=1; }
else
  note "scan2 编译 FAIL"; cat /tmp/ah_scan.err | head -3; fail=1
fi
# boot32: 编译门(运行态已知回归: boot2.c 语义漂移致 SEGV eip=0x400004, 见 README; 不 assert 可运行)
if gcc -m32 -O0 -g -no-pie -o boot32 boot2.c runstub.s shims.s vshim.s trampolines.s 2>/tmp/ah_boot.err; then
  file boot32 | grep -q "ELF 32-bit" && note "boot32 编译 OK + i386 bitness OK(运行态=已知回归, 见 README)" || { note "boot32 bitness FAIL"; fail=1; }
else
  note "boot32 编译 FAIL"; head -3 /tmp/ah_boot.err; fail=1
fi
rm -f scan2x boot32
[ $fail -eq 0 ] && echo "AH BUILD-CHECK PASS" || echo "AH BUILD-CHECK FAIL"
[ $fail -eq 0 ]
