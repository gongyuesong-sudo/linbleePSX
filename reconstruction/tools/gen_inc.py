#!/usr/bin/env python3
"""从 host/mips_jit.c 生成 host/mips_jit_inc.h（剥离 #ifdef JIT_MAIN 块）。
每次修改 mips_jit.c 后必须运行: python3 tools/gen_inc.py
（防 inc.h/.c 漂移——历史上 jit_lib_flush_blocks/jit_dbg_lastblock 手动 append 曾掩盖同步 bug）"""
import re, sys
src = open('host/mips_jit.c').read()
out, skip = [], 0
for line in src.split('\n'):
    if re.match(r'#if(def)?\s+JIT_MAIN', line):
        skip += 1; continue
    if skip and re.match(r'#endif', line):
        skip -= 1; continue
    if not skip:
        out.append(line)
body = '\n'.join(out)
extra = []
for sym, decl in (('jit_lib_gpuw_probe', 'uint32_t jit_lib_gpuw_probe(void){ return g_gpuw; }'),
                  ('jit_dbg_lastblock', 'void* jit_dbg_lastblock(void){ uint32_t h=(psx_state.pc>>2)&(CSLOTS-1); return cc[h].code; }')):
    if ('%s(' % sym) not in body:
        extra.append(decl)
with open('host/mips_jit_inc.h', 'w') as f:
    f.write(body + '\n' + '\n'.join(extra) + '\n')
print("inc.h regenerated from mips_jit.c (%d lines, %d compat-shims)" % (len(out), len(extra)))
