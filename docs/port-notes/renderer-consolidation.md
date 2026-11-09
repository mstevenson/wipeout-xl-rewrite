# Renderer consolidation

The CPU policy and GL pipeline tables use the same profile registration list.
A committed frame owns its active settings by value. The picture owner keeps that
frame for presentation and readback. The recorder keeps a separate configuration
snapshot for its INI export. Saved keys, linked-player settings and capture formats
keep their existing layouts.

Saturn owns native quad batching, the reference rasterizer and sky rendering.
The packet adapter retains source identification, clipping and ordering, and uses
the active profile's packet policy to enable Saturn artwork. The GL backend
provides draw targets and updates their ownership and revisions. The material
classifier is shared by clipping, drawing and inspection. Sampling and coverage
are separate on the CPU and retain their existing packed shader inputs. PC, N64
and Modern share GL fullscreen setup, scratch-target allocation and picture cache
keys while each owns its pass order and output cache.

## Game-side changes

- `game/include/portfar.h` removes the separate Saturn landscape declaration.
  `game/src/draw.c` uses the common entry in `DrawLandscape` and preserves the
  scenery animation advance when the native list handles the view.
- `game/src/portfar.c` gives each profile an explicit combination of road,
  scenery and ship policies. `PortDrawNativeTrack` shares packet emission.
  `ViewRuns` traverses authored linked or contiguous spans, and the N64 topology
  walker retains its own section selection. The original and Full paths retain
  their existing bypasses.
- `PortDrawNativeLandscape` selects the scenery list and preserves each list's
  traversal order. `PortDrawNativeScenery` shares the PC and N64 skeleton
  composition with a per-relative-translation shift. Saturn keeps its native
  radius and depth policies. `PortDrawNativeShips` shares ship traversal and
  emission while retaining per-profile gates and N64 visibility history.
- Modern explicitly keeps its previous Native-mode combination: linked road spans
  without the PC F3 cap, scenery without a profile-specific cull, and the Saturn
  ship depth interval. The regression fixtures exercise that routing.

The affected game code continues to use native struct members and element sizes.
The existing float and double calculations belong to the port's render
projections and visibility gates. The change introduces no retail ABI offsets,
new serialized fields or PsyQ API changes.

## Oracle audit

The PS1-policy MIPS oracle compared `draw.c` and `portfar.c` with commit
`00dc504`. Fifteen function symbols differed. The changes were confined to
the native render dispatch and its helpers.

| Functions | Difference |
| --- | --- |
| `DrawLandscape` | Calls the unified landscape entry. |
| `PortDrawNativeLandscape`, `PortDrawSaturnNativeLandscape` | Selects authored Saturn scenery in the common entry. The old public entry is removed. |
| `PortDrawNativeTrack`, `PcNearSection` | Selects road policy before the shared emit loop. The compiler inlines the existing near-section emitter. |
| `PcViewRuns`, `SaturnViewRuns`, `ViewRuns` | Combines span traversal while preserving link order, contiguous indexing and distant-span limits. |
| `NativeN64SkeletonMatrix`, `NativePcSkeletonMatrix`, `NativeSkeletonMatrix`, `PortDrawNativeScenery` | Shares matrix composition and retains the profile's original translation scale and cull. |
| `PortDrawNativeShips`, `PortNativeShipsEnabled` | Selects the ship gate explicitly before shared emission. |
| `PortNativeTrackFaceVisible` | Reads the road policy to select the same Saturn depth test. |

## Validation

- CPU and GL regression suites passed. The CPU suite included the local European
  Saturn disc checks for bird animation and train materials. The GL suite
  included standalone profile linking, Saturn GPU/software comparison, all 64
  N64 VI settings combinations, Modern HDR and old-buffer presentation.
- The configured macOS build passed, including second-game symbol isolation and
  application packaging. Host diagnostics were clean for both edited game
  sources.
- Hidden disc smoke runs exercised the title, menus and race for all five
  profiles using the US PlayStation and European Saturn discs. The native
  runs and MIPS audit validate the refactor locally. Console lockstep requires
  retail recordings, which were unavailable here.
