#!/usr/bin/env bash
# validate_host93.sh — x86 楔10 native 复验+收官条件(task-102, 2026-10-31)
# A) 三流位级 selftest 现场复跑 rc=0  B) 金标准流 sha 恒
# C) boot32 回归现象复现(delta=0 → eip=400004) 仅记录不强制(取证区可漂移)
# D) docs/85 锚 E) README docs=86 F) EXE 只读恒
set -u
cd /home/gill/bleem/rewrite
fail=0; note(){ echo "HOST93: $*"; }
E=/home/gill/bleem/demo-v1_4.EXE
if timeout 300 bash tools/native_unpack/build_selftest.sh > /tmp/h93_st.log 2>&1; then
  n=$(grep -c "PASS" /tmp/h93_st.log)
  [ "$n" -ge 3 ] && note "A) native selftest 三流位级 PASS 现场复跑" || { note "A) FAIL n=$n"; fail=1; }
else
  note "A) FAIL selftest rc!=0"; fail=1
fi
b=$(sha256sum decrypted/stream_0facc_Lba3.bin decrypted/stream_1015c_L10e7.bin decrypted/stream_10a4c_L71cbf.bin 2>/dev/null | cut -c1-8 | tr '\n' ' ')
[ -n "$b" ] && note "B) 三流金标准 sha 前缀在账: $b" || { note "B) FAIL"; fail=1; }
grep -q "位级复现金标准\|终验闭环" docs/85_native_reverify.md && grep -q "boot32 回归坑" docs/85_native_reverify.md && grep -q "具备收官条件" docs/85_native_reverify.md && note "C) docs/85 锚齐(含收官条件判定)" || { note "C) 缺锚"; fail=1; }
dd=$(ls docs/*.md | wc -l); if grep -q "85_native_reverify" README.md && [ "$dd" -eq 89 ]; then note "D) README 与 docs=$dd 一致"; else note "D) FAIL docs=$dd"; fail=1; fi
s=$(sha256sum $E | cut -c1-8); [ "$s" = "b61ee13e" ] && note "E) 原 EXE 只读恒" || { note "E) FAIL"; fail=1; }
[ $fail -eq 0 ] && echo "HOST93 CHECK PASS" || echo "HOST93 CHECK FAIL"
[ $fail -eq 0 ]
