#!/usr/bin/env python3
# batch_mcp.py v2 — rea mcp stdio: open_binary once, batch_decompile remaining procedures
import json, subprocess, os, threading, time
BIN='/home/gill/bleem/rewrite/decrypted/stream_10a4c_L71cbf.bin'
OUT='/home/gill/bleem/rewrite/decrypted/decompiled/eng_all'
ADDRS=open('/home/gill/bleem/.analysis/addresses.txt').read().split()
env=dict(os.environ, JAVA_HOME='/home/gill/.local/share/java/temurin-21',
         GHIDRA_INSTALL_DIR='/home/gill/.local/share/ghidra')
p=subprocess.Popen(['/home/gill/.npmlocal/bin/rea-agents','mcp'],
                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                   env=env, text=True, bufsize=1)
from typing import cast, TextIO
sin=cast(TextIO, p.stdin); sout=cast(TextIO, p.stdout)
assert sin and sout, 'rea mcp stdio pipes unavailable'
lock=threading.Lock()
def send(o):
    with lock:
        sin.write(json.dumps(o)+'\n'); sin.flush()
def recv(i,to=1500):
    t0=time.time()
    while time.time()-t0<to:
        l=sout.readline()
        if not l: return None
        try: m=json.loads(l)
        except Exception: continue
        if m.get('id')==i: return m
    return None
send({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"b","version":"2"}}})
recv(1)
send({"jsonrpc":"2.0","method":"notifications/initialized"})

def good(a):
    f=f'{OUT}/eng_{a}.json'
    if not os.path.exists(f) or os.path.getsize(f)==0: return False
    try:
        j=json.load(open(f)); return bool((j.get('data',{}).get('normalized_result',{}).get('pseudocode') or '').strip())
    except Exception: return False

rem=[a for a in ADDRS if not good(a)]
print('remaining',len(rem),flush=True)
if not rem:
    print('BATCH_DONE done=0 fail=0 files=%d'%len(os.listdir(OUT))); raise SystemExit(0)

send({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"open_binary","arguments":{
    "path":BIN,"format":"raw-binary","architecture":"x86","entry_point":"0x4","provider_id":"ghidra"}}})
m=recv(2)
if not m or not m.get('result') or m['result'].get('isError'):
    print('OPEN_FAIL:',json.dumps(m)[:300] if m else 'timeout'); p.terminate(); raise SystemExit(1)
print('open_binary OK, analyzing...',flush=True)

send({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"batch_decompile","arguments":{"addresses":rem}}})
m=recv(3,to=1500)
if not m or not m.get('result') or m['result'].get('isError'):
    print('BATCH_FAIL:',json.dumps(m)[:400] if m else 'timeout'); p.terminate(); raise SystemExit(1)
txt=''
for part in m['result'].get('content',[]):
    if part.get('type')=='text': txt=part['text']; break
done=fail=0
try:
    j=json.loads(txt)
except Exception:
    print('BAD JSON head:',txt[:200]); p.terminate(); raise SystemExit(1)
# expected shapes: {'ok':True,'data':{'results':[{...per addr}]}} or list, or {'data':{'normalized_result':{'items':[...]}}}
def items_of(o):
    if isinstance(o,list): return o
    if isinstance(o,dict):
        d=o.get('data')
        if isinstance(d,list): return d
        if isinstance(d,dict):
            for key in ('results','items','functions','procedures'):
                if isinstance(d.get(key),list): return d[key]
            nr=d.get('normalized_result')
            if isinstance(nr,dict):
                for key in ('results','items','functions','procedures'):
                    if isinstance(nr.get(key),list): return nr[key]
                if nr.get('pseudocode'): return [o]
    return None
its=items_of(j)
if its is None:
    open('/home/gill/bleem/.analysis/batch_shape.json','w').write(txt[:200000])
    print('UNKNOWN shape — saved batch_shape.json head:',txt[:200]); p.terminate(); raise SystemExit(1)
# map items to addresses by order or embedded address field
for k,it in enumerate(its):
    a=None
    if isinstance(it,dict):
        s=json.dumps(it)[:400]
        for cand in rem:
            v=cand if cand.startswith('0x') else '0x'+cand
            if v in (it.get('address',''), it.get('procedure','')): a=cand; break
        if a is None and k<len(rem): a=rem[k]
    if not a: continue
    pc=''
    if isinstance(it,dict):
        pc=(((it.get('data') or {}).get('normalized_result') or {}).get('pseudocode')
            or (it.get('normalized_result') or {}).get('pseudocode') or it.get('pseudocode') or '')
    if pc and pc.strip():
        payload=it if it.get('data') else {'ok':True,'data':{'normalized_result':it if 'pseudocode' in it else {'pseudocode':pc}}}
        open(f'{OUT}/eng_{a}.json','w').write(json.dumps(payload)); done+=1
    else: fail+=1
print(f'BATCH_DONE done={done} fail={fail} files={len(os.listdir(OUT))}',flush=True)
p.terminate()
