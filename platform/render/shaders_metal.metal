#include <metal_stdlib>
using namespace metal;
struct Uniforms {
    float targetX, targetY, originX, originY, depthPull;
    int pass, depthPass, left, top, right, bottom, factor, targetScale, adaptive;
    float stepSize, uiScaleX, uiScaleY, uiOffsetX, uiOffsetY;
    float rasterScale;
    int window;
    float surfaceHeight;
};
struct GameIn {
    float3 position [[attribute(0)]];
    float4 colour [[attribute(1)]];
    float2 texel [[attribute(2)]];
    int4 page [[attribute(3)]];
    uint4 window [[attribute(4)]];
    uint4 mode [[attribute(5)]];
    uint4 hd [[attribute(6)]];
    int4 hdTexels [[attribute(7)]];
};
struct GameOut {
    float4 position [[position]];
    float4 vColour;
    float2 vTexel;
    int semiMode [[flat]];
    int hudColourOverlay [[flat]];
    int4 texturePage [[flat]];
    int4 clut [[flat]];
    int4 vWindow [[flat]];
    int4 mode [[flat]];
    uint4 hd [[flat]];
    int4 hdTexels [[flat]];
};
vertex GameOut gameVertex(GameIn a [[stage_in]], constant Uniforms &u [[buffer(1)]])
{
    GameOut o;
    float w = a.position.z != 0.0 ? abs(a.position.z) : 1.0;
    float2 ndc = (a.position.xy - float2(u.originX, u.originY)) / float2(u.targetX, u.targetY) * 2.0 - 1.0;
    float pull = u.depthPass == 1 || (a.mode.w & 1u) == 1u ? 1.0 : u.depthPull;
    float z = a.position.z > 0.0 ? w - pull : (u.depthPass == 1 ? w : 0.0);
    o.position = float4(ndc.x * w, (u.window ? ndc.y : -ndc.y) * w, z, w);
    o.vColour = a.colour;
    o.vTexel = a.texel;
    o.texturePage = int4(a.page.xy, 0, 0);
    o.clut = int4(a.page.zw, 0, 0);
    o.semiMode = int(a.mode.w >> 4u) - 1;
    o.hudColourOverlay = int((a.mode.w >> 1u) & 1u);
    o.vWindow = int4(a.window);
    o.mode = int4(int(a.mode.x), int(a.mode.y), u.pass, int(a.mode.z));
    o.hd = a.hd;
    o.hdTexels = a.hdTexels;
    return o;
}
uint Fetch(texture2d<uint> source, int2 p)
{
    return source.read(uint2(p & 1023)).r;
}
float4 Output(float3 col, float cov, bool blended, GameOut in)
{
    float4 result;
    float4 vColour = in.vColour;
    int4 mode = in.mode;
    int semiMode = in.semiMode;
    int hudColourOverlay = in.hudColourOverlay;

    if (semiMode == 4) blended = false;
    float fade = vColour.a;
    if (mode.z == 3) {
        if (blended || fade < 0.999 || cov < 0.999)
            discard_fragment();
        result = float4(0.0);
        return result;
    }
    if (mode.z == 1 && blended)
        discard_fragment();
    if (mode.z == 2 && !blended)
        discard_fragment();
    col *= fade;
    cov *= fade;
    if (!blended)
        result = float4(col, cov);
    else if (hudColourOverlay != 0 && semiMode == 1) {
        float coverage = clamp(max(col.r, max(col.g, col.b)) * 2.0, 0.0, 1.0);
        result = float4(col, 0.35 * coverage * cov);
    } else if (semiMode == 0)
        result = float4(col * 0.5, cov * 0.5);
    else if (semiMode == 3)
        result = float4(col * 0.25, 0.0);
    else
        result = float4(col, 0.0);

    return result;
}
fragment float4 gameFragment(GameOut in [[stage_in]], constant Uniforms &u [[buffer(1)]], texture2d<uint> vram [[texture(0)]],
                             texture2d<float> glyphs [[texture(1)]], texture2d<float> hdAtlas [[texture(2)]],
                             texture2d<float> portPages [[texture(3)]],
                             texture2d_array<uint> decoded [[texture(4)]],
                             sampler glyphSampler [[sampler(1)]], sampler portSampler [[sampler(3)]])
{
    float4 vColour = in.vColour;
    float2 vTexel = in.vTexel;
    int semiMode = in.semiMode;
    int4 texturePage = in.texturePage, clut = in.clut, vWindow = in.vWindow, mode = in.mode;
    uint4 hd = in.hd;
    int4 hdTexels = in.hdTexels;

    if (semiMode == 4) {
        int2 pixel = int2(floor(in.position.xy / u.rasterScale));
        if (((pixel.x + pixel.y) & 1) != 0) discard_fragment();
    }
    float3 c = vColour.rgb;
    if (mode.x == 0) {
        return Output(c, 1.0, semiMode >= 0, in);
    }
    float2 t = vTexel.xy;
    int portPage = texturePage.x / 64 + 16 * ((texturePage.y / 256) & 1) + (texturePage.y >= 512 ? 32 : 0);
    if (mode.w == 3 && portPage > 0) {
        int k = portPage - 1;
        float2 low = clamp(float2(hdTexels.xy) + 0.5, 0.5, 255.5);
        float2 high = clamp(max(low, float2(hdTexels.zw) - 0.5), 0.5, 255.5);
        float2 at = float2(float(k % 8), float(k / 8)) * 256.0 + clamp(t, low, high);
        float4 g = portPages.sample(portSampler, at / float2(2048.0, 2048.0));
        if (g.a < 0.004)
            discard_fragment();
        return Output(mode.y == 0 ? g.rgb * c : g.rgb, g.a, semiMode >= 0, in);
    }
    if (mode.w == 3) {
        float4 g = glyphs.sample(glyphSampler, t / 256.0);
        if (g.a < 0.004)
            discard_fragment();
        float3 rgb = mode.y == 0 ? min(g.rgb * c * (255.0 / 128.0), float3(g.a)) : g.rgb;
        return Output(rgb, g.a, semiMode >= 0, in);
    }
    int2 uv = int2(floor(t)) & 255;
    uv.x = (uv.x & ~(vWindow.x * 8)) | ((vWindow.z & vWindow.x) * 8);
    uv.y = (uv.y & ~(vWindow.y * 8)) | ((vWindow.w & vWindow.y) * 8);
    if (hd.z > 0u) {
        float2 into = float2(uv - hdTexels.xy) + (t - floor(t));
        if (all(into >= float2(0.0)) && all(into < float2(hdTexels.zw))) {
            int2 at = min(int2(into * float2(hd.zw) / float2(hdTexels.zw)), int2(hd.zw) - 1);
            float4 h = hdAtlas.read(uint2(int2(hd.xy) + at));
            if (h.a < 0.25)
                discard_fragment();
            bool hdSemi = h.a < 0.75;
            float3 hc = mode.y == 0 ? min(h.rgb * c * (255.0 / 128.0), float3(1.0)) : h.rgb;
            return Output(hc, 1.0, semiMode >= 0 && hdSemi, in);
        }
    }
    uint texel;
    if (mode.x == 2) {
        texel = decoded.read(uint2(uv), uint(texturePage.x)).r;
    } else if (mode.w == 0) {
        uint word = Fetch(vram, texturePage.xy + int2(uv.x >> 2, uv.y));
        uint index = (word >> uint((uv.x & 3) * 4)) & 15u;
        texel = Fetch(vram, clut.xy + int2(int(index), 0));
    } else if (mode.w == 1) {
        uint word = Fetch(vram, texturePage.xy + int2(uv.x >> 1, uv.y));
        uint index = (word >> uint((uv.x & 1) * 8)) & 255u;
        texel = Fetch(vram, clut.xy + int2(int(index), 0));
    } else {
        texel = Fetch(vram, texturePage.xy + uv);
    }
    if (texel == 0u)
        discard_fragment();
    bool semi = (texel & 0x8000u) != 0u;
    float3 t8 =
        float3(float(texel & 31u), float((texel >> 5) & 31u), float((texel >> 10) & 31u)) * 8.0 / 255.0;
    if (mode.y == 0) {
        t8 = min(t8 * c * (255.0 / 128.0), float3(1.0));
    }
    return Output(t8, 1.0, semiMode >= 0 && semi, in);
}
struct PictureOut {
    float4 position [[position]];
};
vertex PictureOut pictureVertex(uint id [[vertex_id]], constant Uniforms &u [[buffer(1)]])
{
    float2 p = float2((id << 1) & 2, id & 2);
    float2 ndc = p * 2.0 - 1.0;
    return {float4(ndc.x, u.window ? ndc.y : -ndc.y, 0.0, 1.0)};
}
float2 LogicalPixel(float4 pos, constant Uniforms &u)
{
    return float2(pos.x, u.window ? u.surfaceHeight - pos.y : pos.y);
}
fragment float4 averageFragment(PictureOut in [[stage_in]], constant Uniforms &u [[buffer(1)]],
                                texture2d<float> source [[texture(0)]], sampler linearSampler [[sampler(0)]])
{
    float step = float(u.factor) / float(u.targetScale);
    float2 lo = float2(u.left, u.top) + floor(LogicalPixel(in.position, u)) * step;
    float2 hi = min(lo + step, float2(u.right, u.bottom));
    float4 sum = 0.0;
    float3 squares = 0.0;
    if (u.adaptive == 0 && u.factor % (u.targetScale * 2) == 0 && (u.right - u.left) % 2 == 0 &&
        (u.bottom - u.top) % 2 == 0) {
        float2 size = float2(source.get_width(), source.get_height());
        for (int y = int(lo.y); y < int(hi.y); y += 2)
            for (int x = int(lo.x); x < int(hi.x); x += 2)
                sum += source.sample(linearSampler, (float2(x, y) + 1.0) / size, level(0.0)) * 4.0;
    } else {
        for (int y = int(floor(lo.y)); y < int(ceil(hi.y)); y++)
            for (int x = int(floor(lo.x)); x < int(ceil(hi.x)); x++) {
                float2 cell = float2(x, y);
                float2 covered = max(min(hi, cell + 1.0) - max(lo, cell), float2(0.0));
                float weight = covered.x * covered.y;
                float4 v = source.read(uint2(x, y));
                sum += v * weight;
                if (u.adaptive)
                    squares += v.rgb * v.rgb * weight;
            }
    }
    float area = (hi.x - lo.x) * (hi.y - lo.y);
    float4 colour = sum / area;
    if (u.adaptive) {
        float3 variance = max(squares / area - colour.rgb * colour.rgb, float3(0.0));
        float deviation = sqrt(max(variance.r, max(variance.g, variance.b)));
        colour.a = smoothstep(0.5 / 255.0, 4.0 / 255.0, deviation);
    }
    return colour;
}
fragment float4 detailFragment(PictureOut in [[stage_in]], constant Uniforms &u [[buffer(1)]],
                               texture2d<float> source [[texture(0)]])
{
    int2 at = int2(LogicalPixel(in.position, u)), last = int2(source.get_width(), source.get_height()) - 1;
    float detail = 0.0;
    for (int y = -1; y <= 1; y++)
        for (int x = -1; x <= 1; x++)
            detail = max(detail, source.read(uint2(clamp(at + int2(x, y), int2(0), last))).a);
    return float4(source.read(uint2(at)).rgb, detail);
}
fragment float4 compositeFragment(PictureOut in [[stage_in]], constant Uniforms &u [[buffer(1)]],
                                  texture2d<float> source [[texture(0)]],
                                  texture2d<float> coarse [[texture(1)]],
                                  sampler linearSampler [[sampler(1)]])
{
    float2 p = LogicalPixel(in.position, u), size = float2(coarse.get_width(), coarse.get_height());
    float2 uv = clamp(p / u.stepSize, float2(0.5), size - 0.5) / size;
    float4 low = coarse.sample(linearSampler, uv);
    float3 high = source.read(uint2(int2(u.left, u.top) + int2(p))).rgb;
    return float4(mix(low.rgb, high, low.a), 1.0);
}
struct UiIn {
    float2 position [[attribute(0)]];
    float4 colour [[attribute(1)]];
    float2 uv [[attribute(2)]];
};
struct UiOut {
    float4 position [[position]];
    float4 colour;
    float2 uv;
};
vertex UiOut uiVertex(UiIn a [[stage_in]], constant Uniforms &u [[buffer(1)]])
{
    float2 p = a.position * float2(u.uiScaleX, u.uiScaleY) + float2(u.uiOffsetX, u.uiOffsetY);
    return {float4(p.x, u.window ? p.y : -p.y, 0.0, 1.0), a.colour, a.uv};
}
fragment float4 uiFragment(UiOut in [[stage_in]], texture2d<float> source [[texture(0)]],
                           sampler sam [[sampler(0)]])
{
    float4 c = source.sample(sam, in.uv) * in.colour;
    return float4(c.rgb * c.a, c.a);
}
struct CopyUniforms {
    float fromX, fromY, fromW, fromH, toX, toY, toW, toH, window, height;
};
fragment float4 copyFragment(PictureOut in [[stage_in]], constant CopyUniforms &c [[buffer(2)]],
                             texture2d<float> source [[texture(0)]], sampler sam [[sampler(0)]])
{
    float2 p = float2(in.position.x, c.window ? c.height - in.position.y : in.position.y);
    float2 uv = (float2(c.fromX, c.fromY) +
                 (p - float2(c.toX, c.toY)) / float2(c.toW, c.toH) * float2(c.fromW, c.fromH)) /
                float2(source.get_width(), source.get_height());
    return source.sample(sam, uv, level(0.0));
}
fragment float4 clearFragment(constant float4 &colour [[buffer(2)]])
{
    return colour;
}
struct ClearDepth {
    float4 colour [[color(0)]];
    float depth [[depth(any)]];
};
fragment ClearDepth clearDepthFragment(constant float4 &colour [[buffer(2)]])
{
    return {colour, 1.0};
}

