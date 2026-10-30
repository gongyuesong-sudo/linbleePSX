#!/usr/bin/env python3
"""H-7a/H-7b 结构测绘：img 布局 + 槽表几何 + 172 记录区（SESSION-18）。

输入: decrypted/stream_10a4c_L71cbf_relocated.bin (加载基址 0x400000)
输出(stdout): 结构化报告，validate_host8/9.sh 的判定对象。
结论权威文档: docs/11_template_map.md
"""
import struct, collections, sys

IMG_PATH = "decrypted/stream_10a4c_L71cbf_relocated.bin"
TAB, TAB_N = 0x5AC4C, 14574          # 槽偏移表起点 / 表项数
T0 = 0x7680                          # 槽竞技场首槽地址（img 相对）
SLOT = 16                            # 槽字节长
ZONE172_SLOT0, ZONE172_N = 9552, 172 # 172 条记录区（0x2CB80..0x2D640）
FILL = {b"\x8b\x01\x01\x00"*4: "fillA", b"\x89\x01\x01\x00"*4: "fillB", bytes(16): "zero"}

def main() -> int:
    img = open(IMG_PATH, "rb").read()
    out = []
    P = out.append
    P(f"img_size=0x{len(img):x}")

    # 1) 表几何：严格等差 value(k)=T0+16k
    vals = [struct.unpack_from("<I", img, TAB + 4*k)[0] for k in range(TAB_N)]
    geom_ok = all(v == T0 + SLOT*k for k, v in enumerate(vals))
    end_ok = struct.unpack_from("<I", img, TAB + 4*TAB_N)[0] != vals[-1] + SLOT
    P(f"table_geometry=PASS" if geom_ok and end_ok else "table_geometry=FAIL")
    P(f"table 0x{TAB:x}..0x{TAB+4*TAB_N:x} n={TAB_N} stride=4 arena=0x{T0:x}..0x{T0+SLOT*TAB_N:x}")

    # 2) 槽分类区带
    def cls(k):
        s = img[T0+SLOT*k: T0+SLOT*k+SLOT]
        return FILL.get(s, "real")
    counts = collections.Counter(cls(k) for k in range(TAB_N))
    P("slots " + " ".join(f"{k}={counts[k]}" for k in ("fillA", "fillB", "zero", "real")))

    # 3) 172 记录区结构（带扫描，字段位序不固定——跨槽半记录交错）
    Z = T0 + SLOT*ZONE172_SLOT0
    words = [struct.unpack_from("<I", img, Z + 4*i)[0] for i in range(ZONE172_N*4)]
    tmpl = sorted(v for v in words if 0x40560 <= v <= 0x60000)   # 深层代码区指针
    body = sorted(v for v in words if 0x2C900 <= v <= 0x2D200)   # 竞技场自引用/相邻记录
    tags = sum(1 for v in words if v in (0x28, 0x8A))
    lens = sorted(v for v in words if 0x2A00 <= v <= 0x5600 and v not in body)
    P(f"zone172 addr=0x{Z:x}..0x{Z+SLOT*ZONE172_N:x} n={ZONE172_N}")
    P(f"zone172 deep_code_ptrs={len(tmpl)} distinct={len(set(tmpl))} body_ptrs={len(body)} tag_words={tags} len_words={len(lens)}")
    if not (len(set(tmpl)) >= 8 and len(body) >= 8 and tags >= 10):
        P("zone172_structure=FAIL"); print("\n".join(out)); return 1
    P("zone172_structure=PASS")

    # 4) 支撑库例程锚点：0x598AA 处 VRAM 回绕拷贝序言 (push ebx/esi/edi)
    anchor = img[0x598AA:0x598B0] == bytes([0x53, 0x56, 0x57, 0x66, 0x8B, 0x06])
    P("runtime_lib_anchor_0x598aa=" + ("PASS" if anchor else "FAIL"))
    # 5) 模板体指针步长小（11-23B）→ 微模板而非大函数
    ts = tmpl
    deltas = [b-a for a, b in zip(ts, ts[1:]) if 0 < b-a < 64]
    P(f"tmpl_delta_n={len(deltas)} median~{sorted(deltas)[len(deltas)//2] if deltas else 0}")
    P("RESULT=PASS" if geom_ok and anchor else "RESULT=FAIL")
    print("\n".join(out))
    return 0 if "RESULT=PASS" in out[-1] else 1

if __name__ == "__main__":
    sys.exit(main())
