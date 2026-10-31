#!/bin/bash
# task-045 v2: 装载期多时点采样; QUIT 兜底 kill (v1 卡死于 wait)
export LD_LIBRARY_PATH=$HOME/.local/lib
RA=$HOME/.local/bin/retroarch; CORE=$HOME/.config/retroarch/cores/mednafen_psx_libretro.so
SD="$HOME/.config/retroarch/states/Beetle PSX"
for D in 5 9 16 32 60; do
  rm -f "$SD/disc.state"
  $RA -L "$CORE" /tmp/cd2352/disc.cue > /dev/null 2>&1 &
  RWPID=$!
  sleep $D
  $RA --command "SAVE_STATE" > /dev/null 2>&1
  sleep 2
  $RA --command "QUIT" > /dev/null 2>&1
  for i in $(seq 1 20); do kill -0 $RWPID 2>/dev/null || break; sleep 0.5; done
  kill -9 $RWPID 2>/dev/null
  sleep 1
  if [ -f "$SD/disc.state" ]; then cp "$SD/disc.state" /tmp/bootstate_${D}.state; echo "captured t=${D}s size=$(stat -c%s /tmp/bootstate_${D}.state)"; else echo "MISS t=${D}s"; fi
done
echo DONE
