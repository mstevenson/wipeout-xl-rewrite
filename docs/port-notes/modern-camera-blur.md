# Modern camera motion blur

Modern defaults to camera motion blur in solo races. The Graphics panel and
`modern_camera_blur` setting control it. `modern_camera_blur_strength` scales
the 12 ms reference displacement and the streak limit. It defaults to 100% and
accepts 0% to 300%. The screen-space filter reconstructs moving silhouettes
from depth and velocity. It protects HUD pixels and the picture under
translucent HUD elements.

- `game/src/display.c` and `game/include/port_platform.h` publish camera mode
  changes with the upcoming tables in `Swap`. The game keeps its camera and
  table order.
- `platform/psx/gte.c` and `gte.h` expose `GteCameraShutter`. It evaluates the
  existing fitted view motion over the exposure interval and uses the existing
  camera cut detector. `platform/psx/libgpu.c` retains projections when blur is
  active and supplies the shutter and projection to `GpuCaptureCameraMotion`
  from both ordinary and interpolated draws. An explicit camera switch also
  bypasses interpolation for that picture while blur is enabled.
- `platform/render/gl_backend.c`, `gl_pipeline.h`, and `platform/psx/gpu.h`
  carry depth and camera motion with the picture revision. The depth snapshot
  survives the next HUD table's depth clear. Colour and depth remain in GPU
  memory and the presentation pass leaves the source framebuffer intact.
- `platform/render/modern_gl.c`, `shaders/material_fragment.h`, and
  `gl_program.c` protect overlays at the output pixel and at every sample.
  The auxiliary mask keeps its emission and bloom coverage channels. Its blue
  and alpha channels cover opaque, additive and subtractive HUD fragments. The
  blur runs during the colour resolve, before tone mapping and window scaling.
- `platform/render/modern_renderer.c`, `renderer.c`, `renderer.h`, and
  `renderer_ui.cpp` expose the default, saved preference, and auxiliary resource
  request.

The MIPS oracle comparison with `47fb183` found one changed function in
`display.c`. `Swap` compares and stores the previous camera update function
and passes the resulting cut flag to the existing port hook. Host diagnostics
were clean. The game changes introduce no hardcoded offsets or assumptions
about pointer size.

The macOS build and CPU and OpenGL regression suites passed. The GPU fixture
covers both display buffers, widescreen, bloom on and off, all HUD blend modes,
and preservation of the pixels adjacent to the HUD. A cut matches the
blur-disabled picture byte for byte. A Wipeout XL USA disc smoke run reached
the blur pass during an interpolated race and completed 4,600 fields. A
separate widescreen run reached the pass with interpolation disabled and
completed 4,600 fields. A scripted external-to-cockpit switch in a separate
interpolated widescreen run marked the next picture as a cut, drew the new
view directly and passed no camera motion to the blur pass.

## Hull motion and blur amount

`PortShipPackets` records every ship's hull packet range with its ordering
table. The packet adapter uses exact GTE bindings and `GteVertexShutter` to
match the same model corners from the previous game frame. The view-space
displacements survive replay, interpolation, and near-plane clipping.
Unmatched hull faces use zero displacement.

The scene shader projects the current and shutter-open view positions to
measure the hull's screen motion. The resulting vector and hull coverage are
rasterized alongside scene colour and depth. The fullscreen filter uses the
hull vector where a hull is visible and the camera reprojection on scenery.
It bypasses partially covered hull pixels to preserve silhouettes.

The Modern Graphics slider saves the amount as a percentage.
`MotionPicture` scales the capped displacement and includes the amount in its
cache key. At 0%, Modern bypasses the blur pass.

The macOS build and CPU and OpenGL regression suites passed. The GPU fixture
checks a stationary hull and a hull moving perpendicular to the camera blur
at the same depth, while preserving hull boundaries, HUD pixels, and the cut
bypass. The CPU fixture checks matched displacements on interpolated and
ordinary draws and zero displacement when the hull has no usable history. An
interpolated widescreen smoke run from the Wipeout XL USA disc exercised
matched hull corners and the velocity attachment at 200% on the previous scale.

