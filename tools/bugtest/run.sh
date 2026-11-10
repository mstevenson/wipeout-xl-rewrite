#!/bin/sh
# Small regression tests using synthetic geometry and the movie program table.
set -eu
cd "$(dirname "$0")/../.."
PYTHONDONTWRITEBYTECODE=1 python3 tools/bugtest/test_render_boundaries.py
test_build=$(mktemp -d)
trap 'rm -rf "$test_build"' EXIT HUP INT TERM
${CC:-cc} -O1 -Iplatform tools/bugtest/test_saturn_native.c platform/render/saturn_native.c -lm -o "$test_build/saturn_native"
"$test_build/saturn_native"
render_sources="platform/render/renderer.c platform/render/ps1_renderer.c platform/render/modern_renderer.c platform/render/pc_renderer.c platform/render/n64_renderer.c platform/render/saturn_renderer.c platform/render/saturn_native.c"
case $(uname -s) in Darwin) discard='-Wl,-dead_strip';; *) discard='-Wl,--gc-sections';; esac
${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -Iplatform -iquote game/include $(pkg-config --cflags sdl3) \
    tools/bugtest/test_saturn_bird_art.c $(pkg-config --libs sdl3) -o "$test_build/saturn_bird_art"
"$test_build/saturn_bird_art"
# Optional local Saturn disc: verify bird animation pixels and the train/scenery assets.
if [ -n "${WIPEOUT_SATURN_DISC:-}" ]; then
    ${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -Iplatform -iquote game/include $(pkg-config --cflags sdl3) \
        tools/bugtest/test_saturn_birds_disc.c platform/saturn_art.c platform/saturn.c platform/psx/disc.c \
        $(pkg-config --libs sdl3) -o "$test_build/saturn_birds_disc"
    "$test_build/saturn_birds_disc" "$WIPEOUT_SATURN_DISC"
    ${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -Iplatform -iquote game/include \
        $(pkg-config --cflags sdl3) tools/bugtest/test_saturn_train_disc.c platform/saturn.c \
        platform/saturn_art.c platform/psx/disc.c platform/original.c platform/saturn_scene_names.c \
        platform/render/saturn_native.c $(pkg-config --libs sdl3) -lm -o "$test_build/saturn_train_disc"
    "$test_build/saturn_train_disc" "$WIPEOUT_SATURN_DISC"
fi
${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -Iplatform $(pkg-config --cflags sdl3) \
    tools/bugtest/test_saturn_model.c platform/render/saturn_native.c $(pkg-config --libs sdl3) -lm -o "$test_build/saturn_model"
"$test_build/saturn_model"
${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -Iplatform $(pkg-config --cflags sdl3) \
    tools/bugtest/test_saturn_track.c platform/render/saturn_native.c $(pkg-config --libs sdl3) -lm -o "$test_build/saturn_track"
"$test_build/saturn_track"
${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -Wno-deprecated-declarations \
    -std=gnu11 -include platform/include/port.h -iquote game/include \
    tools/bugtest/test_clearance.c -o "$test_build/clearance"
"$test_build/clearance"
${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -Wno-deprecated-declarations -Wno-pointer-sign \
    -std=gnu11 -include platform/include/port.h -iquote game/include \
    tools/bugtest/test_hud_fill.c -o "$test_build/hud_fill"
"$test_build/hud_fill"
${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard \
    -Iplatform -Iplatform/psx -iquote game/include $(pkg-config --cflags sdl3) \
    tools/bugtest/test_movie.c $(pkg-config --libs sdl3) -o "$test_build/movie"
"$test_build/movie"

${CC:-cc} -O2 -Iplatform -Iplatform/psx -iquote game/include $(pkg-config --cflags sdl3) \
    tools/bugtest/test_glyph_strokes.c platform/keyglyphs.c $(pkg-config --libs sdl3) -o "$test_build/glyph_strokes"
"$test_build/glyph_strokes"

${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -Wno-deprecated-declarations \
    -include platform/include/port.h -iquote game/include tools/bugtest/test_lobby.c \
    game/data/menus_data.c -o "$test_build/lobby"
"$test_build/lobby"

${CC:-cc} -O2 -Iplatform $(pkg-config --cflags sdl3) tools/bugtest/test_second_exit.c \
    $(pkg-config --libs sdl3) -o "$test_build/second_exit"
"$test_build/second_exit"

${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard \
    -Iplatform -Iplatform/psx -iquote game/include $(pkg-config --cflags sdl3) tools/bugtest/test_ordering_cycle.c \
    $(pkg-config --libs sdl3) -o "$test_build/ordering_cycle"
"$test_build/ordering_cycle"

${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard \
    -Iplatform -Iplatform/psx -iquote game/include $(pkg-config --cflags sdl3) tools/bugtest/test_menu_reuse.c \
    $(pkg-config --libs sdl3) -o "$test_build/menu_reuse"
"$test_build/menu_reuse"

${CC:-cc} -O2 -ffunction-sections -fdata-sections $discard \
    tools/bugtest/test_projection_cache.c -lm -o "$test_build/projection_cache"
"$test_build/projection_cache"

${CC:-cc} -O1 -Iplatform $(pkg-config --cflags sdl3) tools/bugtest/test_downsample_settings.c $render_sources \
    $(pkg-config --libs sdl3) -o "$test_build/downsample_settings"
"$test_build/downsample_settings"

${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -Wno-deprecated-declarations \
    -Iplatform -Iplatform/psx $(pkg-config --cflags sdl3) tools/bugtest/test_framecap_storage.c $render_sources \
    $(pkg-config --libs sdl3) -o "$test_build/framecap_storage"

${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard \
    -Iplatform -Iplatform/psx $(pkg-config --cflags sdl3) tools/bugtest/test_rumble_stop.c \
    $(pkg-config --libs sdl3) -o "$test_build/rumble_stop"
"$test_build/rumble_stop"
"$test_build/framecap_storage"

# Keep the portable encoder covered, plus the optional SIMD codec when installed.
${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -Wno-deprecated-declarations \
    -Iplatform -Iplatform/psx $(pkg-config --cflags sdl3) tools/bugtest/test_framecap_codec.c $render_sources \
    $(pkg-config --libs sdl3) -o "$test_build/framecap_codec"
"$test_build/framecap_codec"
if pkg-config --exists libturbojpeg; then
    for test in framecap_storage framecap_codec; do
        ${CC:-cc} -O1 -DPORT_HAVE_TURBOJPEG -ffunction-sections -fdata-sections $discard -Wno-deprecated-declarations \
            -Iplatform -Iplatform/psx $(pkg-config --cflags sdl3 libturbojpeg) "tools/bugtest/test_$test.c" $render_sources \
            $(pkg-config --libs sdl3 libturbojpeg) -o "$test_build/${test}_turbo"
        "$test_build/${test}_turbo"
    done
fi

${CC:-cc} -O1 -Iplatform $(pkg-config --cflags sdl3) tools/bugtest/test_render_switch.c \
    $(pkg-config --libs sdl3) -lm -o "$test_build/render_switch"
"$test_build/render_switch"
${CC:-cc} -O2 -D_FORTIFY_SOURCE=2 -include platform/include/port.h -iquote game/include \
    tools/bugtest/test_card_label.c -o "$test_build/card_label"
"$test_build/card_label" TESTSAVE

# Optional real GPU checks, for a desktop session with an OpenGL driver.
if [ "${WIPEOUT_TEST_GL:-0}" = 1 ]; then
    case $(uname -s) in Darwin) gl_libs='-framework OpenGL';; *) gl_libs='-lGL';; esac
    gpu_defines=''
    gpu_objects=''
    if [ "${WIPEOUT_TEST_METAL:-0}" = 1 ]; then
        python3 tools/embed_shader.py platform/render/shaders_metal.metal "$test_build/shaders_metal.h"
        ${CC:-cc} -c -fobjc-arc -Wno-deprecated-declarations -Iplatform -I"$test_build" $(pkg-config --cflags sdl3) \
            platform/render/device_metal.m -o "$test_build/metal.o"
        gpu_defines='-DPORT_HAVE_METAL'
        gpu_objects="$test_build/metal.o"
        gl_libs="$gl_libs -framework Metal -framework QuartzCore -framework AppKit"
    fi
    gpu_sources="platform/render/state.c platform/render/device_gl.c platform/render/modern_pass.c $render_sources"
    for probe in gpu_hud gpu_mesh gpu_downsample gpu_passes profile_devices; do
        ${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -Wno-deprecated-declarations \
            $gpu_defines -Iplatform -Iplatform/psx $(pkg-config --cflags sdl3) "tools/bugtest/test_$probe.c" $gpu_sources \
            $gpu_objects $(pkg-config --libs sdl3) $gl_libs -lm -o "$test_build/$probe"
        "$test_build/$probe"
    done
    if [ -n "$gpu_objects" ]; then
        env -u WIPEOUT_TEST_METAL "$test_build/profile_devices"
    fi
    # WebGL's float metadata must reproduce the desktop flat interface exactly.
    # Exercise it on a real GL driver even when the other cases selected Metal.
    env -u WIPEOUT_TEST_METAL "$test_build/gpu_passes" "$test_build/flat.bin"
    ${CC:-cc} -O1 -DPORT_TEST_WEBGL_METADATA -DPORT_TEST_WEBGL_UPLOADS -ffunction-sections -fdata-sections $discard \
        $gpu_defines -Iplatform -Iplatform/psx $(pkg-config --cflags sdl3) tools/bugtest/test_gpu_passes.c $gpu_sources \
        $gpu_objects $(pkg-config --libs sdl3) $gl_libs -lm -o "$test_build/gpu_metadata"
    env -u WIPEOUT_TEST_METAL "$test_build/gpu_metadata" "$test_build/float.bin"
    cmp "$test_build/flat.bin" "$test_build/float.bin"
    echo 'WebGL float metadata and direct indexed uploads preserve all colour/depth hashes: OK'
    env -u WIPEOUT_TEST_METAL WIPEOUT_DECODED_CACHE=0 "$test_build/gpu_metadata" "$test_build/raw.bin"
    cmp "$test_build/flat.bin" "$test_build/raw.bin"
    echo 'WebGL raw VRAM sampler preserves all cached colour/depth hashes: OK'
fi

${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -Wno-deprecated-declarations \
    -include platform/include/port.h -Iplatform -Iplatform/psx -iquote game/include $(pkg-config --cflags sdl3) \
    tools/bugtest/test_far_lifetime.c platform/psx/gte.c platform/psx/gte_tables.c \
    game/src/skeleton.c platform/psx/libgte.c game/src/utils.c $(pkg-config --libs sdl3) \
    -lm -o "$test_build/far_lifetime"
"$test_build/far_lifetime"

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
    -Iplatform -Iplatform/psx -iquote game/include $(pkg-config --cflags sdl3) tools/bugtest/test_n64_page.c platform/material_source.c $render_sources \
    platform/psx/gte.c platform/psx/gte_tables.c \
    $(pkg-config --libs sdl3) -lm -o "$test_build/n64_page"
"$test_build/n64_page"

# Port rendering changes must preserve the original effect segments and quad geometry.
for test in weapon_lights electric near_object billboard track_light native_ship_dispatch native_ship_policy native_scene_dispatch; do
    gte_sources=
    if [ "$test" = native_ship_policy ] || [ "$test" = native_scene_dispatch ]; then
        gte_sources="platform/render/n64_visibility.c platform/render/saturn_visibility.c platform/render/saturn_native.c"
    fi
    if [ "$test" = near_object ] || [ "$test" = track_light ]; then
        gte_sources="platform/psx/gte.c platform/psx/gte_tables.c"
    fi
    if [ "$test" = track_light ]; then
        gte_sources="$gte_sources game/src/portfar.c"
    fi
    if [ "$test" = billboard ]; then
        gte_sources="platform/psx/gte.c platform/psx/gte_tables.c platform/psx/libgte.c platform/render/saturn_visibility.c platform/render/saturn_native.c"
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

# The PC native road's view runs follow authored links, with a cap on the distant north list.
${CC:-cc} -std=gnu11 -O1 -ffunction-sections -fdata-sections $discard \
    -include platform/include/port.h -iquote game/include tools/bugtest/test_pc_native_view.c -lm -o "$test_build/pc_native_view"
"$test_build/pc_native_view"

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
    -Iplatform -Iplatform/psx $(pkg-config --cflags sdl3) tools/bugtest/test_link_timing.c $render_sources \
    $(pkg-config --libs sdl3) -o "$test_build/link_timing"
"$test_build/link_timing"

${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -Iplatform $(pkg-config --cflags sdl3) \
    tools/bugtest/test_saturn_viewport.c $(pkg-config --libs sdl3) -o "$test_build/saturn_viewport"
"$test_build/saturn_viewport"
${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -Iplatform \
    -include platform/include/port.h -iquote game/include $(pkg-config --cflags sdl3) \
    tools/bugtest/test_saturn_font.c game/data/ships_data.c $(pkg-config --libs sdl3) -o "$test_build/saturn_font"
"$test_build/saturn_font"
${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -Iplatform -Iplatform/psx -iquote game/include \
    $(pkg-config --cflags sdl3) tools/bugtest/test_saturn_text.c platform/psx/gte.c platform/psx/gte_tables.c \
    $(pkg-config --libs sdl3) -lm -o "$test_build/saturn_text"
"$test_build/saturn_text"
${CC:-cc} -O1 -ffunction-sections -fdata-sections $discard -Iplatform -iquote game/include $(pkg-config --cflags sdl3) \
    tools/bugtest/test_saturn_bird_art.c $(pkg-config --libs sdl3) -o "$test_build/saturn_bird_art"
"$test_build/saturn_bird_art"

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
