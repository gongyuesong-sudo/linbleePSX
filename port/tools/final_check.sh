#!/bin/bash
# final_check.sh —— task-006 集成验收链: 全功能门一次跑通
set -e
cd "$(dirname "$0")/.."
cmake -S . -B build >/dev/null
cmake --build build -j8 >/dev/null
./build/bleem_port | grep -q "full-feature policy"
./build/bleem_port --selftest --driver x11 | grep -q "selftest OK"
./build/bleem_port --selftest --driver wayland | grep -q "selftest OK"
./build/bleem_port --render-ui build/ui.ppm >/dev/null
python3 tools/check_image.py build/ui.ppm
./build/bleem_port --payload-selftest /home/gill/bleem/rewrite/data/payload.bin | grep -q "format engine PASS"
test -s build/payload_report.txt
# NATIVE-ONLY 守卫: 产物不得链接任何 wine 库
! ldd build/bleem_port | grep -iE 'wine'
echo "FINAL CHECK PASS"