struct ModernUniforms {
    float targetX, targetY, originX, originY, depthPull;
    int pass, depthPass, left, top, right, bottom, factor, targetScale, adaptive;
    float stepSize, uiScaleX, uiScaleY, uiOffsetX, uiOffsetY;
    float rasterScale;
    int lightCount;
    packed_float4 lightPosition[16], lightColour[16], shieldGlow;
    packed_float4 effect[8];
    int window;
    float surfaceHeight;
};
/* Modern scene */
struct ModernGameIn {
    float3 position [[attribute(0)]];
    float4 colour [[attribute(1)]];
    float2 texel [[attribute(2)]];
    int4 page [[attribute(3)]];
    uint4 window [[attribute(4)]];
    uint4 mode [[attribute(5)]];
    uint4 hd [[attribute(6)]];
    int4 hdTexels [[attribute(7)]];
    int4 filterBounds [[attribute(8)]];
    float4 view [[attribute(9)]];
    float3 shutter [[attribute(10)]];
    int4 emission [[attribute(11)]];
};
struct ModernGameOut {
    float4 position [[position]];
    float4 vColour;
    float4 vTexel;
    float3 vPixel;
    int semiMode [[flat]];
    int hudColourOverlay [[flat]];
    int4 texturePage [[flat]];
    int4 clut [[flat]];
    int4 vWindow [[flat]];
    int4 mode [[flat]];
    uint4 hd [[flat]];
    int4 hdTexels [[flat]];
    int4 filterBounds [[flat]];
    int material [[flat]];
    float4 view;
    float3 shutter;
    int4 emission [[flat]];
};
vertex ModernGameOut ModerngameVertex(ModernGameIn a [[stage_in]], constant ModernUniforms &u [[buffer(1)]])
{
    ModernGameOut o;
    float w = a.position.z != 0.0 ? abs(a.position.z) : 1.0;
    float2 grid = u.effect[0].yz;
    float2 pixel = (a.mode.y & 128u) != 0u ? floor(a.position.xy * grid) / grid : a.position.xy;
    float2 ndc = (pixel - float2(u.originX, u.originY)) / float2(u.targetX, u.targetY) * 2.0 - 1.0;
    float pull = u.depthPass == 1 || (a.mode.w & 1u) == 1u ? 1.0 : u.depthPull;
    float z = a.position.z > 0.0 ? w - pull : (u.depthPass == 1 ? w : 0.0);
    o.position = float4(ndc.x * w, (u.window ? ndc.y : -ndc.y) * w, z, w);
    float colourWeight = (a.mode.y & 16u) != 0u ? w : 1.0;
    o.view = a.view;
    o.shutter = a.shutter;
    o.emission = a.emission;
    o.vColour = float4(a.colour.rgb * colourWeight, a.colour.a);
    o.vTexel = (a.mode.y & 32u) != 0u ? float4(a.texel * w, w, colourWeight) : float4(a.texel, 1.0, colourWeight);
    o.vPixel = float3(pixel * w, w);
    o.texturePage = int4(a.page.xy, 0, 0);
    o.clut = int4(a.page.zw, 0, 0);
    o.semiMode = int(a.mode.w >> 4u) - 1;
    o.hudColourOverlay = int((a.mode.w >> 1u) & 1u);
    o.vWindow = int4(a.window);
    o.mode = int4(int(a.mode.x & 3u), int(a.mode.y), u.pass, int(a.mode.z & 3u));
    o.material = int((a.mode.w >> 2u) & 3u) | (int(a.mode.z >> 2u) << 2) | (int(a.mode.x >> 4u) << 8) | (int((a.mode.x >> 2u) & 3u) << 12);
    o.filterBounds = a.filterBounds;
    o.hd = a.hd;
    o.hdTexels = a.hdTexels;
    return o;
}
uint ModernFetch(texture2d<uint> source, int2 p)
{
    return source.read(uint2(p & 1023)).r;
}
struct ModernGameAux { float4 colour, mask, velocity; };
bool ModernBoostSpark(ModernGameOut in, float gain) { return (in.material & 2048) != 0 && gain > 1.0; }
float3 ModernHdrMaterial(ModernGameOut in, float gain, float3 c, float coverage)
{
    if (ModernBoostSpark(in, gain)) return c * gain;
    return (in.material & 512) != 0 ? min(c, float3(coverage)) : c;
}
void ModernWriteAux(ModernGameOut in, float gain, float4 c, float covered, thread ModernGameAux &aux)
{
    bool overlay = (in.material & 512) != 0 && !ModernBoostSpark(in, gain);
    bool emits = (in.material & 256) != 0;
    if (in.emission.z > 0) {
        float2 t = in.vTexel.xy / in.vTexel.z;
        emits = emits && all(abs(t - float2(in.emission.xy)) < float2(in.emission.zw));
    }
    bool hull = (in.material & 1024) != 0;
    float guard = (overlay || (hull && c.a < 0.999)) && covered > 0.0 ? 1.0 : 0.0;
    float3 past = in.view.xyz + in.shutter;
    float2 velocity = 0.0;
    if (hull && in.view.z > 1.0 && past.z > 1.0)
        velocity = past.xy / past.z - in.view.xy / in.view.z;
    aux.colour = c;
    aux.velocity = float4(float3(velocity, hull ? 1.0 : 0.0) * c.a, c.a);
    aux.mask = float4(emits && !overlay ? c.a : 0.0, overlay ? c.a : 0.0,
                      in.mode.z == 2 ? 0.0 : guard, in.mode.z == 2 ? guard : c.a);
}
float4 ModernOutput(float3 col, float cov, bool blended, ModernGameOut in, bool saturate,
              float gain, bool hdr, float2 grid, thread ModernGameAux &aux)
{
    float4 result;
    float3 unclamped = col;
    if (saturate) col = min(col, float3(cov));
    float4 vColour = in.vColour;
    int4 mode = in.mode;
    int semiMode = in.semiMode;
    int hudColourOverlay = in.hudColourOverlay;

    if ((mode.y & 4) != 0) {
        const int offsets[16] = {-4,0,-3,1, 2,-2,3,-1, -3,1,-4,0, 3,-1,2,-2};
        int2 pixel = int2(floor(in.vPixel.xy / in.vPixel.z)) & 3;
        float dither = (mode.y & 8) != 0 ? float(offsets[pixel.y * 4 + pixel.x]) : 0.0;
        col = floor(clamp(floor(col * 255.0 + 0.0001) + dither, 0.0, 255.0) / 8.0) * (8.0 / 255.0);
    }
    if (semiMode == 4) blended = false;
    float fade = vColour.a;
    if (mode.z == 3) {
        if (blended || fade < 0.999 || cov < 0.999)
            discard_fragment();
        result = float4(0.0);
        return result;
    }
    if (mode.z == 1 && blended)
        discard_fragment();
    if (mode.z == 2 && !blended)
        discard_fragment();
    int coverageMode = (in.material >> 12) & 3;
    if (coverageMode != 0 && blended && semiMode != 2) {
        if (semiMode != 0 && all(col == float3(0.0))) discard_fragment();
        if (coverageMode == 2) {
            int2 dot = int2(floor(in.vPixel.xy / in.vPixel.z * grid));
            if (((dot.x + dot.y) & 1) != 0) discard_fragment();
        } else {
            col *= 0.5;
            cov *= 0.5;
        }
        if (semiMode == 0) blended = false;
    }
    col *= fade;
    cov *= fade;
    if (!blended)
        result = float4(col, cov);
    else if (hudColourOverlay != 0 && semiMode == 1) {
        float coverage = clamp(max(col.r, max(col.g, col.b)) * 2.0, 0.0, 1.0);
        result = float4(col, 0.35 * coverage * cov);
    } else if (semiMode == 0)
        result = float4(col * 0.5, cov * 0.5);
    else if (semiMode == 3)
        result = float4(col * 0.25, 0.0);
    else
        result = float4(col, 0.0);

    if (hdr) {
        float3 radiance = ModernHdrMaterial(in, gain, unclamped, cov / max(fade, 0.00001)) * fade;
        float weight = !blended ? 1.0 : semiMode == 0 ? 0.5 : semiMode == 3 ? 0.25 : 1.0;
        ModernWriteAux(in, gain, float4(radiance * weight, result.a), cov, aux);
    }
    return result;
}
int Address(int p, int low, int high, int mirror, int material) {
    int flags = (material >> 2) & 7, size = high - low + 1;
    if (flags == 0 || (size & (size - 1)) != 0) return clamp(p, low, high);
    int at = p - low;
    if ((flags & mirror) != 0 && (at & size) != 0) at = ~at;
    return low + (at & (size - 1));
}
int2 SampleUV(ModernGameOut in, int2 uv) {
    int4 filterBounds = in.filterBounds, mode = in.mode, vWindow = in.vWindow;
    if (filterBounds.z >= filterBounds.x)
        return int2(Address(uv.x, filterBounds.x, filterBounds.z, 1, in.material),
                     Address(uv.y, filterBounds.y, filterBounds.w, 2, in.material));
    uv &= 255;
    if ((mode.y & 64) == 0) {
        uv.x = (uv.x & ~(vWindow.x * 8)) | ((vWindow.z & vWindow.x) * 8);
        uv.y = (uv.y & ~(vWindow.y * 8)) | ((vWindow.w & vWindow.y) * 8);
    }
    return uv;
}
float4 ModernReadTexel(ModernGameOut in, texture2d<uint> vram, texture2d_array<uint> decoded, int2 uv) {
    int4 mode = in.mode, texturePage = in.texturePage, clut = in.clut;
    uint texel;
    if (mode.x == 2) {
        texel = decoded.read(uint2(uv), uint(texturePage.x)).r;
    } else if (mode.w == 0) {
        uint word = ModernFetch(vram, texturePage.xy + int2(uv.x >> 2, uv.y));
        texel = ModernFetch(vram, clut.xy + int2(int((word >> uint((uv.x & 3) * 4)) & 15u), 0));
    } else if (mode.w == 1) {
        uint word = ModernFetch(vram, texturePage.xy + int2(uv.x >> 1, uv.y));
        texel = ModernFetch(vram, clut.xy + int2(int((word >> uint((uv.x & 1) * 8)) & 255u), 0));
    } else {
        texel = ModernFetch(vram, texturePage.xy + uv);
    }
    bool n64 = (mode.y & 64) != 0;
    if (n64 ? (texel & 32767u) == 0u : texel == 0u) return float4(0.0);
    uint3 rgb = uint3(texel, texel >> 5u, texel >> 10u) & 31u;
    rgb = (rgb << 3u) | (n64 ? (rgb >> 2u) : uint3(0u));
    return float4(float3(rgb) / 255.0, !n64 && (texel & 32768u) != 0u ? 0.5 : 1.0);
}
float3 ModernSurfaceLighting(ModernGameOut in, constant ModernUniforms &u) {
    float3 illumination = 0.0;
    float4 view = in.view, shieldGlow = u.shieldGlow;
    if (view.w < 0.5 || (u.lightCount == 0 && shieldGlow.a == 0.0) || in.mode.z == 3) return illumination;
    float3 normal = cross(dfdx(view.xyz), dfdy(view.xyz));
    float length2 = dot(normal, normal);
    if (length2 < 0.00000001) return illumination;
    normal *= rsqrt(length2);
    if (dot(normal, view.xyz) > 0.0) normal = -normal;
    if (shieldGlow.a > 0.0) {
        float3 viewDir = -view.xyz * rsqrt(max(dot(view.xyz, view.xyz), 1.0));
        float rim = 1.0 - clamp(dot(normal, viewDir), 0.0, 1.0);
        illumination += mix(float3(1.0), shieldGlow.rgb, 0.72) * shieldGlow.a * (0.35 + 0.95 * rim * rim);
    }
    for (int i = 0; i < u.lightCount; i++) {
        float3 delta = u.lightPosition[i].xyz - view.xyz;
        float distance2 = dot(delta, delta), radius = u.lightPosition[i].w;
        float falloff = max(1.0 - distance2 / (radius * radius), 0.0);
        float diffuse = max(dot(normal, delta) * rsqrt(max(distance2, 1.0)), 0.0);
        illumination += u.lightColour[i].rgb * u.lightColour[i].w * diffuse * falloff * falloff;
    }
    return illumination;
}
float3 ModernLinearColour(float3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, float3(2.4)), step(float3(0.04045), c));
}
float3 ModernLightMaterial(float3 c, float3 albedo, float coverage, float3 illumination) {
    if (coverage <= 0.0 || all(illumination == float3(0.0))) return c;
    float3 lit = ModernLinearColour(max(c / coverage, 0.0)) + ModernLinearColour(max(albedo / coverage, 0.0)) * illumination;
    return mix(lit * 12.92, 1.055 * pow(lit, float3(1.0 / 2.4)) - 0.055, step(float3(0.0031308), lit)) * coverage;
}
float4 ModernFiltered(ModernGameOut in, texture2d<uint> vram, texture2d_array<uint> decoded, texture2d<float> hdAtlas,
                float2 uv, float3 tint, int kind, bool useHd, float3 illumination,
                float gain, bool hdr, thread ModernGameAux &aux) {
    uint4 hd = in.hd;
    int4 hdTexels = in.hdTexels, mode = in.mode;
    int semiMode = in.semiMode;
    float2 size = float2(hd.zw), nativeSize = float2(hdTexels.zw);
    float2 at = uv - (kind == 2 ? 0.5 : 0.0);
    if (useHd) {
        float2 into = float2(SampleUV(in, int2(floor(uv))) - hdTexels.xy) + fract(uv);
        at = (into + (kind == 1 ? 0.5 : 0.0)) * size / nativeSize - 0.5;
    }
    float2 f = fract(at);
    int2 base = int2(floor(at));
    float4 opaque = float4(0.0), semi = float4(0.0);
    if (kind == 1) f = floor(f * 32.0) / 32.0;
    bool upper = f.x + f.y >= 1.0;
    for (int tap = 0; tap < 4; tap++) {
        if (kind == 1 && tap == 3) break;
        int2 offset = int2(tap & 1, tap >> 1);
        float2 weights = mix(1.0 - f, f, float2(offset));
        float weight = weights.x * weights.y;
        if (kind == 1) {
            if (tap == 0) offset = upper ? int2(1) : int2(0);
            weight = (upper ? float3(f.x + f.y - 1.0, 1.0 - f.y, 1.0 - f.x) : float3(1.0 - f.x - f.y, f.x, f.y))[tap];
        }
        int2 p = base + offset;
        float4 texel = useHd ? hdAtlas.read(uint2(int2(hd.xy) + clamp(p, int2(0), int2(hd.zw) - 1)))
                           : ModernReadTexel(in, vram, decoded, SampleUV(in, p));
        if (texel.a < 0.25) continue;
        float4 covered = float4(texel.rgb, 1.0) * weight;
        if (semiMode >= 0 && ((mode.y & 64) != 0 || texel.a < 0.75)) semi += covered;
        else opaque += covered;
    }
    if (mode.z == 3) {
        if (opaque.a < 0.999 || in.vColour.a < 0.999) discard_fragment();
        return float4(0.0);
    }
    if (mode.z == 1) semi = float4(0.0);
    if (mode.z == 2) opaque = float4(0.0);
    if (opaque.a + semi.a < 0.004) discard_fragment();
    opaque.rgb = ModernLightMaterial(opaque.rgb * tint, opaque.rgb, opaque.a, illumination);
    semi.rgb *= tint;
    if (hdr) {
        float semiWeight = semiMode == 0 ? 0.5 : semiMode == 3 ? 0.25 : 1.0;
        float coverage = opaque.a + (semiMode == 0 ? semi.a * 0.5 : 0.0);
        ModernWriteAux(in, gain, float4(ModernHdrMaterial(in, gain, opaque.rgb, opaque.a) +
                 ModernHdrMaterial(in, gain, semi.rgb, semi.a) * semiWeight, coverage) * in.vColour.a,
                 (opaque.a + semi.a) * in.vColour.a, aux);
    }
    opaque.rgb = min(opaque.rgb, float3(opaque.a));
    semi.rgb = min(semi.rgb, float3(semi.a));
    if (semiMode == 0) semi *= 0.5;
    else {
        if (semiMode == 3) semi.rgb *= 0.25;
        semi.a = 0.0;
    }
    return (opaque + semi) * in.vColour.a;
}
float4 ModernShadeGame(ModernGameOut in, constant ModernUniforms &u, texture2d<uint> vram,
                             texture2d<float> glyphs, texture2d<float> hdAtlas,
                             texture2d<float> portPages,
                             texture2d_array<uint> decoded,
                             sampler glyphSampler, sampler portSampler,
                 bool hdr, thread ModernGameAux &aux)
{
    float gain = u.effect[0].x;
    float4 vColour = in.vColour;
    float2 vTexel = in.vTexel.xy / in.vTexel.z;
    int semiMode = in.semiMode;
    int4 texturePage = in.texturePage, mode = in.mode;
    uint4 hd = in.hd;
    int4 hdTexels = in.hdTexels;

    if (semiMode == 4) {
        int2 pixel = int2(floor(in.position.xy / u.rasterScale));
        if (((pixel.x + pixel.y) & 1) != 0) discard_fragment();
    }
    float3 illumination = ModernSurfaceLighting(in, u);
    float3 c = vColour.rgb / in.vTexel.w;
    if (mode.x == 0) {
        return ModernOutput(ModernLightMaterial(c, c, 1.0, illumination), 1.0, semiMode >= 0, in, false, gain, hdr, u.effect[0].yz, aux);
    }
    float2 t = vTexel.xy;
    int portPage = texturePage.x / 64 + 16 * ((texturePage.y / 256) & 1) + (texturePage.y >= 512 ? 32 : 0);
    if (mode.w == 3 && portPage > 0) {
        int k = portPage - 1;
        float2 low = clamp(float2(hdTexels.xy) + 0.5, 0.5, 255.5);
        float2 high = clamp(max(low, float2(hdTexels.zw) - 0.5), 0.5, 255.5);
        float2 at = float2(float(k % 8), float(k / 8)) * 256.0 + clamp(t, low, high);
        float4 g = portPages.sample(portSampler, at / float2(2048.0, 2048.0));
        if (g.a < 0.004)
            discard_fragment();
        return ModernOutput((mode.y & 1) == 0 ? g.rgb * c : g.rgb, g.a, semiMode >= 0, in, false, gain, hdr, u.effect[0].yz, aux);
    }
    if (mode.w == 3) {
        float4 g = glyphs.sample(glyphSampler, t / 256.0);
        if (g.a < 0.004)
            discard_fragment();
        float3 rgb = (mode.y & 1) == 0 ? min(g.rgb * c * (255.0 / 128.0), float3(g.a)) : g.rgb;
        return ModernOutput(rgb, g.a, semiMode >= 0, in, false, gain, hdr, u.effect[0].yz, aux);
    }
    float3 tint = (mode.y & 1) != 0 ? float3(1.0)
        : c * (((mode.y & 64) != 0 || (in.material & 64) != 0) && (in.material & 32) == 0 ? 1.0 : 255.0 / 128.0);
    int2 uv = SampleUV(in, int2(floor(t)));
    float2 into = float2(uv - hdTexels.xy) + fract(t);
    bool useHd = hd.z > 0u && all(into >= float2(0.0)) && all(into < float2(hdTexels.zw));
    if ((in.material & 3) != 0 && semiMode != 4) {
        return ModernFiltered(in, vram, decoded, hdAtlas, t, tint, in.material & 3, useHd, illumination, gain, hdr, aux);
    }
    float4 texel;
    if (useHd) {
        int2 at = min(int2(into * float2(hd.zw) / float2(hdTexels.zw)), int2(hd.zw) - 1);
        texel = hdAtlas.read(uint2(int2(hd.xy) + at));
    } else {
        texel = ModernReadTexel(in, vram, decoded, uv);
    }
    if (texel.a < 0.25) discard_fragment();
    bool semi = (mode.y & 64) != 0 || texel.a < 0.75;
    return ModernOutput(ModernLightMaterial(texel.rgb * tint, texel.rgb, 1.0, illumination), 1.0, semiMode >= 0 && semi, in, true, gain, hdr, u.effect[0].yz, aux);
}
struct ModernGameHdrResult {
    float4 sdr [[color(0)]], hdr [[color(1)]], mask [[color(2)]];
};
struct ModernGameMotionResult {
    float4 sdr [[color(0)]], hdr [[color(1)]], mask [[color(2)]], velocity [[color(3)]];
};
fragment float4 ModerngameFragment(ModernGameOut in [[stage_in]], constant ModernUniforms &u [[buffer(1)]], texture2d<uint> vram [[texture(0)]],
                             texture2d<float> glyphs [[texture(1)]], texture2d<float> hdAtlas [[texture(2)]],
                             texture2d<float> portPages [[texture(3)]],
                             texture2d_array<uint> decoded [[texture(4)]],
                             sampler glyphSampler [[sampler(1)]], sampler portSampler [[sampler(3)]])
{
    ModernGameAux aux{};
    return ModernShadeGame(in, u, vram, glyphs, hdAtlas, portPages, decoded, glyphSampler, portSampler, false, aux);
}
fragment void ModerngameDepthFragment(ModernGameOut in [[stage_in]], constant ModernUniforms &u [[buffer(1)]],
                                texture2d<uint> vram [[texture(0)]], texture2d<float> glyphs [[texture(1)]],
                                texture2d<float> hdAtlas [[texture(2)]], texture2d<float> portPages [[texture(3)]],
                                texture2d_array<uint> decoded [[texture(4)]],
                                sampler glyphSampler [[sampler(1)]], sampler portSampler [[sampler(3)]])
{
    // Keep the same alpha coverage tests while specializing away colour and lighting.
    in.mode.z = 3;
    ModernGameAux aux{};
    ModernShadeGame(in, u, vram, glyphs, hdAtlas, portPages, decoded, glyphSampler, portSampler, false, aux);
}
fragment ModernGameHdrResult ModerngameHdrFragment(ModernGameOut in [[stage_in]], constant ModernUniforms &u [[buffer(1)]], texture2d<uint> vram [[texture(0)]],
                             texture2d<float> glyphs [[texture(1)]], texture2d<float> hdAtlas [[texture(2)]],
                             texture2d<float> portPages [[texture(3)]],
                             texture2d_array<uint> decoded [[texture(4)]],
                             sampler glyphSampler [[sampler(1)]], sampler portSampler [[sampler(3)]])
{
    ModernGameAux aux{};
    float4 sdr = ModernShadeGame(in, u, vram, glyphs, hdAtlas, portPages, decoded, glyphSampler, portSampler, true, aux);
    return {sdr, aux.colour, aux.mask};
}
fragment ModernGameMotionResult ModerngameMotionFragment(ModernGameOut in [[stage_in]], constant ModernUniforms &u [[buffer(1)]], texture2d<uint> vram [[texture(0)]],
                             texture2d<float> glyphs [[texture(1)]], texture2d<float> hdAtlas [[texture(2)]],
                             texture2d<float> portPages [[texture(3)]],
                             texture2d_array<uint> decoded [[texture(4)]],
                             sampler glyphSampler [[sampler(1)]], sampler portSampler [[sampler(3)]])
{
    ModernGameAux aux{};
    float4 sdr = ModernShadeGame(in, u, vram, glyphs, hdAtlas, portPages, decoded, glyphSampler, portSampler, true, aux);
    return {sdr, aux.colour, aux.mask, aux.velocity};
}

