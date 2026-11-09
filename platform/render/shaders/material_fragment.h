/* Shared asset decoding, coverage and blend transport. No renderer selection here. */
    "#ifdef GL_ES\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "precision highp usampler2D;\n"
    "#endif\n"
#ifdef RENDER_FRAGMENT_UNIFORMS
    RENDER_FRAGMENT_UNIFORMS
#endif
    "in vec4 vColour;\n"
    "in float vColourWeight;\n"
    "in vec3 vPixel;\n"
    "in vec3 vTexel;\n"
    "flat in int semiMode;\n"
    "flat in ivec4 filterBounds;\n"
    "flat in int filtered;\n"
    "flat in int addressFlags;\n"
    "flat in int ps1Light;\n"
    "uniform usampler2D vram;\n"
    "uniform sampler2D glyphs;\n"
    "uniform sampler2D portPages;\n"
    "uniform sampler2D hdAtlas;\n"
    "flat in uvec4 hd;\n"
    "flat in ivec4 hdTexels;\n"
    "flat in ivec4 texturePage;\n"
    "flat in ivec4 clut;\n"
    "flat in ivec4 vWindow;\n"
    "flat in ivec4 mode;\n"
#ifdef RENDER_AUX_COLOUR
    "layout(location = 0) out vec4 fragment;\n"
    RENDER_AUX_COLOUR
#else
    "out vec4 fragment;\n"
#endif
    /* texels and palettes from either half of video memory (rows to 1023) */
    "uint Fetch(ivec2 p) { return texelFetch(vram, ivec2(p.x & 1023, p.y & 1023), 0).r; }\n"
    "uint FetchClut(ivec2 p) { return texelFetch(vram, ivec2(p.x & 1023, p.y & 1023), 0).r; }\n"
    "vec3 VertexColour() {\n"
    "    vec3 c = vColour.rgb / vColourWeight;\n"
#ifdef RENDER_VERTEX_COLOUR
    RENDER_VERTEX_COLOUR
#elif defined(RENDER_RGB5_VERTEX_COLOUR)
    "    return (mode.y & 4) != 0 ? floor(c * 255.0 + 0.0001) / 255.0 : c;\n"
#else
    "    return c;\n"
#endif
    "}\n"
#ifdef RENDER_TINT
    RENDER_TINT
#else
    /* Imported RGB8 light uses 255 as neutral. PlayStation-scale light uses 128. */
    "vec3 TextureTint() { return VertexColour() * ((mode.y & 64) != 0 && ps1Light == 0 ? 1.0 : 255.0 / 128.0); }\n"
#endif
#ifdef RENDER_OUTPUT_COLOUR
    RENDER_OUTPUT_COLOUR
#else
    "vec3 OutputColour(vec3 c) { return c; }\n"
#endif
#ifdef RENDER_LIGHTING
    RENDER_LIGHTING
#endif
    /* colour col with coverage cov (premultiplied), blended or opaque, faded with distance (vColour.a) */
    "vec4 BlendMaterial(vec3 col, float cov, bool blended) {\n"
    "    if (!blended) return vec4(col, cov);\n"
    "    if (semiMode == 0) return vec4(col * 0.5, cov * 0.5);\n"
    "    if (semiMode == 3) return vec4(col * 0.25, 0.0);\n"
    "    return vec4(col, 0.0);\n"
    "}\n"
    "void Out(vec3 col, vec3 albedo, float cov, bool blended, bool saturate) {\n"
#ifdef RENDER_LIGHTING
    "    col = LightMaterial(col, albedo, cov);\n"
#endif
#ifdef RENDER_AUX_COLOUR
    "    vec3 unclamped = col;\n"
#endif
    "    if (saturate) col = min(col, vec3(cov));\n"
    "    col = OutputColour(col);\n"
    "    float fade = vColour.a;\n"
    "    if (mode.z == 3) {\n"
    "        if (blended || fade < 0.999 || cov < 0.999) discard;\n"
    "        fragment = vec4(0.0);\n"
    "        return;\n"
    "    }\n"
    "    if (mode.z == 1 && blended) discard;\n"
    "    if (mode.z == 2 && !blended) discard;\n"
#ifdef RENDER_COVERAGE
    RENDER_COVERAGE
#endif
    "    fragment = BlendMaterial(col * fade, cov * fade, blended);\n"
#ifdef RENDER_AUX_COLOUR
    "    if (hdrActive) WriteAux(BlendMaterial(HdrMaterial(unclamped, cov) * fade, cov * fade, blended));\n"
