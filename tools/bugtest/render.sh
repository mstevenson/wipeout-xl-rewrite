#!/bin/sh
# Optional GPU regression: a hidden desktop OpenGL 3.3 context, no game data.
set -eu
cd "$(dirname "$0")/../.."
test_build=$(mktemp -d)
trap 'rm -rf "$test_build"' EXIT HUP INT TERM
case $(uname -s) in
    Darwin) discard='-Wl,-dead_strip'; gl='-framework OpenGL';;
    *) discard='-Wl,--gc-sections'; gl='-lGL';;
esac
${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -Wno-deprecated-declarations \
    -Iplatform -Iplatform/psx $(pkg-config --cflags sdl3) tools/bugtest/test_n64_shader.c platform/render/renderer.c platform/render/ps1_renderer.c \
    platform/render/pc_renderer.c platform/render/n64_renderer.c \
    platform/render/gl_api.c platform/render/gl_program.c platform/render/gl_pipeline.c \
    platform/render/ps1_gl.c platform/render/pc_gl.c \
    $(pkg-config --libs sdl3) $gl -lm -o "$test_build/n64_shader"
"$test_build/n64_shader"

# No dead stripping here: each renderer must link and run without the others.
for profile in ps1 pc n64; do
    ${CC:-cc} -O1 -Wno-deprecated-declarations -Iplatform $(pkg-config --cflags sdl3) \
        -DTEST_PIPELINE="${profile}Pipeline" -DTEST_PROFILE="$profile" \
        tools/bugtest/test_renderer_isolation.c "platform/render/${profile}_gl.c" \
        platform/render/gl_api.c platform/render/gl_program.c \
        $(pkg-config --libs sdl3) $gl -lm -o "$test_build/${profile}_isolated"
    "$test_build/${profile}_isolated"
done
