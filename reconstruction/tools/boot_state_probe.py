#!/usr/bin/env python3
# task-045 v3: RA1.22 RZIP——顺序分块 [3*u32 sizes][name\0][zlib data]; RAM 标记普查+装载态切片导出(.analysis)
import struct, sys, os, zlib, hashlib, json
OUT="/home/gill/bleem/.analysis/golden"
def ram_of(state_path):
    d=open(state_path,"rb").read()
    pos=0; out=bytearray()
    while True:                                   # golden_state_parse.inflate_all 同法: 全流拼接
        j=d.find(b"\x78\x9c",pos)
        if j<0: break
        try:
            o=zlib.decompressobj(); raw=o.decompress(d[j:]); out+=raw
            pos=j+max(len(d[j:])-len(o.unused_data),1)
        except Exception: pos=j+1
    out=bytes(out)
    i=out.find(b"MainRAM.data8")
    if i<0: return None
    p=out.find(b"\x00\x00\x20\x00",i,i+64)
    if p<0: return None
    ram=out[p+4:p+4+0x200000]
    return ram if len(ram)==0x200000 else None
res={}
for f in sorted(sys.argv[1:]):
    ram=ram_of(f)
    if ram is None: res[os.path.basename(f)]={"err":"parse"}; continue
    ram_b: bytes=ram
    g=lambda o: struct.unpack_from("<I",ram_b,o)[0]
    tag=os.path.basename(f).replace(".state","")
    slice_=ram_b[0x30000:0x90000]
    open(os.path.join(OUT,f"boot_c2_slice_{tag}.bin"),"wb").write(slice_)
    res[tag]={"sha_slice":hashlib.sha256(slice_).hexdigest()[:8],
        "w66940":hex(g(0x66940)),"w66948":hex(g(0x66948)),"w89e7c":hex(g(0x89e7c)),
        "w1AC80":hex(g(0x1AC80)),"w600":hex(g(0x600)),"w674":hex(g(0x674)),
        "nz_c2":sum(1 for o in range(0x30000,0x90000,4) if g(o)),
        "nz_90_b0":sum(1 for o in range(0x90000,0xb0000,4) if g(o))}
print(json.dumps(res,indent=1))