/* Modern postprocessing */
/* Generated by tools/generate_modern_metal.py. */
struct modernMotionFragmentUniforms
{
    float4 effect[8];
};

struct modernMotionFragment_out
{
    float4 motion [[color(0)]];
};

static inline __attribute__((always_inline))
float4 modernMotionFragment_Field(thread const int2& p, texture2d<float> image0, sampler image0Smplr, texture2d<float> image2, sampler image2Smplr, depth2d<float> image1, sampler image1Smplr, constant modernMotionFragmentUniforms& _53)
{
    float2 guard = image0.read(uint2(p), 0).zw;
    float3 objectMotion = image2.read(uint2(p), 0).xyz;
    float reciprocal = 1.0 - image1.read(uint2((p - int4(_53.effect[1]).xy)), 0);
    bool hull = objectMotion.z > 0.0;
    bool _77 = guard.x > 0.0;
    bool _85;
    if (!_77)
    {
        _85 = guard.y < 1.0;
    }
    else
    {
        _85 = _77;
    }
    bool _97;
    if (!_85)
    {
        bool _96;
        if (hull)
        {
            _96 = objectMotion.z < 0.999000012874603271484375;
        }
        else
        {
            _96 = hull;
        }
        _97 = _96;
    }
    else
    {
        _97 = _85;
    }
    if (_97)
    {
        return float4(0.0, 0.0, reciprocal, -1.0);
    }
    if (reciprocal <= 0.0)
    {
        return float4(0.0);
    }
    float2 current = float2(p) + float2(0.5);
    float2 travel;
    if (hull)
    {
        travel = objectMotion.xy * _53.effect[2].zw;
    }
    else
    {
        float z = 1.0 / reciprocal;
        float4 point = float4(((current - _53.effect[2].xy) * z) / _53.effect[2].zw, z, 1.0);
        float3 past = float3(dot(_53.effect[3], point), dot(_53.effect[4], point), dot(_53.effect[5], point));
        float2 _167;
        if (past.z > 1.0)
        {
            _167 = (_53.effect[2].xy + ((past.xy * _53.effect[2].zw) / float2(past.z))) - current;
        }
        else
        {
            _167 = float2(0.0);
        }
        travel = _167;
    }
    float radius = length(travel);
    travel *= fast::min(_53.effect[6].z * 0.5, _53.effect[6].w / fast::max(radius, 9.9999997473787516355514526367188e-06));
    return float4(travel, reciprocal, float(hull));
}

