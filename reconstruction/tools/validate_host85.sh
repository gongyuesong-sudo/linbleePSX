#!/usr/bin/env bash
# validate_host85.sh — x86 楔2 两级 stub(task-094, 2026-10-31)
# A) 一级 stub 核心行复现: lea 0x55338(%eax),%edx + call *%edx
# B) 二段入口验真: mov $0x5,%ecx + sub $0x4010c4 修正 + call 0x55586
# C) 署名串在
# D) docs/77 锚; E) README docs=78; F) EXE 只读恒
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST85: $*"; }
E=/home/gill/bleem/demo-v1_4.EXE
dd if=$E of=/tmp/h85_stub.bin bs=1 skip=1536 count=1184 2>/dev/null
a=$(objdump -D -b binary -m i386 --adjust-vma=0x401000 /tmp/h85_stub.bin 2>/dev/null)
echo "$a" | grep -q "lea    0x55338(%eax),%edx" && echo "$a" | grep -q "call   \*%edx" && note "A) 一级 stub 交接行锚" || { note "A) FAIL"; fail=1; }
dd if=$E of=/tmp/h85_host.bin bs=1 skip=348984 count=2048 2>/dev/null
b=$(objdump -D -b binary -m i386 --adjust-vma=0x55338 /tmp/h85_host.bin 2>/dev/null | head -40)
echo "$b" | grep -q "mov    \$0x5,%ecx" && echo "$b" | grep -q "sub    \$0x4010c4,%eax" && echo "$b" | grep -q "call   0x55586" && note "B) 二段引导器验真锚" || { note "B) FAIL"; fail=1; }
grep -qa "bleem!" $E && note "C) 署名串在(0x1002 域=7 项 RVA 指针表, 非纯文本)" || { note "C) FAIL"; fail=1; }
grep -q "call 映射基址+0x55338\|0x55338" docs/77_two_stage_stub.md && grep -q "二段引导器" docs/77_two_stage_stub.md && grep -q "objdump" docs/77_two_stage_stub.md && note "D) docs/77 锚齐" || { note "D) 缺锚"; fail=1; }
dd2=$(ls docs/*.md | wc -l); if grep -q "77_two_stage_stub" README.md && [ "$dd2" -eq 82 ]; then note "E) README 与 docs=$dd2 一致"; else note "E) FAIL docs=$dd2"; fail=1; fi
s=$(sha256sum $E | cut -c1-8); [ "$s" = "b61ee13e" ] && note "F) EXE 只读恒" || { note "F) FAIL"; fail=1; }
[ $fail -eq 0 ] && echo "HOST85 CHECK PASS" || echo "HOST85 CHECK FAIL"
[ $fail -eq 0 ]
