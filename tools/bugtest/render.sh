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
    platform/render/modern_renderer.c platform/render/pc_renderer.c platform/render/n64_renderer.c platform/render/saturn_renderer.c platform/render/saturn_native.c \
    platform/render/gl_api.c platform/render/gl_program.c platform/render/gl_pipeline.c \
    platform/render/ps1_gl.c platform/render/modern_gl.c platform/render/pc_gl.c \
    $(pkg-config --libs sdl3) $gl -lm -o "$test_build/n64_shader"
"$test_build/n64_shader"

# No dead stripping here: each renderer must link and run without the others.
for profile in ps1 pc n64 saturn modern; do
    native=
    if [ "$profile" = saturn ]; then native=platform/render/saturn_native.c; fi
    ${CC:-cc} -O1 -Wno-deprecated-declarations -Iplatform $(pkg-config --cflags sdl3) \
        -DTEST_PIPELINE="${profile}Pipeline" -DTEST_RENDERER="${profile}Renderer" -DTEST_PROFILE="$profile" \
        tools/bugtest/test_renderer_isolation.c "platform/render/${profile}_gl.c" \
        "platform/render/${profile}_renderer.c" $native \
        platform/render/gl_api.c platform/render/gl_program.c \
        $(pkg-config --libs sdl3) $gl -lm -o "$test_build/${profile}_isolated"
    "$test_build/${profile}_isolated"
done