static inline __attribute__((always_inline))
float modernMotionFragment_Score(thread const float4& candidate)
{
    return dot(candidate.xy, candidate.xy) * candidate.z;
}

fragment modernMotionFragment_out modernMotionFragment(constant modernMotionFragmentUniforms& _53 [[buffer(3)]], texture2d<float> image0 [[texture(0)]], depth2d<float> image1 [[texture(1)]], texture2d<float> image2 [[texture(2)]], texture2d<float> image3 [[texture(3)]], sampler image0Smplr [[sampler(0)]], sampler image1Smplr [[sampler(1)]], sampler image2Smplr [[sampler(2)]], sampler image3Smplr [[sampler(3)]], float4 gl_FragCoord [[position]])
{
    modernMotionFragment_out out = {};
    int2 at = int2(gl_FragCoord.xy);
    if (int(_53.effect[6].x) == 0)
    {
        int2 param = at + int4(_53.effect[0]).xy;
        out.motion = modernMotionFragment_Field(param, image0, image0Smplr, image2, image2Smplr, image1, image1Smplr, _53);
        return out;
    }
    int2 size = int2(image3.get_width(), image3.get_height());
    out.motion = float4(0.0);
    float best = 0.0;
    if (int(_53.effect[6].x) == 1)
    {
        int2 base = at * int2(4);
        for (int y = 0; y < 4; y++)
        {
            for (int x = 0; x < 4; x++)
            {
                float4 candidate = image3.read(uint2(min((base + int2(x, y)), (size - int2(1)))), 0);
                float4 param_1 = candidate;
                float score = modernMotionFragment_Score(param_1);
                if (score > best)
                {
                    out.motion = candidate;
                    best = score;
                }
            }
        }
        return out;
    }
    int blocks = int(_53.effect[6].y) / 4;
    int2 _321;
    if (int(_53.effect[6].x) == 2)
    {
        _321 = at * int2(blocks);
    }
    else
    {
        _321 = max((at - int2(1)), int2(0));
    }
    int2 lo = _321;
    int2 _340;
    if (int(_53.effect[6].x) == 2)
    {
        _340 = lo + int2(blocks);
    }
    else
    {
        _340 = at + int2(2);
    }
    int2 hi = min(_340, size);
    for (int y_1 = lo.y; y_1 < hi.y; y_1++)
    {
        for (int x_1 = lo.x; x_1 < hi.x; x_1++)
        {
            float4 candidate_1 = image3.read(uint2(int2(x_1, y_1)), 0);
            float4 param_2 = candidate_1;
            float score_1 = modernMotionFragment_Score(param_2);
            if (score_1 > best)
            {
                out.motion = candidate_1;
                best = score_1;
            }
        }
    }
    return out;
}


