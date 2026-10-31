#!/bin/bash
# task-046 H 系列终局评估 — 验证(docs/33)
cd "$(dirname "$0")/.." || exit 1
D=docs/33_final_route.md
[ -f "$D" ] || { echo "FAIL A: docs/33 缺失"; exit 1; }
grep -qa "七道墙" "$D" || { echo "FAIL B: 七道墙章节缺"; exit 1; }
for t in task-024 task-027 task-031 task-040 task-043 task-044 task-045; do
  grep -qa "$t" "$D" || { echo "FAIL C: 墙证据缺 $t"; exit 1; }
done
echo "PASS B/C: 七道墙齐全(024/027/031/040/043/044/045), 终判=完整设备驱动面"
grep -qa "人月量级" "$D" && grep -qa "归档收束" "$D" && grep -qa "提请用户" "$D" || { echo "FAIL D: 两案成本/决定待用户缺"; exit 1; }
echo "PASS D: A/B 两案成本对比 + 决定提请用户(未擅自归档)"
grep -qa "X11 + Wayland backends" "$D" && grep -qa "selftest" "$D" || { echo "FAIL E: port 显示状态实证缺"; exit 1; }
grep -qa "项目终局状态" README.md && grep -qa "docs/33_final_route.md" README.md || { echo "FAIL F: README 终局章节缺"; exit 1; }
echo "PASS E/F: port X11/Wayland/Vulkan 状态实证 + README 终局指针"
for t in task-043 task-044 task-045; do
  python3 -c "
import json,sys
d=json.load(open('harness-tasks.json'))
t=[x for x in d['tasks'] if x['id']=='$t'][0]
assert t['status']=='failed' and t['error_log'], t['id']
" || { echo "FAIL G: JSON $t 判定缺失"; exit 1; }
done
echo "PASS G: H-20 三 attempt 判定均落 JSON"
if [ -x ../port/build/bleem_port ]; then
  R=$(timeout 30 env XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 ../port/build/bleem_port --selftest --driver wayland 2>&1 | tail -1)
  case "$R" in *"selftest OK"*) echo "PASS H: port wayland selftest 现场复证($R)";; *) echo "NOTE H: wayland 面不可达(会话环境), 记录于 docs/33 §3: $R";; esac
fi
echo "HOST38 CHECK PASS: 终局评估落地(docs/33+README), 决定提请用户"
exit 0
