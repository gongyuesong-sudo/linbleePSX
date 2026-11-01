#!/usr/bin/env python3
"""engine_entries_scan.py — task-127 ③W5 函数入口候选扫描（docs/11 布局为纲）。

真实代码区（docs/11 §一 实测布局）：
  A = 0x4..0x22A4      入口驱动桩 + 桩后杂项例程
  B = 0x40560..0x5AC4C 宿主支撑库（真 x86 例程）
三源候选：
  1) 区内函数序言特征扫描（pushad / push ebp;mov ebp,esp / push regs 组 / jmp thunk）
  2) 区内 `call rel32`(E8) 目标收集（重定位无关，最可信的入口证据）
  3) 172 记录区(0x2CB80..0x2D640)深码指针（docs/11 §三，img 绝对址 0x400000+v 归一）
  4) 存档 154 地址并入符号表（已在账的不重复反编译，只做符号锚）
输出 decrypted/decompiled/engine_entries.json：{entries:[{addr,src}], stats:{...}}
"""
import json, re, glob, os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMG = os.path.join(ROOT, "decrypted", "stream_10a4c_L71cbf.bin")
OUT = os.path.join(ROOT, "decrypted", "decompiled", "engine_entries.json")
BASE = 0x400000

img = open(IMG, "rb").read()
n = len(img)
REGIONS = [(0x4, 0x22A4, "A"), (0x40560, 0x5AC4C, "B")]

def in_code(a):
    return any(lo <= a < hi for lo, hi, _ in REGIONS)

def region_of(a):
    for lo, hi, name in REGIONS:
        if lo <= a < hi:
            return name
    return None

entries = {}   # addr -> set(src)
def add(a, src):
    if isinstance(a, int) and 0 < a < n:
        entries.setdefault(a, set()).add(src)

# --- 源4: 存档 154 ---
for f in glob.glob(os.path.join(ROOT, "decrypted/decompiled/eng*.json")) + \
         glob.glob(os.path.join(ROOT, "decrypted/decompiled/eng_all/eng_*.json")):
    m = re.search(r"eng_(0x[0-9a-f]+)\.json$", f)
    if not m:
        continue
    try:
        meta = json.load(open(f)).get("meta", {}) or {}
    except Exception:
        meta = {}
    if str(meta.get("session", "")).startswith("SESSION-138"):
        continue  # 本楔自产存档不作 legacy 输入(防生成器吃自身输出)
    add(int(m.group(1), 16), "archived")

# --- 源1: 序言特征 ---
PROLOGUES = [
    (re.compile(rb"\x60\x8b\xb0", re.S), 0),          # pushad; mov esi,[eax+...] (docs/06 §流头序言族)
    (re.compile(rb"\x55\x8b\xec"), 0),                 # push ebp; mov ebp,esp
    (re.compile(rb"\x55\x89\xe5"), 0),                 # push ebp; mov ebp,esp (AT&T)
    (re.compile(rb"\x53\x56\x57"), 0),                 # push ebx;esi;edi
    (re.compile(rb"\x56\x53\x57"), 0),
    (re.compile(rb"\x57\x53\x56"), 0),
    (re.compile(rb"\x55\x54\x56\x57"), 0),
    (re.compile(rb"\x55\x54\x53\x56\x57"), 0),
    (re.compile(rb"\xb8[\x00-\xff]{4}\xc3", re.S), 0), # mov eax,imm; ret (小包装)
]
for lo, hi, tag in REGIONS:
    blob = img[lo:hi]
    for pat, off in PROLOGUES:
        for m in pat.finditer(blob):
            a = lo + m.start() + off
            add(a, f"prologue-{tag}")

# --- 源2: call rel32 目标（从两个代码区出发）---
for lo, hi, tag in REGIONS:
    i = lo
    while i < hi - 5:
        if img[i] == 0xE8:
            rel = struct.unpack_from("<i", img, i + 1)[0]
            t = i + 5 + rel
            if in_code(t):
                add(t, f"callrel-{tag}")
        i += 1

# --- 源3: 172 记录区深码指针 ---
Z0, Z1 = 0x2CB80, 0x2D640
for i in range(Z0, Z1 - 3, 4):
    v = struct.unpack_from("<I", img, i)[0]
    # 判据与 analyze_jit_tables.py 对齐：裸流偏移 0x40560..0x60000；兼容 0x400000+v 绝对址
    if 0x40560 <= v <= 0x60000:
        add(v, "zone172-deepptr")
    elif BASE < v < BASE + n and region_of(v - BASE):
        add(v - BASE, "zone172-deepptr")

# --- 过滤: 非存档地址必须在代码区或被指针引用; 数据槽对齐噪声剔除(至少要求落在区或archived) ---
final = {}
for a, srcs in sorted(entries.items()):
    if "archived" in srcs or in_code(a):
        final[a] = sorted(srcs)

stats = {
    "img_size": n,
    "archived": sum(1 for s in final.values() if "archived" in s),
    "new_A": sum(1 for a, s in final.items() if "archived" not in s and region_of(a) == "A"),
    "new_B": sum(1 for a, s in final.items() if "archived" not in s and region_of(a) == "B"),
    "new_outregion": sum(1 for a, s in final.items() if "archived" not in s and not in_code(a)),
    "zone172_hits": sum(1 for s in final.values() if "zone172-deepptr" in s),
    "total": len(final),
}
out = {"entries": [{"addr": hex(a), "src": s} for a, s in sorted(final.items())], "stats": stats}
open(OUT, "w").write(json.dumps(out, indent=1))
print(json.dumps(stats))
print(f"wrote {OUT}")