struct modernResolveFragmentUniforms
{
    float4 effect[8];
};

struct modernResolveFragment_out
{
    float4 colour [[color(0)]];
};

static inline __attribute__((always_inline))
float3 modernResolveFragment_LinearColour(thread const float3& c)
{
    return mix(c / float3(12.9200000762939453125), powr((c + float3(0.054999999701976776123046875)) / float3(1.05499994754791259765625), float3(2.400000095367431640625)), step(float3(0.040449999272823333740234375), c));
}

static inline __attribute__((always_inline))
float3 modernResolveFragment_Scene(thread const int2& p, constant modernResolveFragmentUniforms& _111, texture2d<float> image0, sampler image0Smplr, texture2d<float> image3, sampler image3Smplr)
{
    if (_111.effect[3].x != 0.0)
    {
        return image0.read(uint2((p - int4(_111.effect[0]).xy)), 0).xyz;
    }
    float3 param = fast::max(image0.read(uint2(p), 0).xyz, image3.read(uint2(p), 0).xyz);
    return modernResolveFragment_LinearColour(param);
}

static inline __attribute__((always_inline))
float modernResolveFragment_Peak(thread const float3& c)
{
    return fast::max(fast::max(c.x, c.y), c.z);
}

static inline __attribute__((always_inline))
float3 modernResolveFragment_Seed(thread int2& p, constant modernResolveFragmentUniforms& _111, texture2d<float> image0, sampler image0Smplr, texture2d<float> image1, sampler image1Smplr)
{
    p = min(p, (int4(_111.effect[0]).zw - int2(1)));
    float3 encoded = image0.read(uint2(p), 0).xyz;
    float2 flags = image1.read(uint2(p), 0).xy;
    float3 param = encoded;
    bool _144 = modernResolveFragment_Peak(param) <= 1.0;
    bool _151;
    if (_144)
    {
        _151 = flags.x == 0.0;
    }
    else
    {
        _151 = _144;
    }
    if (_151)
    {
        return float3(0.0);
    }
    float3 param_1 = fast::max(encoded, float3(0.0));
    float3 c = modernResolveFragment_LinearColour(param_1);
    float3 param_2 = c;
    float peak = modernResolveFragment_Peak(param_2);
    float excess = fast::max(peak - 1.0, 0.0) * smoothstep(1.0, 1.17999994754791259765625, peak);
    float tagged = flags.x * smoothstep(0.100000001490116119384765625, 0.4199999868869781494140625, peak);
    return (c * ((excess / fast::max(peak, 9.9999997473787516355514526367188e-06)) + tagged)) * (1.0 - flags.y);
}

