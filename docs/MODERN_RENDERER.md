# Modern renderer

Modern is an opt-in presentation path over the existing GPU, GTE and graphics
backends. Open the debug console (`~`), select **Graphics**, then enable
**Modern renderer**. Graphics backend selection remains independent.
Both renderers use the global resolution, frame-rate and draw-distance settings.

For an isolated hidden run:

```sh
WIPEOUT_HIDDEN=1 WIPEOUT_RENDERER_MODE=modern WIPEOUT_RENDERER=metal \
  WIPEOUT_RENDER_SCALE=1 build/mac/wipeoutxl --disc '/absolute/path/to/game.chd'
```

`WIPEOUT_RENDERER_MODE=existing` forces the baseline path.
`WIPEOUT_RENDER_PROFILE=modern` is a compatibility alias; other hardware-profile
names do not select new rendering paths. `WIPEOUT_RENDERER` still selects the
API. Hidden runs neither load nor save the player's graphics preferences.

## Controls

The Modern panel owns filtering, brightness, track lighting, dynamic lights,
bloom, tone mapper, camera blur and final resampling. Resolution, frame rate and
cap, draw distance and fade, distant engine flares, widescreen, HD packs and
downsampling use the global settings. Reset Modern changes only Modern preferences.

Defaults are bilinear world textures, 85% brightness, smooth track lighting,
dynamic lights, bloom, Soft Highlights, camera blur at 100%, and linear final
resampling. Existing rendering remains the installation default. Both renderers
use the original authored track detail and the existing extended-road path.

World filtering and brightness leave ordinary HUD/menu art unchanged. A chosen
HD texture pack is always drawn, at every scale including 1x, even while the
HD textures setting (which governs the existing renderer) is off. Classic and Soft Highlights select the two tone
curves; HDR remains internal and output is SDR. Camera blur is eligible only in
solo races with valid camera history; linking, tracing, cuts and missing history
produce a sharp picture. The panel reports unavailable HDR/post resources without
changing the requested preferences.

Settings are saved as `renderer_mode` and the `modern_*` keys listed in
[the specification](MODERN_RENDERER_SPEC.md#6-settings-and-ui-contract).
`WIPEOUT_MODERN_BLOOM=0|1` supports focused captures. Existing render-scale,
frame-rate/cap, far-view, fade and far-flare environment overrides affect the
shared settings through the existing settings loader. Old Modern-only resolution,
pacing, distance and track-detail keys are ignored and omitted when saving.

## Implementation boundaries

| Location | Ownership |
| --- | --- |
| `platform/render/modern/modern.c`, `scene.c`, `ui.cpp` | Requested and committed policy, material intent, controls |
| `platform/render/modern/metadata.c`, `lights.c` | Packet epochs, copied observations and deterministic per-view light selection |
| `platform/render/modern/post.c`, `portable_post.h` | Lazy scratch images, motion/bloom/tone resolve and revision cache |
| `platform/render/modern/source.c`, `platform/n64_material.c` | Stable fixture identities and source material addressing |
| `game/src/portmodern.c` | Track adjacency/live colour sampling, ship road tint, source and particle observations |
| Existing `libgpu`, `gte`, `gpu_renderer` | Decoding/clipping, read-only motion queries, optional submission and picture ownership |
| Existing `render/state` and devices | Portable float images, MRT, compact baseline and optional Modern draw payloads, migration |

No packet, serialized face, object, particle or save layout changes. Host sidecars
hold enhancement state. A frame boundary commits settings only after old work
flushes. Each screen band retains its picture policy and revision. The common
resolve supplies display, recorder and linked transfers; raw VRAM reads stay SDR.

Modern uses a separate vertex layout and optional uniform payload. Existing
vertices and ordinary draw uniforms carry no light arrays. There are at most
112 copied sources and 16 selected lights per view. Packet metadata is bounded
and expires on packet reuse/frame epochs. Enhanced object geometry uses the existing
per-band arena, with deterministic allocation failure instead of overwrites.

HDR scene attachments match the existing framebuffer dimensions so SDR, HDR,
masks and velocity share a depth test and console-coordinate CPU transfers.
This currently includes the full 1024x512 VRAM surface (and widescreen storage
when used); post scratch images use the picture crop. At scale S, the additional
full-VRAM HDR + mask cost is `1024*512*S*S*12` bytes, plus `*8` for velocity.
At 1x/3x/6x that is 6/54/216 MiB without velocity or 10/90/360 MiB with it,
excluding depth snapshots, crop-sized scratch, widescreen and driver overhead.
This is a deliberate simple MRT integration with a material high-scale memory
cost; a cropped attachment optimization requires preserving console-coordinate
coverage and transfer semantics. Resource limits and allocation failures are
checked. Buffered pictures retain required images; once neither current nor
buffered content needs effects, scene/depth/scratch resources are released.

## Shader maintenance

Scene GLSL is in `platform/render/modern/shaders_gl.h`. The Metal scene is
hand-written in the Modern section of `shaders_metal.metal`. Post math has one
authoritative source, `platform/render/modern/portable_post.h`.

Install development tools `glslangValidator` and `spirv-cross`, then run:

```sh
python3 tools/generate_modern_metal.py
python3 tools/generate_modern_metal.py --check
```

The check rejects generated-output drift. Neither conversion tool is a runtime
dependency. CMake builds the resulting shaders in runtime-source and packaged
`WIPEOUT_METALLIB=ON` configurations. Baseline shaders remain separate.

## Captures and validation

`frames.bin` remains XLFC version 3, readable by existing inspection tools.
An optional `modern.json` version 1 records per-frame policy and primitive
lighting/emission/motion semantics. `tools/framecap.py` accepts old captures
without that file. JPEGs contain the resolved displayed picture; the texel
inspector describes source primitives and does not reconstruct dynamic lights,
bloom or motion from the captured VRAM.

```sh
cmake --build build/mac
WIPEOUT_TEST_GL=1 WIPEOUT_TEST_METAL=1 sh tools/bugtest/run.sh
python3 tools/generate_modern_metal.py --check
```

With Emscripten on `PATH`, build the same production GPU fixture for WebGL 2:

```sh
sh tools/webtest/modern_renderer.sh
python3 -m http.server 8764 --bind 127.0.0.1 --directory build/modern-web-test
```

Open `http://127.0.0.1:8764/renderer.html`. Success sets the body's
`data-result="PASS"` and prints the actual browser GPU and fixture results.
Browser execution is required; compilation alone is not a passing runtime test.

For repeatable baseline-off timings, use `tools/bugtest/modern_benchmark.py`
with repeated `--binary LABEL=PATH` arguments, `--disc`, `--pads` and a fresh
`--output` directory. It warms each executable, rotates serial run order, and
reports medians at 1x, 3x and 6x. Its process RSS is not GPU allocation accounting.

See [the implementation and validation ledger](port-notes/modern-renderer.md)
for tested configurations, port-rule accounting and remaining acceptance work.
A native build or desktop GL metadata fixture does not establish console,
WebGL 2 or OpenGL ES equivalence.
