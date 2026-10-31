#!/usr/bin/env python3
# 装载期金标准采样(task-045): 多时点 RA headless SAVE_STATE_TO_SLOT, 输出 RAM 切片候选
import socket, subprocess, time, os, struct, sys, glob, hashlib
RA="/home/gill/.local/bin/retroarch"; CUE="/tmp/cd2352/disc.cue"
CFG=os.path.expanduser("~/.config/retroarch/retroarch.cfg")
SDIR=os.path.expanduser("~/.config/retroarch/states/Beetle PSX")
cfg=open(CFG).read()
open(CFG,"w").write(cfg if "network_cmd_enable = TRUE" in cfg else cfg.replace("\n", "\nnetwork_cmd_enable = TRUE\n",1))
def fifo_cmds(port, delay_s, slot):
    p=subprocess.Popen([RA,"-L",os.path.expanduser("~/.config/retroarch/cores/mednafen_psx_libretro.so"),CUE],
                       stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    s=socket.socket(); ok=False
    for _ in range(120):
        try: s.connect(("127.0.0.1",port)); ok=True; break
        except Exception: time.sleep(0.5)
    if not ok: p.kill(); return False
    time.sleep(delay_s)
    try:
        s.sendall(f"SAVE_STATE_TO_SLOT {slot}\n".encode()); time.sleep(1.5)
        s.sendall(b"QUIT\n"); time.sleep(1.0)
    except Exception: pass
    s.close()
    try: p.wait(timeout=20)
    except Exception: p.kill(); p.wait()
    return True
import json
res={}
for slot,delay in [(11,2),(12,5),(13,10),(14,20),(15,40)]:
    f=os.path.join(SDIR,f"disc-slot{slot}.state")
    if os.path.exists(f): os.remove(f)
    fifo_cmds(55435,delay,slot)
    if not os.path.exists(f): res[slot]={"err":"no state"}; continue
    raw=open(f,"rb").read()
    # rzip: 找 MainRAM.data8 (与 golden_state_parse 同法)
    i=raw.find(b"MainRAM.data8")
    ram=None
    if i>=0:
        # libretro var 结构: name\0 size(u32) data
        j=i+len(b"MainRAM.data8")+1
        sz=struct.unpack_from("<I",raw,j)[0]; ram=raw[j+4:j+4+sz]
    if ram is None or len(ram)!=0x200000: res[slot]={"err":f"ram {None if ram is None else len(ram)}"}; continue
    open(f"/tmp/boot_ram_slot{slot}.bin","wb").write(ram)
    g32=lambda o: struct.unpack_from("<I",ram,o)[0]
    res[slot]={"delay":delay,"sha":hashlib.sha256(ram[0x30000:0x90000]).hexdigest()[:8],
        "w66940":hex(g32(0x66940)),"w66948":hex(g32(0x66948)),"w89e7c":hex(g32(0x89e7c)),
        "v1AC80":hex(g32(0x1AC80)),"nz_30_90":sum(1 for o in range(0x30000,0x90000,4) if g32(o)),
        "nz_90_100":sum(1 for o in range(0x90000,0x100000,4) if g32(o)),
        "v600":hex(g32(0x600)),"v674":hex(g32(0x674))}
print(json.dumps(res,indent=1))
