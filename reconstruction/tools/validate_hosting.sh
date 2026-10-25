#!/bin/bash
# validate_hosting.sh — task-005 验证：宿主设计文档结构与证据引用
set -e
cd "$(dirname "$0")/.."
D=docs/09_engine_hosting.md
test -s $D
grep -q 'API shim' $D
grep -q 'DirectDraw → Vulkan\|DirectDraw->Vulkan\|DDRAW' $D
grep -q 'DirectInput' $D
grep -q '分阶段路线' $D
rows=$(grep -c '^| ' $D); [ "$rows" -ge 25 ] || { echo "FAIL table rows $rows < 25"; exit 1; }
grep -q '0x4022a4' $D
grep -q 'relocated' $D
grep -q '0xa0000' $D
# referenced artifacts actually exist
test -s decrypted/stream_10a4c_L71cbf_relocated.bin
test -s decrypted/reloc_report.json
test -s docs/08_api_dispatch.md
test -s tools/native_unpack/unpack_lib.c
echo "HOSTING CHECK PASS (rows=$rows)"
