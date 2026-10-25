#!/usr/bin/env python3
# validate_decomp.py — task-002 validation: count non-empty decompiled engine fns
import json, glob, os, sys
d='/home/gill/bleem/rewrite/decrypted/decompiled/eng_all'
files=glob.glob(os.path.join(d,'eng_*.json'))
ok=0; empty=[]
for f in files:
    try:
        j=json.load(open(f))
        pc=j['data']['normalized_result'].get('pseudocode') or ''
        if len(pc.strip())>0: ok+=1
        else: empty.append(os.path.basename(f))
    except Exception:
        empty.append(os.path.basename(f))
print(f'decompile files={len(files)} with_pseudocode={ok}')
if empty: print('missing/empty:', empty[:10])
idx='/home/gill/bleem/rewrite/decrypted/decompiled/index.md'
if os.path.exists(idx) and ok>=150:
    print('index.md present'); sys.exit(0)
if ok>=150 and not os.path.exists(idx):
    print('FAIL: need index.md'); sys.exit(1)
sys.exit(0 if ok>=150 else 1)
