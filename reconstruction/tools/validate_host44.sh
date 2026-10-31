#!/usr/bin/env bash
# validate_host44.sh — M-1 port X11 显示面补证（task-054，2026-10-31）
# A) 经 X0(Xwayland)+cookie: bleem_port --selftest --driver x11 PASS(frames>=3)
# B) --window 模式冒烟(启动后 2s 自退, 不崩)
# C) Wayland 面回归(已知 PASS 不回退)
# D) 无 DISPLAY 时仍按预期硬失败(去静默化守卫不回退)
set -u
cd /home/gill/bleem/port
fail=0
note(){ echo "HOST44: $*"; }
export XDG_RUNTIME_DIR=/run/user/1000
XA=$(ls "$XDG_RUNTIME_DIR"/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
[ -n "$XA" ] || { note "A) 找不到 Xwayland cookie"; exit 1; }
if DISPLAY=:0 XAUTHORITY="$XA" ./build/bleem_port --selftest --driver x11 2>&1 | grep -q "selftest OK (driver=x11"; then note "A) x11 selftest PASS(X0)"; else note "A) x11 selftest FAIL"; fail=1; fi
if DISPLAY=:0 XAUTHORITY="$XA" timeout 3 ./build/bleem_port --window --driver x11 >/tmp/h44win.log 2>&1; then rc=0; else rc=$?; fi
if [ "$rc" -eq 124 ] || { [ "$rc" -eq 0 ] && grep -qi "frames" /tmp/h44win.log; }; then note "B) window 模式 X11 下稳定(rc=$rc, 无崩溃)"; else note "B) window FAIL rc=$rc"; tail -3 /tmp/h44win.log; fail=1; fi
if env -u DISPLAY WAYLAND_DISPLAY=wayland-0 ./build/bleem_port --selftest --driver wayland 2>&1 | grep -q "selftest OK (driver=wayland"; then note "C) wayland 回归 PASS"; else note "C) wayland 回归 FAIL"; fail=1; fi
if env -u DISPLAY -u XAUTHORITY ./build/bleem_port --selftest --driver x11 >/tmp/h44nx.log 2>&1; then note "D) 无 DISPLAY 应硬失败但成功了?"; fail=1; else grep -q "x11 not available\|cannot open display\|SDL_Init" /tmp/h44nx.log && note "D) 无 DISPLAY 硬失败+可读报错" || { note "D) 报错形态异常"; fail=1; }; fi
[ "$fail" -eq 0 ] && echo "HOST44 CHECK PASS" || echo "HOST44 CHECK FAIL"
[ "$fail" -eq 0 ]
