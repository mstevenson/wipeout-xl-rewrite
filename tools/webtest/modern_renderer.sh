#!/bin/sh
# Build the native production GPU fixture for an actual WebGL 2 browser.
# Serve the output folder over HTTP, open renderer.html, and check data-result.
set -eu
cd "$(dirname "$0")/../.."
web_test_output=${1:-build/modern-web-test}
mkdir -p "$web_test_output"
emcc -O2 --use-port=sdl3 -Iplatform -Iplatform/psx -iquote game/include \
    tools/bugtest/test_modern_gpu.c platform/render/state.c platform/render/device_gl.c \
    platform/render/modern/modern.c platform/render/modern/scene.c platform/render/modern/post.c \
    platform/n64_material.c --pre-js platform/web/presentation.js \
    --shell-file tools/webtest/renderer.html -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 \
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=167772160 -sSTACK_SIZE=8388608 \
    -sASSERTIONS=1 -sEXIT_RUNTIME=1 -sMINIFY_HTML=0 -o "$web_test_output/renderer.html"
