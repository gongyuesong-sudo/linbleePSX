#!/bin/bash
# validate_host3.sh — task-010：统一宿主服务回调验收
set -e
cd "$(dirname "$0")/.."
mkdir -p host/build
gcc -m32 -O0 -g -no-pie -o host/build/bleem_host host/host32.c host/tramp32.s
rm -f host/build/host_run.txt host/build/memmap.txt
timeout 120 ./host/build/bleem_host decrypted/stream_10a4c_L71cbf_relocated.bin > host/build/host_run.txt 2>&1 || true
# 1) 引擎正常返回（非信号终止）
grep -q 'H1 PASS (signal=0)' host/build/host_run.txt
# 2) 服务协议：memtable 0x330000 与 0x9400 分配真实发生
grep -q 'svc alloc 0x330000' host/build/host_run.txt
grep -q 'svc alloc 0x10000' host/build/host_run.txt
# 3) 引擎写穿 cb+0x254..0x25c 服务键（popa 污染恢复路径生效）
grep -q 'store-recover@29c' host/build/host_run.txt
# 4) memmap 窗口链完整（非零）
test -s host/build/memmap.txt
python3 - <<'PY'
toks=open('host/build/memmap.txt').read().replace('\n',' ').split()
mm=dict(t.split('=',1) for t in toks if '=' in t)
for k in ('bios_base','win4c','win50','win5c','psx_size','memtable'):
    assert mm[k].strip() not in ('00000000',''), (k, mm[k])
assert mm['psx_size'].strip()=='00800000'
print('memmap complete:', {k:mm[k].strip() for k in ('psx_size','bios_base','win5c')})
PY
echo "HOST3 CHECK PASS"
