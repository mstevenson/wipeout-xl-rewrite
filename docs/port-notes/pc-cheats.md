# PC cheats

Adds the nine typed Windows codes alongside the PlayStation button sequences, with either
front end and any supported disc assets. This is an intentional feature extension, not a
representation fix or a claim of console equivalence.

## Changes by file

- `game/src/pccheats.c`: new `ApplyPcCheat` and `PcCheatsInit`. Menu codes set the existing
  Piranha, Phantom and track flags; race codes toggle the existing weapon, damage, checkpoint
  timer and minigun flags. `PSYMEGA` requires weapons enabled. Gameplay codes require a
  single-player race, including time trials as in the PC input handlers. `RUSH` selects
  animal ships before models load. `FRAMERATE` changes the existing overlay setting.
- `game/src/main.c`, `PsxMain`: register PC cheats after the console's startup ship selection,
  before logos; close the startup context before resident models load.
- `game/src/pad.c`, `UpdateCheatSequence`: refresh the PC cheats' menu/race context each frame.
  The PlayStation sequence matching and effects are unchanged. No protected function changed.
- `platform/pc_cheats.h`: code/context constants and game/platform interface.
- `platform/controls.c`: `PcCheatsSetHandler` and `PcCheatsSetContext` bridge to SDL input;
  `PortToggleFps` toggles and saves the overlay through the existing settings API.
- `platform/psx/pad_sdl.c`: recognize words from non-repeating key presses in the player's
  layout. Keep a bounded rolling history; suppress subsequent mapped buttons within a code
  prefix (particularly the second P in `PSYPROTECT`). The first P retains Start. Reject name
  input, unfocused input, update alerts and Ctrl/Alt/GUI shortcuts. Reset on focus loss,
  name entry, non-letter input and context changes. Menu/race contexts expire after four
  video fields without refresh, following the existing name-entry context convention.
- `platform/psx/pad.h`: handler, context and reset API declarations.
- `platform/video.c`, `PortPumpEvents`: reset incomplete PC cheats when the debug UI consumes
  an event, preventing words spanning game and console input.
- `tools/bugtest/test_pc_cheats.c` and `tools/bugtest/run.sh`: exercise all nine codes through
  production SDL input and dispatch, toggling, mode restrictions, expiry, focus/name/modifier
  guards, repeat handling and the existing console sequence decoder.
- `tools/iotest/testenv.c`: supply the current input/timing layer's missing `PsxFieldCount`,
  `PortLinkRaceActive` and `portPal` fixtures so the standalone I/O suite links. The field
  counter is the same one advanced by `PsxVBlank`; these tests have no linked race.
- `README.md` and `docs/CONTROLS.md`: document the PC cheats, contexts and keyboard behavior.

No pointer representations, raw offsets, generated data or serialized layouts changed.
No new findings under port rules 7–10, unfixed offset/size dependencies or uncertain game
semantics were encountered. The PC code table and input handlers were checked against the
local Windows decompilation (`pc/src/pad.c`).

## Validation

- Native macOS RelWithDebInfo build, including the second-game isolation check: passed.
- `tools/bugtest/run.sh`: passed. After naming changes, the PC-cheats regression was compiled
  and run with AddressSanitizer and UBSan: passed.
- Headless I/O suite: all 17 CTest entries successful with a temporary USA-disc fixture.
  The SPU oracle test and extracted-file CD comparison skip without their recordings/tree;
  the libspu register-stream comparison also skips without `PSX_SPU_ORACLE`. The available
  combined BIN/CUE needed temporary track-2/3 reference files because the audio tests open
  fixed split-track filenames. No source assets or original disc images were changed.
- Native scripted USA-disc smoke: startup `RUSH`, all menu codes and all gameplay/overlay
  codes; completed 3100 video fields without a crash. The load log confirms `wierd.cmp` and
  `wierd.prm`, the animal ship assets, were selected by `RUSH`. The captured race frame was
  visually checked: it shows the animal ship and the checkpoint clock held at 9:59.
- `tools/diag.py` for `main.c`, `pad.c`, `pccheats.c`: no output.
- `tools/xdecl.py`: report unchanged from HEAD after normalizing line numbers; existing
  declaration reports remain.
- MIPS o32 comparison against a focused HEAD snapshot (`pc-cheats-B0`): only `PsxMain` and
  `UpdateCheatSequence` changed, from the explicit initialization/context calls above.
  `pccheats.c` is new and has no baseline. The stock oracle lacks `sys/types.h`; a temporary
  empty freestanding stub allowed both baseline and working-tree compilation. No oracle
  tooling or repository headers were changed to accommodate it.
- No console lockstep recording was available in this workspace, so lockstep was not run.
  `git diff --check`: clean.
