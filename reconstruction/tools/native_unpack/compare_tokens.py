#!/usr/bin/env python3
# compare_tokens.py — diff gdb golden tokens vs C trace (delta-normalized)
import json, sys
gold=json.load(open('/home/gill/bleem/.analysis/tokens_gold.json'))
if gold and gold[0] and gold[0][0]=='BKFAIL':
    print('golden trace failed:', gold[0][1][:80]); sys.exit(2)
C=[]
for ln in open('/tmp/c_tokens.txt'):
    a,b,c,d=map(int,ln.split()); C.append([a,b,c,d])
print(f'gold={len(gold)} c={len(C)}')
def norm(seq, off_edi, off_esi):
    return [[x[0]-off_edi, x[1], x[2]-off_esi, x[3]] for x in seq]
if not gold or not C: sys.exit(2)
g=norm(gold, gold[0][0], gold[0][2])   # edi0, esi0
c=norm(C,    C[0][0],    C[0][2])
# esi: hw = img+0x400000 coord; c = img offset coord; after norm compare delta only
bad=None
n=min(len(g),len(c))
for i in range(n):
    gi,ci=g[i],c[i]
    # compare ebx exactly, cf exactly, edi-delta and esi-delta vs previous
    if i>0:
        gd0=g[i-1][0]-gi[0]; cd0=c[i-1][0]-ci[0]
        gd2=(g[i-1][2]-gi[2]); cd2=(c[i-1][2]-ci[2])
        if gi[1]!=ci[1] or gi[3]!=ci[3] or gd0!=cd0 or gd2!=cd2:
            bad=i; break
if bad is None and len(g)==len(c):
    print('TOKENS MATCH'); sys.exit(0)
if bad is None:
    print(f'prefix match but length differs (gold {len(g)} vs c {len(c)}) at token {n}: last gold {g[-1]} last c {c[-1]}'); sys.exit(1)
print(f'first divergent token idx={bad}')
for j in range(max(0,bad-3), min(n, bad+3)):
    mark='*' if j==bad else ' '
    print(f'{mark}g[{j}] ebx={g[j][1]:08x} cf={g[j][3]} edi_delta={g[j-1][0]-g[j][0] if j else 0} esi_delta={g[j-1][2]-g[j][2] if j else 0}')
    print(f'{mark}c[{j}] ebx={c[j][1]:08x} cf={c[j][3]} edi_delta={c[j-1][0]-c[j][0] if j else 0} esi_delta={c[j-1][2]-c[j][2] if j else 0}')
sys.exit(1)