## Silhouette and HDR reconstruction

`MotionPicture` builds a screen-space field of half-exposure motion,
reciprocal depth, and coverage. A tile reduction and a neighborhood pass
give the resolve a direction to search for moving foreground. The resolve
accepts a foreground sample when its own swept segment reaches the output
pixel. It also gathers background that a moving silhouette exposes. The
screen-space approximation sees the surfaces visible in the current frame.
The tile and directional weighting approach is described in
[A Fast and Stable Feature-Aware Motion Blur Filter](https://research.nvidia.com/publication/2013-11_fast-and-stable-feature-aware-motion-blur-filter).

`ResolvePicture` accumulates scene samples in linear HDR and applies the
existing tone mapper afterward. Bloom still uses its own emissive source.
With bloom and dynamic lights both off, only pixels touched by motion use
the HDR samples and clip to SDR after the gather. Camera cuts and the blur
opt-out bypass motion reconstruction.

The macOS build and CPU and OpenGL suites passed. The GPU fixture verifies
colour outside a moving hull's original coverage, a stationary hull in front
of moving scenery, and two points with equal SDR white but different HDR
radiance. The brighter point leaves a brighter streak with bloom disabled,
both with and without tone mapping. The fixture also checks the HUD,
widescreen, and the blur-off image on camera cuts.

Matched 4,000-field retail-disc smoke runs at 3× internal resolution and
widescreen each displayed 7,854 pictures. The fixed-step runs took 29.62 s
before the change and 33.55 s with the silhouette and HDR resolve.


## 3× GPU profile

The motion reduction now checks every pixel in fixed 4×4 blocks, then
combines block maxima into tiles sized to cover the search radius. The
resolve still checks the depth and swept coverage of individual taps and
accumulates scene colour in linear HDR. Repeated presentations blit the
cached resolve directly.

OpenGL elapsed-time queries covered the motion field through the final
colour resolve, including the enabled bloom passes. Two matched runs per
version used the Wipeout XL USA disc, Talon's Reach, widescreen at 1284×720
internal resolution, and interpolation on an Apple M4 Max. Each used
the same physical strength (150% on the previous scale, 100% on the new
scale), with 1,310 sampled resolves per version.

| GPU time per resolve | Before | After |
| --- | ---: | ---: |
| Mean | 0.89 ms | 0.71 ms |
| Median | 0.64 ms | 0.57 ms |
| 95th percentile | 2.15 ms | 1.67 ms |

In a matched 1920×1080 window capture, six of 6,220,800 RGB channel values
changed by one level. The GPU regression fixture also exercises the
silhouette trails, HDR streaks with bloom off, stationary hulls, HUD
blend modes, and camera cuts at both 1× and 3×.

## Motion resource isolation

Modern owns the reference shutter sample (8 ms, scaled by its resolve to the
12 ms reference exposure) and the solo-only motion policy. Shared packet and
GPU transport apply the same frame-resource eligibility to linked and traced
frames; they do not allocate velocity targets for those frames. Existing targets
remain available to buffered pictures, but velocity draw buffers and clears are
disabled when motion is inactive, including when HDR remains enabled. Re-enabling
motion clears and restores the velocity attachment.

Regression coverage checks profile-owned exposure, linked/traced allocation
bypass, and actual auxiliary/scene draw-buffer routing across disable and
re-enable transitions.

Validation: `tools/bugtest/run.sh`, `tools/bugtest/render.sh` (including all
five standalone profiles), and `cmake --build build/mac --parallel 2` passed.
Retail-disc smoke, console lockstep, and mobile/WebGL runs were not repeated
for this follow-up. No additional game-source changes were required.
