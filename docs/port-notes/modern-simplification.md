# Modern renderer simplification

Date: 2026-11-12. Branch: `modern-renderer`, on `e4ebce8`.
Removes code that the settings cleanup left without effect. Rendering, gameplay
and the disabled path's per-primitive work are intended to be unchanged.

## Removed or collapsed

| Area | Change |
| --- | --- |
| `platform/render/modern/modern.h`, `scene.c`, `geometry.h` | `RenderGeometry.lightFormat/worldBillboards/originalSceneryColours`, `RenderResources.hdTexturesAtNativeScale/hdTexturesAlways/motionShutterSeconds` and `RenderPacketPolicy` were constants of the selected renderer. Call sites use `ModernEnabled()`, the committed `smoothTrackLighting` setting or `MODERN_SHUTTER_SECONDS`. The shutter is read only when camera blur is on, which requires Modern. Unread `RenderMaterial.fullColour/perspective`, `RenderCoverage`, `RENDER_THREE_POINT` and `SHADE_SNAP` are gone; `RENDER_BILINEAR` keeps its packed value 2. |
| `shaders_gl.h`, `shaders_metal.metal` (hand-written Modern scene only) | Half/mesh coverage, three-point filtering, vertex pixel snapping, their `vPixel` varying and the `effect[0].yz` grid were unreachable. The post fragments are unchanged and `generate_modern_metal.py --check` passes. |
| `render/state.[ch]`, `psx/gpu_renderer.c` | `RgGameGrid` (always 1, 1) and the coverage bits in the vertex mode byte removed. `PixelX/PixelY/SpanX/SpanY` restored to the baseline integer `* scale` expressions; this also restores baseline double-precision rounding for the wide picture's left edge in `Present`. One placement helper serves both vertex loops; `DecodedLayer` reads texture fields directly; batching and command coalescing share one light comparison; redundant Modern-only checks inside the `frame` branch dropped. |
| `render/modern/metadata.c`, `modern.c`, `source.c` | Shared annotation lookup guard; one changed-policy test. `PortTagObjectPrims` loses its ignored scratch-word argument and unused includes. |
| `psx/libgpu.c`, `device_gl.c`, `gpu.h` | No-op `n64` clear in `StateFor`; formatting noise and a stale file name in a comment restored. |

## Game-source audit

| File | Functions and category |
| --- | --- |
| `game/src/objdraw.c` | `GouraudColours`: scenery-colour retention tests `ModernEnabled()` directly. `ObjectLight`: drops the always-true light-format argument. `DrawObject`: billboards follow `modernArena`; the road-light policy is queried only on the Modern ship path instead of for every object; `PortFarAlloc` parentheses restored. `DrawClippedPrimitive`, `DrawObject`: `PortTagObjectPrims` without the scratch word. |

No representation, protected function, serialized layout, absolute address or
rule 7–10 item changed. `tools/diag.py game/src/objdraw.c` is clean.
`tools/oracle32.py diff modern-simplify-base objdraw.c` (snapshot of `e4ebce8`)
reports `DrawClippedPrimitive` and `DrawObject` changed and `GouraudTextured`
removed: without the out-of-line geometry query the compiler inlines
`GouraudTextured` into `DrawClippedPrimitive`, and `DrawObject` loses the query
and the extra argument. Both are presentation call sites already listed in the
original ledger.

## Validation

- `cmake --build build/local` (macOS, Unix Makefiles, RelWithDebInfo): passed,
  including the second-game build and app bundle. Packaged metallib is off in this
  tree; Metal shaders were compiled at runtime by the tests below.
- `WIPEOUT_TEST_GL=1 WIPEOUT_TEST_METAL=1 sh tools/bugtest/run.sh`: passed on
  Apple M4 Max, including the 512-case GPU fixtures, Modern HDR/bloom/motion/HUD/
  widescreen and in-flight backend switching on real OpenGL and Metal, and the
  WebGL-metadata shader variant compiled natively.
- `python3 tools/generate_modern_metal.py --check`: passed.
- `tools/asmtest/build.sh` builds with the changed stub; the retail packet
  comparison was not run (it needs the extracted executable and symbols).
- Not run: browser WebGL 2 fixture (Emscripten unavailable), benchmarks, disc race
  captures, mobile hardware. This is not a console-equivalence claim.
