# Saturn Gare d'Europa train

The train was invisible because Saturn `train.prm`/`train.cmp` used empty
fallback assets. `platform/saturn_models.c` now converts the carriage embedded
in `art\track02\course02.rbh`, with textures from `course02.vri`.

The mapping preserves the light-primitive layout and reserves texture space
so loading the train cannot overwrite scenery. It uses Saturn artwork with
the existing six-car PlayStation movement; Saturn train behavior and native
light animation have not been reimplemented.

The macOS build, CPU/render tests, disc comparisons and sanitizer checks
passed. Race captures and motion traces confirm all six carriages move,
maintain spacing and reverse direction between laps. Validation used the
European Saturn disc and does not establish console equivalence.

Local captures and logs: `build/train-validation/` (ignored).
