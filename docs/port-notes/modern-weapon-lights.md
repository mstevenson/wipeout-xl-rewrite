# Modern dynamic lights

Modern now has a separate **Dynamic lights** checkbox, enabled by default and
saved as `modern_weapon_lights` (retained for settings compatibility after the UI rename). Rockets and missiles emit orange light,
electrobolts blue and plasma violet. Additive explosions emit short, fading
flashes using their source tint. Smoke, subtractive effects, delayed launches
and the local player's plasma charge bar do not emit light. Each live ship also
emits subtle blue engine light, with a stronger light while `SHIELDED` is set.
The shield light is blue at idle and follows the shield's hit flash as it fades.
These ship lights use the same toggle and remain independent of bloom.

## Implementation

The source adapter copies up to 92 weapon, explosion and ship records at the existing
race-information hook. Positions are transformed through the camera's skeleton
chain without modifying its caches or the GTE. Explosions attached to ships
include the ship's position. The hook precedes `DrawExplode`, so the snapshot
uses the additive explosion's next lifetime (the one that function draws).
Engine positions follow `UpdateShipTrails`: 420 units behind the ship along its
nose vector, with the same -15 vertical offset. Engine radius/intensity are
1000/0.35; shield radius/intensity are 1600/1.8. Shield light is centred on the
ship and checks the active flag, not the inventory slot. Destroyed and
uninitialised ships are excluded; cockpit and off-screen ships remain eligible.
`InitShield` clears the saved tint on activation. `UpdateShield` copies the
decayed red and green channels immediately before drawing the shell or cockpit
overlay. A hit in a later weapon slot appears on both the shield and its light
on the next update. The light blends the idle blue toward the red or yellow hit
tint, using a single blue value for the shell's varying vertex blues.
Stable, distinct source IDs preserve interpolation when shields activate or
expire. The source pool holds 96 records; the shader budget remains sixteen.

`libgpu.c` owns current and previous frame snapshots, keyed by geometry frame.
It matches source slots and falling lifetimes, rejects reuse and large jumps,
and shares the geometry camera-cut guard. Every draw pass selects up to sixteen
lights by camera-relative influence, with deterministic ties. Lights behind the
camera remain eligible while their radius reaches the view. Flushes precede
changes to the draw list; stale frames clear it. Linked games compile separate
copies of this state, like their geometry and renderer state.

View-space vertex positions survive interpolation and near-plane clipping into
the batch stream. The Modern shader derives flat normals before coverage
branches. It adds diffuse light with a squared finite-radius falloff to the
original shading in linear space, modulated by the source texture. The result
returns to display encoding for the existing console blend operations and HDR
transport. Other renderer programs ignore the optional attributes and callback.
Only opaque, modulated geometry with reliable depth receives light; the sky,
HUD, shadow packets and transparent effects are excluded.

Either dynamic lights or bloom requests the existing HDR targets. With bloom
disabled, presentation skips extraction and blur but still resolves highlights
through the selected tonemapper. Unavailable HDR targets fall back to lit SDR.
Display and image capture use the same presentation path. Frame inspection
continues to report the original material; dynamic lighting is present in its
captured picture, not reproduced by its per-texel material inspector.

These lights are unshadowed: walls do not occlude their influence. There are no
new textures, normal maps, shadow maps or simulation changes.

## Game source changes and port rules

- `game/src/portlights.c`: new, host-only `PortCaptureWeaponLights`, `Light` and
  `LightView` helpers read real weapon, effect, ship and skeleton fields.
  Floating-point calculations are presentation metadata; no console data,
  pointers, counters, random state or packets are changed. The regional plasma
  charge threshold mirrors `UpdatePlasma` (200 NTSC / 140 PAL).
- `game/src/portinfo.c`: `PortInfoRaceDrawn` calls the new observer. This is the
  only changed existing function; it is already a port-only metadata hook.
- `game/include/port_platform.h`: declares the observer. No PsyQ prototypes,
  protected functions, generated data or pointer-bearing game layouts changed.
- No new raw offsets, file-layout dependencies or uninitialised console values
  were used. The native integration fixture uses the independently verified
  host `ShipData.weapon` offset only in its external test script.

## Validation

