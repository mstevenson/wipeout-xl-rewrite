"""Architectural checks complement the per-profile link and pixel regressions."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def source(path):
    text = (ROOT / path).read_text()
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)


def excludes(path, pattern):
    assert not re.search(pattern, source(path)), f"{path}: forbidden renderer dependency: {pattern}"


# Shared transport must not interpret private settings or call Modern entry points.
for path in ("platform/render/gl_backend.c", "platform/render/gl_program.c"):
    excludes(path, r"settings\s*\.\s*(ps1|pc|n64|saturn|modern)\b")
    excludes(path, r"\b(?:RenderModern\w*|RENDER_MODERN|MODERN_\w*)\b|modern_gl\.h")

# Packet/source adapters publish facts even when their consumer is inactive.
excludes("platform/psx/libgpu.c", r"\bRENDER_MODERN\b|settings\s*\.\s*modern\b")
excludes("platform/material_source.c", r"\bRenderCurrent\b|\bRenderSettings\b|\bRENDER_\w+\b")

# Capturing a primitive must not classify source assets or bind their native art.
excludes("platform/psx/framecap.c", r"\b(?:PortTagObjectPrims|PortActiveTrackNumber|SaturnNative\w*|GpuTagEmission)\b")

# Fixture names and UV masks belong to the source adapter, never a profile shader.
excludes("platform/render/modern_gl.c", r"\b(?:grid1|56942|42075)\b|174\.0|148\.0")

print("Renderer boundaries: shared transport, source metadata and recorder dependencies: OK")
