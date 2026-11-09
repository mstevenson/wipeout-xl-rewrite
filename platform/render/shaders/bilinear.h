#define RENDER_FILTER \
    "#define FILTER_TAPS 4\n" \
    "vec2 FilterPosition(vec2 t) { return t - 0.5; }\n" \
    "vec2 FilterHdPosition(vec2 t) { return t * vec2(hd.zw) / vec2(hdTexels.zw) - 0.5; }\n" \
    "vec2 FilterFraction(vec2 f) { return f; }\n" \
    "ivec2 FilterCorner(int i, vec2 f) { return ivec2(i & 1, i >> 1); }\n" \
    "float FilterWeight(int i, vec2 f) { vec2 w = mix(1.0 - f, f, vec2(FilterCorner(i, f))); return w.x * w.y; }\n"
