# Firestar primitive buffers

AddressSanitizer caught `DrawObject` writing past the 44,032-byte
`PrimitiveBuffer0` while rendering the original Wipeout's Firestar in Modern.
Both Firestar classes on the US disc have 10,918 scenery primitives and
176 sky primitives. Even allowing a 52-byte Gouraud textured quad for
each, the scenery and sky use at most 576,888 bytes.

- `game/src/display.c` gives each screen a word-aligned 1 MiB packet buffer.
  `InitSystem` registers both as one GPU link region, once per game instance.
  `Swap` and `CallbackSwap` reset the write pointer to the new screen's
  buffer after handing off the previous ordering tables.
- `game/data/display_data.c` drops the superseded console buffers and their
  pointer table. The generator already omits console symbols with no
  declaration in the game sources.

`tools/diag.py` was clean for `display.c`. The B0 snapshot was unavailable,
so the MIPS oracle used pre-change HEAD (`461cbfc`). The three changes were
in `InitSystem`, `Swap`, and `CallbackSwap`, from registering the storage
and indexing it directly. No packet layout or ordering rule changed.

The same scripted Modern Venom start failed with the old AddressSanitizer
build and passed with the new one. Using the US PlayStation discs, scripted
Firestar races reached lap three in Venom and completed three laps in Rapier
on both Modern and PlayStation without sanitizer findings. The macOS build
and the CPU and GPU regression suites passed.

The PlayStation profile's native Talon's Reach trace matched pre-change
output packet for packet across all 2,042 compared frames. Retail lockstep
recordings and the data generator's split assembly inputs were unavailable.