static inline __attribute__((always_inline))
bool modernResolveFragment_InPicture(thread const int2& p, constant modernResolveFragmentUniforms& _111)
{
    bool _327 = all(p >= int2(0));
    bool _342;
    if (_327)
    {
        _342 = all(p < (int4(_111.effect[0]).zw - int4(_111.effect[0]).xy));
    }
    else
    {
        _342 = _327;
    }
    return _342;
}

static inline __attribute__((always_inline))
float modernResolveFragment_SweepCoverage(thread const float2& motion, thread const float2& offset)
{
    float reach = length(motion);
    if (reach < 0.5)
    {
        return 0.0;
    }
    float2 axis = motion / float2(reach);
    float along = dot(axis, offset);
    float across = length(offset - (axis * along));
    return (1.0 - smoothstep(0.449999988079071044921875, 1.35000002384185791015625, across)) * (1.0 - smoothstep(reach, reach + 1.0, abs(along)));
}

static inline __attribute__((always_inline))
bool modernResolveFragment_MotionColour(thread const int2& p, thread float3& scene, constant modernResolveFragmentUniforms& _111, texture2d<float> image0, sampler image0Smplr, texture2d<float> image3, sampler image3Smplr, texture2d<float> image4, sampler image4Smplr, texture2d<float> image5, sampler image5Smplr)
{
    int2 rel = p - int4(_111.effect[0]).xy;
    float4 centre = image4.read(uint2(rel), 0);
    if (centre.w < 0.0)
    {
        return false;
    }
    float2 at = float2(rel) + float2(0.5);
    float3 sum = scene;
    float total = 1.0;
    float reach = length(centre.xy);
    if (reach >= 0.5)
    {
        int steps = clamp(int(ceil(reach)), 3, 12);
        float2 tapStep = centre.xy / float2(float(steps));
        float falloff = 0.60000002384185791015625 / float(steps);
        int _444 = -steps;
        for (int i = _444; i <= steps; i++)
        {
            if (i == 0)
            {
                continue;
            }
            int2 tap = int2(floor(at + (tapStep * float(i))));
            bool _470 = all(tap == rel);
            bool _478;
            if (!_470)
            {
                int2 param = tap;
                _478 = !modernResolveFragment_InPicture(param, _111);
            }
            else
            {
                _478 = _470;
            }
            if (_478)
            {
                continue;
            }
            float4 surface = image4.read(uint2(tap), 0);
            bool _489 = surface.w < 0.0;
            bool _500;
            if (!_489)
            {
                _500 = surface.z > (centre.z * 1.019999980926513671875);
            }
            else
            {
                _500 = _489;
            }
            if (_500)
            {
                continue;
            }
            bool _510 = surface.w == centre.w;
            bool _520;
            if (_510)
            {
                _520 = surface.z >= (centre.z * 0.87999999523162841796875);
            }
            else
            {
                _520 = _510;
            }
            bool same = _520;
            bool _522 = !same;
            bool _532;
            if (_522)
            {
                _532 = surface.z >= (centre.z * 0.980000019073486328125);
            }
            else
            {
                _532 = _522;
            }
            if (_532)
            {
                continue;
            }
            float weight = (1.0 - (falloff * float(abs(i)))) * (same ? 1.0 : 0.64999997615814208984375);
            int2 param_1 = tap + int4(_111.effect[0]).xy;
            sum += (modernResolveFragment_Scene(param_1, _111, image0, image0Smplr, image3, image3Smplr) * weight);
            total += weight;
        }
    }
    float4 nearby = image5.read(uint2((rel / int2(int(_111.effect[2].x)))), 0);
    float nearReach = length(nearby.xy);
    if ((nearby.z > (centre.z * 1.019999980926513671875)) && (nearReach >= 0.5))
    {
        int steps_1 = clamp(int(ceil(nearReach)), 3, 12);
        float2 tapStep_1 = nearby.xy / float2(float(steps_1));
        float falloff_1 = 0.60000002384185791015625 / float(steps_1);
        int _609 = -steps_1;
        for (int i_1 = _609; i_1 <= steps_1; i_1++)
        {
            if (i_1 == 0)
            {
                continue;
            }
            int2 tap_1 = int2(floor(at + (tapStep_1 * float(i_1))));
            bool _635 = all(tap_1 == rel);
            bool _643;
            if (!_635)
            {
                int2 param_2 = tap_1;
                _643 = !modernResolveFragment_InPicture(param_2, _111);
            }
            else
            {
                _643 = _635;
            }
            if (_643)
            {
                continue;
            }
            float4 surface_1 = image4.read(uint2(tap_1), 0);
            bool _654 = surface_1.w < 0.0;
            bool _664;
            if (!_654)
            {
                _664 = surface_1.z <= (centre.z * 1.019999980926513671875);
            }
            else
            {
                _664 = _654;
            }
            if (_664)
            {
                continue;
            }
            float2 param_3 = surface_1.xy;
            float2 param_4 = float2(rel - tap_1);
            float weight_1 = modernResolveFragment_SweepCoverage(param_3, param_4) * (1.0 - (falloff_1 * float(abs(i_1))));
            if (weight_1 <= 0.0)
            {
                continue;
            }
            int2 param_5 = tap_1 + int4(_111.effect[0]).xy;
            sum += (modernResolveFragment_Scene(param_5, _111, image0, image0Smplr, image3, image3Smplr) * weight_1);
            total += weight_1;
        }
    }
    if (total <= 1.0)
    {
        return false;
    }
    scene = sum / float3(total);
    return true;
}

