#!/usr/bin/env python3
# build_index.py — generate decrypted/decompiled/index.md from eng_all/*.json
import json, glob, os, re
OUT='/home/gill/bleem/rewrite/decrypted/decompiled'
def _addr(p):
    m=re.search(r'eng_0x([0-9a-f]+)',p)
    return int(m.group(1),16) if m else -1
files=sorted(glob.glob(os.path.join(OUT,'eng_all','eng_*.json')),key=_addr)
rows=[]; total=0
for f in files:
    m=re.search(r'eng_0x([0-9a-f]+)',f)
    if not m: continue
    a='0x'+m.group(1)
    try:
        j=json.load(open(f)); d=j['data']['normalized_result']
        pc=(d.get('pseudocode') or '').strip()
        if not pc: continue
        sig=pc.split('\n')[0].strip().rstrip('{').strip()
        nm=d.get('function_name') or sig.split('(')[0].split()[-1]
        nc=pc.count('call'); ns=len(re.findall(r'0x[0-9a-f]+',pc))
        rows.append((a,nm,sig[:110],len(pc),nc,f'eng_all/{os.path.basename(f)}')); total+=1
    except Exception:
        pass
md=['# PSX 引擎体反编译索引（stream_10a4c_L71cbf.bin，base=0x0，Ghidra/REA）','',
    f'共 {total} 个函数含伪代码。地址为流内偏移（raw 加载基址 0）。','',
    '| 地址 | 函数 | 签名 | 伪代码字节 | call 数 | 文件 |','|---|---|---|---|---|---|']
for r in rows:
    md.append(f'| {r[0]} | `{r[1]}` | `{r[2].replace("|","&#124;")}` | {r[3]} | {r[4]} | [{r[5]}]({r[5]}) |')
open(os.path.join(OUT,'index.md'),'w').write('\n'.join(md)+'\n')
print('index.md functions:',total)
