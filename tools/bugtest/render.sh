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
    -Iplatform -Iplatform/psx $(pkg-config --cflags sdl3) tools/bugtest/test_n64_shader.c \
    $(pkg-config --libs sdl3) $gl -lm -o "$test_build/n64_shader"
"$test_build/n64_shader"
