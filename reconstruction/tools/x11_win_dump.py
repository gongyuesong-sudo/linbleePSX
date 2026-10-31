#!/usr/bin/env python3
# x11_win_dump.py — 经 libxcb xcb_get_image 直取指定窗口 pixmap 存 PNG
# 用途: wayland 合成下 screen-grab 通道(x11grab)黑屏时的窗口级取证通道。
# 用法: x11_win_dump.py <win-id-hex> <w> <h> <out.png>
import ctypes, ctypes.util, struct, sys, zlib

def main():
    wid, w, h, out = int(sys.argv[1],0), int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
    lib = ctypes.CDLL("libxcb.so.1")
    lib.xcb_connect.restype = ctypes.c_void_p
    lib.xcb_connect.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int)]
    conn = lib.xcb_connect(None, None)
    if not conn or lib.xcb_connection_has_error(conn):
        print("xcb connect fail"); sys.exit(1)
    class Cookie(ctypes.Structure): _fields_=[("sequence",ctypes.c_uint32)]
    lib.xcb_get_image.restype = Cookie
    lib.xcb_get_image.argtypes=[ctypes.c_void_p,ctypes.c_uint8,ctypes.c_uint32,
        ctypes.c_int16,ctypes.c_int16,ctypes.c_uint16,ctypes.c_uint16,ctypes.c_uint32]
    class Reply(ctypes.Structure):  # X11 wire: type@0 depth@1 seq@2 len@4 visual@8 stride@12
        _fields_=[("response_type",ctypes.c_uint8),("depth",ctypes.c_uint8),
                  ("sequence",ctypes.c_uint16),("length",ctypes.c_uint32),
                  ("visual_type",ctypes.c_uint32),("line_stride",ctypes.c_uint16),
                  ("pad2",ctypes.c_uint8*10)]
    lib.xcb_get_image_reply.restype = ctypes.POINTER(Reply)
    lib.xcb_get_image_reply.argtypes=[ctypes.c_void_p,Cookie,ctypes.POINTER(ctypes.c_void_p)]
    ck = lib.xcb_get_image(conn, 2, wid, 0, 0, w, h, 0xFFFFFFFF)  # 2=Z_PIXMAP
    rep = lib.xcb_get_image_reply(conn, ck, None)
    if not rep: print("get_image reply NULL (BadWindow?)"); sys.exit(2)
    # xcb_get_image_data/_size 为 header-inline: 手工= reply+24 起, len= reply.length*4-24
    n = rep.contents.length*4 - 24
    data = ctypes.string_at(ctypes.cast(rep, ctypes.c_void_p).value + 24, n)
    depth = rep.contents.depth; ls = rep.contents.line_stride
    print(f"depth={depth} line_stride={ls} bytes={len(data)}")
    # BGRA/BGRX -> RGB
    bpp = 4 if depth>=24 else 3
    stride = ls*4 if ls else (w*bpp+3)//4*4
    h = min(h, len(data)//stride)   # 尾行截断防御
    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filter none
        row_off = y*stride
        for x in range(w):
            o = row_off + x*bpp
            b,g,r = data[o],data[o+1],data[o+2]
            raw += bytes((r,g,b))
    def chunk(t,p):
        c=struct.pack(">I",len(p))+t+p
        return c+struct.pack(">I",zlib.crc32(t+p)&0xffffffff)
    png=b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",w,h,8,2,0,0,0))+\
        chunk(b"IDAT",zlib.compress(bytes(raw),9))+chunk(b"IEND",b"")
    open(out,"wb").write(png)
    print("wrote", out, len(png), "bytes")

if __name__=="__main__": main()
