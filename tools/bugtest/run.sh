#!/bin/sh
# Small regression tests using synthetic geometry and the movie program table.
set -eu
cd "$(dirname "$0")/../.."
test_build=$(mktemp -d)
trap 'rm -rf "$test_build"' EXIT HUP INT TERM
render_sources="platform/render/renderer.c platform/render/ps1_renderer.c platform/render/pc_renderer.c platform/render/n64_renderer.c"
case $(uname -s) in Darwin) discard='-Wl,-dead_strip';; *) discard='-Wl,--gc-sections';; esac
${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -Wno-deprecated-declarations \
    -std=gnu11 -include platform/include/port.h -iquote game/include \
    tools/bugtest/test_clearance.c -o "$test_build/clearance"
"$test_build/clearance"
${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard \
    -Iplatform -Iplatform/psx -iquote game/include $(pkg-config --cflags sdl3) \
    tools/bugtest/test_movie.c $(pkg-config --libs sdl3) -o "$test_build/movie"
"$test_build/movie"

for test in guest_sky guest_animation render_settings; do
    extra_sources=
    if [ "$test" = render_settings ]; then extra_sources="$render_sources"; fi
    ${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard \
        -Iplatform -Iplatform/psx -iquote game/include $(pkg-config --cflags sdl3) \
        "tools/bugtest/test_$test.c" $extra_sources $(pkg-config --libs sdl3) -o "$test_build/$test"
    "$test_build/$test"
done

# Compare live inspection with the Python reader of real serialized captures.
${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -DPORT_VERSION='"test"' \
    -Iplatform -Iplatform/psx $(pkg-config --cflags sdl3) \
    tools/bugtest/test_framecap_render.c $render_sources $(pkg-config --libs sdl3) -lm -o "$test_build/framecap_render"
mkdir "$test_build/frames"
"$test_build/framecap_render" "$test_build/frames/frames.bin"
PYTHONDONTWRITEBYTECODE=1 python3 tools/bugtest/test_framecap_render.py "$test_build/frames"

# Imported N64 materials must not change PS1 colour arithmetic or page state.
${CC:-cc} -O1 -ffunction-sections -fdata-sections -Wno-deprecated-declarations \
    -include platform/include/port.h -iquote game/include -c game/src/port_widen.c -o "$test_build/port_widen.o"
${CC:-cc} -O1 -ffunction-sections -fdata-sections -Wno-deprecated-declarations \
    -include platform/include/port.h -iquote game/include -c game/src/port_records.c -o "$test_build/port_records.o"
${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard \
    -Iplatform -Iplatform/psx -iquote game/include $(pkg-config --cflags sdl3) \
    tools/bugtest/test_n64_material.c platform/n64_material.c "$test_build/port_widen.o" "$test_build/port_records.o" \
    $(pkg-config --libs sdl3) -o "$test_build/n64_material"
"$test_build/n64_material"
${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard \
    -Iplatform -Iplatform/psx -iquote game/include $(pkg-config --cflags sdl3) tools/bugtest/test_n64_page.c $render_sources \
    platform/psx/gte.c platform/psx/gte_tables.c \
    $(pkg-config --libs sdl3) -lm -o "$test_build/n64_page"
"$test_build/n64_page"

# Port rendering changes must preserve the original effect segments and quad geometry.
for test in electric near_object track_light; do
    gte_sources=
    if [ "$test" = near_object ] || [ "$test" = track_light ]; then
        gte_sources="platform/psx/gte.c platform/psx/gte_tables.c"
    fi
    ${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -Wno-deprecated-declarations \
        -include platform/include/port.h -iquote game/include \
        "tools/bugtest/test_$test.c" $gte_sources -lm -o "$test_build/$test"
    "$test_build/$test"
done

${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -DPORT_VERSION='"0.0.6"' \
    -Iplatform -Iplatform/psx $(pkg-config --cflags sdl3) \
    tools/bugtest/test_updates.c $render_sources $(pkg-config --libs sdl3) -o "$test_build/updates"
"$test_build/updates"
${CC:-cc} -O1 -Iplatform -Iplatform/psx $(pkg-config --cflags sdl3) \
    tools/bugtest/test_updates_web.c $(pkg-config --libs sdl3) -o "$test_build/updates_web"

"$test_build/updates_web"

# Exercise the real alert and SDL controller navigation without a GPU or network.
imgui=platform/third_party/imgui
${CXX:-c++} -std=c++17 -O1 -ffunction-sections -fdata-sections $discard -DPORT_VERSION='"0.0.7"' \
    -Iplatform -Iplatform/psx -I"$imgui" $(pkg-config --cflags sdl3) \
    tools/bugtest/test_update_navigation.cpp "$imgui/imgui.cpp" "$imgui/imgui_draw.cpp" \
    "$imgui/imgui_tables.cpp" "$imgui/imgui_widgets.cpp" "$imgui/backends/imgui_impl_sdl3.cpp" \
    $(pkg-config --libs sdl3) -o "$test_build/update_navigation"
"$test_build/update_navigation"

# Exercise the production renderer panels with real keyboard navigation.
render_objects=
for source in $render_sources; do
    object="$test_build/$(basename "$source" .c).o"
    ${CC:-cc} -O1 -c "$source" -o "$object"
    render_objects="$render_objects $object"
done
${CXX:-c++} -std=c++17 -O1 -Iplatform -I"$imgui" tools/bugtest/test_renderer_ui.cpp $render_objects \
    "$imgui/imgui.cpp" "$imgui/imgui_draw.cpp" "$imgui/imgui_tables.cpp" "$imgui/imgui_widgets.cpp" \
    -o "$test_build/renderer_ui"
"$test_build/renderer_ui"

# Run track reaction on a steep guest descent with the production integer helpers.
${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -Wno-deprecated-declarations \
    -include platform/include/port.h -iquote game/include tools/bugtest/test_guest_descent.c \
    game/src/utils.c game/data/ships_data.c platform/psx/libgte.c \
    platform/psx/gte.c platform/psx/gte_tables.c -lm -o "$test_build/guest_descent"
"$test_build/guest_descent"

# The input tests drive a real SDL virtual controller and the game's button decoder.
${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -Wno-deprecated-declarations -Wno-pointer-sign \
    -include platform/include/port.h -iquote game/include -Iplatform -Iplatform/psx $(pkg-config --cflags sdl3) \
    tools/bugtest/test_steering.c $render_sources platform/psx/pad.c game/src/pad.c $(pkg-config --libs sdl3) -o "$test_build/steering"
"$test_build/steering"
${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -Wno-deprecated-declarations -Wno-pointer-sign \
    -include platform/include/port.h -iquote game/include -Iplatform -Iplatform/psx $(pkg-config --cflags sdl3) \
    tools/bugtest/test_pc_cheats.c platform/psx/pad.c game/src/pad.c $(pkg-config --libs sdl3) -o "$test_build/pc_cheats"
"$test_build/pc_cheats"
for test in steering_target steering_ghost guest_motion guest_sign; do
    ${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -Wno-deprecated-declarations -Wno-pointer-sign \
        -include platform/include/port.h -iquote game/include "tools/bugtest/test_$test.c" -lm -o "$test_build/$test"
    "$test_build/$test"
done
${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -Wno-deprecated-declarations \
    -include platform/include/port.h -iquote game/include tools/bugtest/test_guest_1995.c \
    game/src/skeleton.c platform/psx/libgte.c platform/psx/gte.c platform/psx/gte_tables.c \
    -lm -o "$test_build/guest_1995"
"$test_build/guest_1995"
${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -iquote game/include \
    -Iplatform -Iplatform/psx $(pkg-config --cflags sdl3) tools/bugtest/test_steering_recording.c \
    $(pkg-config --libs sdl3) -o "$test_build/steering_recording"
"$test_build/steering_recording"

${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -iquote game/include \
    -Iplatform -Iplatform/psx $(pkg-config --cflags sdl3) tools/bugtest/test_link_timing.c \
    $(pkg-config --libs sdl3) -o "$test_build/link_timing"
"$test_build/link_timing"

${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -Iplatform -Iplatform/psx \
    $(pkg-config --cflags sdl3) tools/bugtest/test_link_lifecycle.c $(pkg-config --libs sdl3) \
    -o "$test_build/link_lifecycle"
"$test_build/link_lifecycle"

# Optional disc images exercise full intro decoding and compare all five frame limits
# with the constants in the disc's player executables, then test skipping playback.
if [ "$#" -gt 0 ]; then
    ${CC:-cc} -O2 -Iplatform -Iplatform/psx -iquote game/include $(pkg-config --cflags sdl3) \
        tools/bugtest/test_movie_disc.c platform/psx/disc.c $(pkg-config --libs sdl3) -lm -o "$test_build/movie_disc"
    for disc in "$@"; do
        "$test_build/movie_disc" "$disc"
    done
fi
