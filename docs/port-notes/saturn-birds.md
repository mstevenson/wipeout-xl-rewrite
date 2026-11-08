# Saturn birds and scenery

Valparaiso's birds were invisible because `bird.cmp` had no Saturn artwork
mapping. `platform/saturn_art.c` now loads the disc's `birds.rbh`/`birds.vri`
and maps its 16 poses into the existing animation. Flight behavior is unchanged.

The macOS build, CPU/render tests, disc artwork comparisons and sanitizer
checks passed. Race captures confirm wing animation and flock movement.

The six remaining tracks were checked in races. Fans, camera drones, smoke,
rotating adverts, radar dishes and neon animate. No further missing scenery
assets were confirmed. Valparaiso's Saturn data contains no torch placements.
Gare d'Europa's train was restored separately; see [train notes](saturn-train.md).

Video screens and Red Bull boards retain static Saturn artwork. Their playback
still needs comparison with the retail Saturn game before treating it as a
missing effect. Validation used the European disc, not the Japanese release,
and does not establish console equivalence.

To include the disc-dependent tests:

```sh
WIPEOUT_SATURN_DISC='../roms/Sega Saturn/WipEout 2097 (Europe).cue' \
    sh tools/bugtest/run.sh
```

Local captures and logs: `build/birds-validation/` and
`build/scenery-validation/` (ignored).