- `cmake --build build/mac -j 2`: passed, including the second-game symbol check.
- `sh tools/bugtest/run.sh`: passed. New checks cover source eligibility,
  NTSC/PAL plasma charging, ship-relative explosions, camera transforms,
  source immutability, full pools, frame ownership, slot reuse, interpolation,
  camera cuts, stale-frame reset, light budgets, clipped/interpolated surface
  coordinates, persistence, reset, UI toggling and profile isolation.
- `sh tools/bugtest/render.sh`: passed, including standalone linking of all five
  profiles. New production-shader pixel checks cover diffuse colour, finite
  radius, normals, nearest/bilinear textures, sky/HUD/shadow/raw exclusions,
  bloom independence, HDR/SDR fallback and isolation from PC rendering.
- Both Modern programs compiled and linked with `glslangValidator` as GLSL ES
  3.00. This is shader validation, not a device/browser runtime test.
- `tools/diag.py` on both changed/new game C files: clean.
- `tools/xdecl.py` reports six existing declaration spelling/typedef mismatches
  in unchanged files (`InitRaceData`, `InitSpeedo`, `UpdateRaceHud`,
  `fill_speedo`, `cdMusicTable`, `padBuf1`); no new light-related mismatch.
- `tools/oracle32.py diff B0 portinfo.c portlights.c`: blocked because
  `/tmp/oracle32/B0/snapshot.json` is absent. No console-equivalence claim.
- Fixed-step US PlayStation race runs, with scripted rockets, missiles,
  electrobolts and plasma, completed for 4,000 fields with lights on and off.
  Matched images show illumination on the road and barriers, preserving HUD
  pixels. Test scripts, saves and images are outside the repository under
  `/tmp/wipeout-weapon-lights/`.
- A further 3,600-field widescreen run with bloom off and
  `WIPEOUT_INTERPOLATE=2` completed (9,551 presented pictures); the captured
  world remained lit with interpolated presentation.
- Engine/shield extension: the macOS build and CPU bugtest suite passed again.
  Source tests additionally cover two engine headings, cockpit/off-screen ships,
  inactive inventory versus active shields, expiry/reactivation, destroyed and
  uninitialised ships, absent pools, unchanged ship state and all 92 unique sources.
  `diag.py game/src/portlights.c` remained clean; the B0 oracle comparison remains
  blocked by the missing snapshot. No retail simulation function changed.
- Three additional 4,000-field scripted US races, with bloom disabled, compared
  lights off, engines alone and an activated shield. Captures show subtle engine
  illumination, stronger blue shield spill on road/barriers, and its removal on
  expiry. Scripts, saves and images are under `/tmp/wipeout-ship-lights/`.
- Renamed the checkbox and related help/documentation to **Dynamic lights**;
  retained the saved key and internal field for compatibility. The updated
  renderer UI navigation/toggle regression passed.
- Shield flash tint: `weapon.c` adds presentation calls in `InitShield` and
  `UpdateShield`. `port_platform.h` declares `PortSetShieldFlash`, and
  `portlights.c` stores the per-ship tint and applies it in
  `PortCaptureWeaponLights`. The hooks write to port-owned metadata and use
  existing ship indices and channels. The macOS build, CPU and GPU bugtests,
  and compiler diagnostics for both game source files passed. The B0 oracle
  comparison remains blocked by the missing snapshot.
- A fixed-step US PlayStation race in Modern confirmed that a scripted wall
  collision produces a red light and fades back to idle blue. Injecting the two
  weapon flash channels after the shield draw left the light blue for that
  frame. The next update applied both channels, and subsequent updates faded
  back to the original idle RGB.

No performance guarantee or new console lockstep result is claimed. Split-screen
state separation is covered by the second-game build and existing CPU suite;
this change has not been visually exercised in a live two-player race.

## Shield hull glow

Each ordering table keeps the active shield tint alongside its owning hull
packets. The Modern shader lights the hull's unshaded texture in linear space,
with stronger illumination at grazing angles. The light uses the same idle and hit colors as the shield's
point light and follows the Dynamic lights toggle independently of bloom. It
uses the captured source tint before point light selection, so a crowded light
list does not cause the hull to flicker.

The only game source edit names the existing shield light ID base in
`PortCaptureWeaponLights` (`game/src/portlights.c`). The value, source records,
and simulation are unchanged. Compiler diagnostics were clean and the MIPS
oracle matched all three functions in the file against the pre-change revision.
The original `B0` snapshot was unavailable.