static inline __attribute__((always_inline))
float3 modernResolveFragment_Srgb(thread const float3& c)
{
    return mix(c * 12.9200000762939453125, (powr(c, float3(0.4166666567325592041015625)) * 1.05499994754791259765625) - float3(0.054999999701976776123046875), step(float3(0.003130800090730190277099609375), c));
}

static inline __attribute__((always_inline))
float3 modernResolveFragment_ToneMap(thread const float3& c)
{
    float3 param = c;
    float peak = modernResolveFragment_Peak(param);
    float excess = fast::max(peak - 0.89999997615814208984375, 0.0);
    return c * (1.0 - ((excess / (0.100000001490116119384765625 + excess)) * (excess / fast::max(peak, 9.9999997473787516355514526367188e-06))));
}

static inline __attribute__((always_inline))
float3 modernResolveFragment_ToneMapPbrNeutral(thread float3& c)
{
    float low = fast::min(fast::min(c.x, c.y), c.z);
    float _233;
    if (low < 0.07999999821186065673828125)
    {
        _233 = low * (1.0 - (low / 0.1599999964237213134765625));
    }
    else
    {
        _233 = 0.039999999105930328369140625;
    }
    float offset = _233;
    c -= float3(offset);
    float3 param = c;
    float peak = modernResolveFragment_Peak(param);
    if (peak <= 0.7599999904632568359375)
    {
        return c;
    }
    float mappedPeak = 1.0 - (0.057599999010562896728515625 / (0.23999999463558197021484375 + (peak - 0.7599999904632568359375)));
    float desaturation = 0.1500000059604644775390625 * (peak - mappedPeak);
    return mix(c * (mappedPeak / peak), float3(mappedPeak), float3(desaturation / (1.0 + desaturation)));
}