#endif
    "}\n"
    "ivec2 WindowUV(ivec2 uv) {\n"
    "    uv = uv & 255;\n"
    "    if ((mode.y & 64) != 0) return uv;\n"
    "    uv.x = (uv.x & ~(vWindow.x * 8)) | ((vWindow.z & vWindow.x) * 8);\n"
    "    uv.y = (uv.y & ~(vWindow.y * 8)) | ((vWindow.w & vWindow.y) * 8);\n"
    "    return uv;\n"
    "}\n"
    /* Resolve palette indices before filtering; retain the packed colour's semi-transparency bit. */
    "uint NativeTexel(ivec2 uv) {\n"
    "    if (mode.w == 0) {\n"
    "        uint word = Fetch(texturePage.xy + ivec2(uv.x >> 2, uv.y));\n"
    "        uint index = (word >> uint((uv.x & 3) * 4)) & 15u;\n"
    "        return FetchClut(clut.xy + ivec2(int(index), 0));\n"
    "    } else if (mode.w == 1) {\n"
    "        uint word = Fetch(texturePage.xy + ivec2(uv.x >> 1, uv.y));\n"
    "        uint index = (word >> uint((uv.x & 1) * 8)) & 255u;\n"
    "        return FetchClut(clut.xy + ivec2(int(index), 0));\n"
    "    }\n"
    "    return Fetch(texturePage.xy + uv);\n"
    "}\n"
    /* N64 images have registered bounds; native PS1 taps retain page wrapping and texture windows. */
#include "material_address.h"
    "ivec2 MaterialUV(ivec2 p) {\n"
#ifdef RENDER_FILTER_BOUNDS
    RENDER_FILTER_BOUNDS
#endif
    "    return ((mode.y & 64) != 0 || filtered == 1) && filterBounds.z >= filterBounds.x\n"
    "        ? ivec2(N64Axis(p.x, filterBounds.x, filterBounds.z, 1), N64Axis(p.y, filterBounds.y, filterBounds.w, 2))\n"
    "        : WindowUV(p);\n"
    "}\n"
    "vec4 NativeSample(ivec2 p) {\n"
    "    uint texel = NativeTexel(MaterialUV(p));\n"
    "    if ((mode.y & 64) != 0) {\n"
    "        if ((texel & 0x7fffu) == 0u) return vec4(0.0);\n"
    "        uvec3 c = uvec3(texel, texel >> 5, texel >> 10) & 31u;\n"
    "        return vec4(vec3((c << 3) | (c >> 2)) / 255.0, 1.0);\n"
    "    }\n"
    "    if (texel == 0u) return vec4(0.0);\n"
    "    vec3 rgb = vec3(float(texel & 31u), float((texel >> 5) & 31u), float((texel >> 10) & 31u)) * (8.0 / 255.0);\n"
    "    return vec4(rgb, (texel & 0x8000u) != 0u ? 0.5 : 1.0);\n"
    "}\n"
    /* Alpha 0/0.5/1 labels transparent/semi/opaque texels; keep their PS1 blend contributions separate. */
    "int HdAxis(int p, int low, int size, int nativeSize, int mirror) {\n"
    "    if (addressFlags == 0 || (nativeSize & (nativeSize - 1)) != 0) return clamp(p, low, low + size - 1);\n"
    "    int at = p - low, period = (addressFlags & mirror) != 0 ? size * 2 : size;\n"
    "    at = ((at % period) + period) % period;\n"
    "    return low + (at >= size ? period - 1 - at : at);\n"
    "}\n"
    "vec4 HdSample(ivec2 p) {\n"
    "    if ((mode.y & 64) != 0 && filterBounds.z >= filterBounds.x) {\n"
    "        vec2 ratio = vec2(hd.zw) / vec2(hdTexels.zw);\n"
    "        ivec2 size = filterBounds.zw - filterBounds.xy + 1;\n"
    "        ivec2 low = ivec2(round(vec2(filterBounds.xy - hdTexels.xy) * ratio));\n"
    "        ivec2 span = max(ivec2(round(vec2(size) * ratio)), ivec2(1));\n"
    "        p = ivec2(HdAxis(p.x, low.x, span.x, size.x, 1), HdAxis(p.y, low.y, span.y, size.y, 2));\n"
    "        if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, ivec2(hd.zw))))\n"
    "            return NativeSample(ivec2(floor(vec2(hdTexels.xy) + (vec2(p) + 0.5) / ratio)));\n"
    "    }\n"
    "    return texelFetch(hdAtlas, ivec2(hd.xy) + clamp(p, ivec2(0), ivec2(hd.zw) - 1), 0);\n"
    "}\n"
    "vec2 HdCoordinates(vec2 t) {\n"
    "    return ((mode.y & 64) != 0 && filterBounds.z >= filterBounds.x\n"
    "        ? t : vec2(WindowUV(ivec2(floor(t)))) + fract(t)) - vec2(hdTexels.xy);\n"
    "}\n"
