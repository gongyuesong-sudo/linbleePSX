#!/usr/bin/env bash
# validate_host46.sh — M-3 port Vulkan 呈现链双代实证（task-056，2026-10-31）
# A) X11: VK_EV drv=x11 + 真实 GPU device + swapchain imgs>=2 + selftest OK
# B) Wayland: 同判 drv=wayland
# C) 默认(无 VK_EVIDENCE)零行为变化: 不打印 VK_EV
# D) port final_check 全绿回归
set -u
cd /home/gill/bleem/port
fail=0
note(){ echo "HOST46: $*"; }
export XDG_RUNTIME_DIR=/run/user/1000
XA=$(ls "$XDG_RUNTIME_DIR"/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
[ -n "$XA" ] || { note "A) 无 Xwayland cookie"; exit 1; }
O=$(DISPLAY=:0 XAUTHORITY="$XA" VK_EVIDENCE=1 ./build/bleem_port --selftest --driver x11 2>&1)
echo "$O" | grep -q "VK_EV drv=x11 device=.\+ swapchain_imgs=[2-9]" && echo "$O" | grep -q "selftest OK (driver=x11" \
  && note "A) X11 Vulkan 呈现链实证: $(echo "$O" | grep VK_EV)" || { note "A) FAIL"; echo "$O" | tail -2; fail=1; }
O2=$(env -u DISPLAY -u XAUTHORITY WAYLAND_DISPLAY=wayland-0 VK_EVIDENCE=1 ./build/bleem_port --selftest --driver wayland 2>&1)
echo "$O2" | grep -q "VK_EV drv=wayland device=.\+ swapchain_imgs=[2-9]" && echo "$O2" | grep -q "selftest OK (driver=wayland" \
  && note "B) Wayland Vulkan 呈现链实证: $(echo "$O2" | grep VK_EV)" || { note "B) FAIL"; echo "$O2" | tail -2; fail=1; }
O3=$(DISPLAY=:0 XAUTHORITY="$XA" ./build/bleem_port --selftest --driver x11 2>&1)
echo "$O3" | grep -q "VK_EV" && { note "C) 门控失效(默认仍打印)"; fail=1; } || note "C) VK_EVIDENCE 门控正确(默认零变化)"
if DISPLAY=:0 XAUTHORITY="$XA" timeout 240 bash tools/final_check.sh >/tmp/h46fc.log 2>&1; then note "D) port final_check PASS"; else note "D) final_check FAIL"; tail -3 /tmp/h46fc.log; fail=1; fi
[ "$fail" -eq 0 ] && echo "HOST46 CHECK PASS" || echo "HOST46 CHECK FAIL"
[ "$fail" -eq 0 ]
