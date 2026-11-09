# PC menu model aspect in Modern

`DrawMenuModel` in `game/src/pc/pcmenus.c` always applies the existing horizontal
aspect correction when Modern draws the Windows menus. The correction preserves
model height and placement. The PC profile still uses its saved **Correct menu
model aspect** setting. Modern has no separate control. There are no layout or
representation changes.

The macOS build, game-source diagnostics and `tools/bugtest/run.sh` passed.
Scripted runs with the European Windows disc produced identical Modern captures
with the PC checkbox off and on. Before-and-after frames show the reduced model
width. Both PC checkbox settings reproduced their respective baseline captures.

The `B0` oracle snapshot was unavailable. The PlayStation oracle scans the C
files directly under `game/src` and does not cover this Windows menu source.