fragment modernResolveFragment_out modernResolveFragment(constant modernResolveFragmentUniforms& _111 [[buffer(3)]], texture2d<float> image0 [[texture(0)]], texture2d<float> image1 [[texture(1)]], texture2d<float> image2 [[texture(2)]], texture2d<float> image3 [[texture(3)]], texture2d<float> image4 [[texture(4)]], texture2d<float> image5 [[texture(5)]], sampler image0Smplr [[sampler(0)]], sampler image1Smplr [[sampler(1)]], sampler image2Smplr [[sampler(2)]], sampler image3Smplr [[sampler(3)]], sampler image4Smplr [[sampler(4)]], sampler image5Smplr [[sampler(5)]], float4 gl_FragCoord [[position]])
{
    modernResolveFragment_out out = {};
    if (int(_111.effect[1].z) == 4)
    {
        int2 param = int4(_111.effect[0]).xy + int2(gl_FragCoord.xy);
        out.colour = float4(modernResolveFragment_Scene(param, _111, image0, image0Smplr, image3, image3Smplr), 1.0);
        return out;
    }
    if (int(_111.effect[1].z) == 1)
    {
        int2 p = int4(_111.effect[0]).xy + (int2(int(_111.effect[1].w)) * int2(gl_FragCoord.xy));
        float3 sum = float3(0.0);
        for (int y = 0; y < int(_111.effect[1].w); y++)
        {
            for (int x = 0; x < int(_111.effect[1].w); x++)
            {
                int2 param_1 = p + int2(x, y);
                float3 _794 = modernResolveFragment_Seed(param_1, _111, image0, image0Smplr, image1, image1Smplr);
                sum += _794;
            }
        }
        out.colour = float4(sum / float3(float(int(_111.effect[1].w) * int(_111.effect[1].w))), 1.0);
    }
    else
    {
        if (int(_111.effect[1].z) == 2)
        {
            float2 uv = gl_FragCoord.xy / float2(int2(image0.get_width(), image0.get_height()));
            float3 sum_1 = image0.sample(image0Smplr, uv).xyz * 0.227026998996734619140625;
            sum_1 += ((image0.sample(image0Smplr, (uv - (_111.effect[1].xy * 1.3846149444580078125))).xyz + image0.sample(image0Smplr, (uv + (_111.effect[1].xy * 1.3846149444580078125))).xyz) * 0.316215991973876953125);
            sum_1 += ((image0.sample(image0Smplr, (uv - (_111.effect[1].xy * 3.2307689189910888671875))).xyz + image0.sample(image0Smplr, (uv + (_111.effect[1].xy * 3.2307689189910888671875))).xyz) * 0.070270001888275146484375);
            out.colour = float4(sum_1, 1.0);
        }
        else
        {
            int2 rel = clamp(int2(gl_FragCoord.xy) - int2(1), int2(0), (int4(_111.effect[0]).zw - int4(_111.effect[0]).xy) - int2(1));
            int2 p_1 = int4(_111.effect[0]).xy + rel;
            float4 original = image3.read(uint2(p_1), 0);
            float overlay = image1.read(uint2(p_1), 0).y;
            int2 param_2 = p_1;
            float3 scene = modernResolveFragment_Scene(param_2, _111, image0, image0Smplr, image3, image3Smplr);
            bool mixed = false;
            if (int(_111.effect[2].x) > 0)
            {
                int2 param_3 = p_1;
                float3 param_4 = scene;
                bool _943 = modernResolveFragment_MotionColour(param_3, param_4, _111, image0, image0Smplr, image3, image3Smplr, image4, image4Smplr, image5, image5Smplr);
                scene = param_4;
                mixed = _943;
            }
            if (!(_111.effect[2].w != 0.0))
            {
                float4 _952;
                if (mixed)
                {
                    float3 param_5 = scene;
                    _952 = float4(modernResolveFragment_Srgb(param_5), original.w);
                }
                else
                {
                    _952 = original;
                }
                out.colour = _952;
                return out;
            }
            float2 glowSize = float2(int2(image2.get_width(), image2.get_height())) * float(int(_111.effect[1].w));
            float3 glow = image2.sample(image2Smplr, ((float2(rel) + float2(0.5)) / glowSize)).xyz;
            float3 _994;
            if (_111.effect[2].z != 0.0)
            {
                _994 = glow * 0.25;
            }
            else
            {
                _994 = float3(0.0);
            }
            float3 hdr = scene + _994;
            float3 param_6 = hdr;
            float3 mapped = modernResolveFragment_ToneMap(param_6);
            if (_111.effect[2].y != 0.0)
            {
                float3 param_7 = hdr;
                float weight = smoothstep(0.1500000059604644775390625, 0.75, modernResolveFragment_Peak(param_7));
                float3 param_8 = hdr;
                float3 _1021 = modernResolveFragment_ToneMapPbrNeutral(param_8);
                mapped = mix(mapped, _1021, float3(weight * weight));
            }
            float3 param_9 = mapped;
            out.colour = float4(mix(modernResolveFragment_Srgb(param_9), original.xyz, float3(overlay)), original.w);
        }
    }
    return out;
}
