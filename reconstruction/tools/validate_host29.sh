#!/usr/bin/env bash
# validate_host29.sh — task-037 H-17a: homebrew 组装器/EXE 封装自洽
set -u
cd "$(dirname "$0")/.."
mkdir -p host/build
python3 tools/hbrew_build.py host/build/hbrew.exe >/tmp/hb29.log 2>&1 || { echo "FAIL: 组装"; tail -5 /tmp/hb29.log; exit 1; }
echo "PASS A: 编码器 round-trip + 组装成功 ($(head -1 /tmp/hb29.log))"

python3 - <<'PY' || exit 1
import struct, sys
sys.path.insert(0, "tools")
from hbrew_build import decode, TADDR, SP0, DADDR
d = open("host/build/hbrew.exe", "rb").read()
assert d[:8] == b"PS-X EXE", "magic"
assert len(d) == 2048 + 80 + 108, len(d)
pc0, gp, ta, ts, sa, ss = struct.unpack_from("<6I", d, 0x10)
assert (pc0, ta, ts, sa, ss, gp) == (TADDR, TADDR, 108, DADDR, 80, SP0), (pc0, ta, ts, sa, ss)
data = d[2048:2048+ss]; text = d[2048+ss:]
assert data[:20] == b"HOMEBREW-SELF-0 OK\n\x00", data[:20]
words = struct.unpack(f"<{len(text)//4}I", text)
sem = [(words[i] >> 26) & 0x3F for i in range(len(words))]
assert words[0] == 0x3C10801F, hex(words[0])            # lui s0,0x801F
assert 0x26310001 in words, "addiu s1 锚"
assert 0x03207809 in words, "jal t7,t9 锚(链接=预载哨兵寄存器)"                  # 语义锚(布局无关)
assert 0xAE110000 in words and 0xAE831810 in words, "sw 锚"
assert all(decode(w)[0] != "?" for w in words)
print("PASS B: EXE 头字段/banner/关键指令语义断言")
PY
grep -qa "FUN_bfc03a18" docs/28_homebrew_e2e.md && grep -qa "0x8000b070" docs/28_homebrew_e2e.md || { echo "FAIL: 加载器链落档"; exit 1; }
echo "PASS C: BIOS 加载器链判异落档 docs/28"
echo "HOST29 CHECK PASS: 第一方 homebrew 工具链（编码器自证+EXE 封装+加载器链落档）"