#ifdef RENDER_FILTER
    RENDER_FILTER
    "void FilterTap(vec4 sampleColour, float weight, inout vec4 opaque, inout vec4 semi) {\n"
    "    if (sampleColour.a < 0.25) return;\n"
    "    vec4 covered = vec4(sampleColour.rgb, 1.0) * weight;\n"
    "    if (semiMode >= 0 && ((mode.y & 64) != 0 || sampleColour.a < 0.75)) semi += covered;\n"
    "    else opaque += covered;\n"
    "}\n"
    /* Resolve palette colours and coverage before the owner's sampling rule. */
    "void FilteredTexture(vec2 t) {\n"
    "    vec4 opaque = vec4(0.0), semi = vec4(0.0);\n"
    "    vec2 at = FilterPosition(t);\n"
    "    bool useHd = false;\n"
    "    if (hd.z > 0u) {\n"
    "        vec2 into = vec2(MaterialUV(ivec2(floor(t))) - hdTexels.xy) + fract(t);\n"
    "        useHd = all(greaterThanEqual(into, vec2(0.0))) && all(lessThan(into, vec2(hdTexels.zw)));\n"
    "        if (useHd) at = FilterHdPosition(HdCoordinates(t));\n"
    "    }\n"
    "    vec2 f = FilterFraction(fract(at));\n"
    "    ivec2 base = ivec2(floor(at));\n"
    "    for (int i = 0; i < FILTER_TAPS; i++) {\n"
    "        ivec2 corner = FilterCorner(i, f);\n"
    "        float weight = FilterWeight(i, f);\n"
    "        ivec2 p = base + corner;\n"
    "        vec4 sampleColour;\n"
    "        if (useHd) sampleColour = HdSample(p);\n"
    "        else sampleColour = NativeSample(p);\n"
    "        FilterTap(sampleColour, weight, opaque, semi);\n"
    "    }\n"
    "    float fade = vColour.a;\n"
    /* Only fully opaque, unfaded coverage writes depth; filtered edges leave the background visible. */
    "    if (mode.z == 3) {\n"
    "        if (opaque.a < 0.999 || fade < 0.999) discard;\n"
    "        fragment = vec4(0.0);\n"
    "        return;\n"
    "    }\n"
    "    if (mode.z == 1) semi = vec4(0.0);\n"
    "    if (mode.z == 2) opaque = vec4(0.0);\n"
    "    if (opaque.a + semi.a < 0.004) discard;\n"
#ifdef RENDER_LIGHTING
    "    vec3 albedo = opaque.rgb;\n"
#endif
    "    bool tinted = (mode.y & 1) == 0;\n"
    "    if (tinted) {\n"
    "        vec3 tint = TextureTint();\n"
    "        opaque.rgb *= tint;\n"
    "        semi.rgb *= tint;\n"
    "    }\n"
#ifdef RENDER_LIGHTING
    "    opaque.rgb = LightMaterial(opaque.rgb, albedo, opaque.a);\n"
#endif
#ifdef RENDER_AUX_COLOUR
    "    if (hdrActive) {\n"
    "        vec3 hdrOpaque = HdrMaterial(opaque.rgb, opaque.a);\n"
    "        vec3 hdrSemi = HdrMaterial(semi.rgb, semi.a);\n"
    "        float semiWeight = semiMode == 0 ? 0.5 : (semiMode == 3 ? 0.25 : 1.0);\n"
    "        float coverage = opaque.a + (semiMode == 0 ? semi.a * 0.5 : 0.0);\n"
    "        WriteAux(vec4(hdrOpaque + hdrSemi * semiWeight, coverage) * fade);\n"
    "    }\n"
#endif
    /* RGB is premultiplied by coverage: cap SDR tinted colours at coverage. */
    "    if (tinted) {\n"
    "        opaque.rgb = min(opaque.rgb, vec3(opaque.a));\n"
    "        semi.rgb = min(semi.rgb, vec3(semi.a));\n"
    "    }\n"
    "    if (semiMode == 0) semi *= 0.5;\n"
    "    else {\n"
    "        if (semiMode == 3) semi.rgb *= 0.25;\n"
    "        semi.a = 0.0;\n"
    "    }\n"
    "    fragment = (opaque + semi) * fade;\n"
    "}\n"
#endif
    /* Full-colour pages use the owning renderer's sampler too. */
    "vec4 RgbaSample(sampler2D image, vec2 at, vec2 low, vec2 high) {\n"
#ifdef RENDER_FILTER
    "    if (filtered != 0) {\n"
    "        vec2 pos = FilterPosition(at), base = floor(pos), f = FilterFraction(fract(pos));\n"
    "        vec4 result = vec4(0.0);\n"
    "        for (int i = 0; i < FILTER_TAPS; i++) {\n"
    "            vec2 p = base + vec2(FilterCorner(i, f)) + 0.5;\n"
    "            result += texelFetch(image, ivec2(clamp(p, low, high)), 0) * FilterWeight(i, f);\n"
    "        }\n"
    "        return result;\n"
    "    }\n"
#endif
    "    return texelFetch(image, ivec2(clamp(floor(at) + 0.5, low, high)), 0);\n"
    "}\n"
    "void main() {\n"
#ifdef RENDER_LIGHTING
    "    SurfaceLighting();\n"
#endif
    "    vec2 t = vTexel.xy / vTexel.z;\n"
    "    vec3 c = VertexColour();\n"
    "    float fade = vColour.a;\n"
    "    if (mode.x == 0) {\n"
    "        Out(c, c, 1.0, semiMode >= 0, false);\n"
    "        return;\n"
    "    }\n"
    "    int portPage = texturePage.x / 64 + 16 * ((texturePage.y / 256) & 1) + (texturePage.y >= 512 ? 32 : 0);\n"
    "    if (mode.w == 3 && portPage > 0) {\n"  /* the port's other pages (the PC version's): its colour key, the
                                                 vertex colour as Direct3D's (255 white), blended whole */
    "        int k = portPage - 1;\n"
    /* (hdTexels: the texels the primitive covers, its samples kept to them; PORT_PAGES_...) */
    "        vec2 low = clamp(vec2(hdTexels.xy) + 0.5, 0.5, 255.5);\n"
    "        vec2 high = clamp(max(low, vec2(hdTexels.zw) - 0.5), 0.5, 255.5);\n"
    "        vec2 page = vec2(float(k % 8), float(k / 8)) * 256.0;\n"
    "        vec4 g = RgbaSample(portPages, page + t, page + low, page + high);\n"
    "        if (g.a < 0.004) discard;\n"
    "        Out((mode.y & 1) == 0 ? g.rgb * c : g.rgb, g.rgb, g.a, semiMode >= 0, false);\n"
    "        return;\n"
    "    }\n"
    "    if (mode.w == 3) {\n"                 /* the platform's glyphs: premultiplied, with coverage */
    "        vec2 size = vec2(textureSize(glyphs, 0));\n"
    "        vec4 g = RgbaSample(glyphs, t * size / 256.0, vec2(0.5), size - 0.5);\n"
    "        if (g.a < 0.004) discard;\n"
    "        bool tinted = (mode.y & 1) == 0;\n"
    "        Out(tinted ? g.rgb * TextureTint() : g.rgb, g.rgb, g.a, semiMode >= 0, tinted);\n"
    "        return;\n"
    "    }\n"
#ifdef RENDER_FILTER
    "    if (filtered != 0) { FilteredTexture(t); return; }\n"
#endif
    "    ivec2 uv = WindowUV(ivec2(floor(t)));\n"
    /* the HD texture over the texels it covers: alpha 0 transparent, 0.5 semi-transparent, 1 opaque */
    "    if (hd.z > 0u) {\n"
    "        vec2 into = vec2(MaterialUV(ivec2(floor(t))) - hdTexels.xy) + fract(t);\n"
    "        if (all(greaterThanEqual(into, vec2(0.0))) && all(lessThan(into, vec2(hdTexels.zw)))) {\n"
    "            ivec2 at = ivec2(floor(HdCoordinates(t) * vec2(hd.zw) / vec2(hdTexels.zw)));\n"
    "            vec4 h = HdSample(at);\n"
    "            if (h.a < 0.25) discard;\n"
    "            bool hdSemi = (mode.y & 64) != 0 || h.a < 0.75;\n"
    "            bool tinted = (mode.y & 1) == 0;\n"
    "            Out(tinted ? h.rgb * TextureTint() : h.rgb, h.rgb, 1.0, semiMode >= 0 && hdSemi, tinted);\n"
    "            return;\n"
    "        }\n"
    "    }\n"
    "    if ((mode.y & 64) != 0) {\n"
    "        vec4 c = NativeSample(ivec2(floor(t)));\n"
    "        if (c.a == 0.0) discard;\n"
    /* Match filtered/HD modulation: saturation precedes fade and blend weighting. */
    "        bool tinted = (mode.y & 1) == 0;\n"
    "        Out(tinted ? c.rgb * TextureTint() : c.rgb, c.rgb, c.a, semiMode >= 0, tinted);\n"
    "        return;\n"
    "    }\n"
    "    uint texel = NativeTexel(uv);\n"
    "    if (texel == 0u) discard;\n"
    "    bool semi = (texel & 0x8000u) != 0u;\n"
    "    vec3 t8 = vec3(float(texel & 31u), float((texel >> 5) & 31u), float((texel >> 10) & 31u)) * 8.0 / 255.0;\n"
    "    bool tinted = (mode.y & 1) == 0;\n"
    "    Out(tinted ? t8 * TextureTint() : t8, t8, 1.0, semiMode >= 0 && semi, tinted);\n"
    "}\n"
