#include "gl_pipeline.h"
#include <stdlib.h>
#include <string.h>
#include "shaders/bilinear.h"

#define RENDER_FRAGMENT_UNIFORMS "uniform float worldBrightness;\n"
#define RENDER_FILTER_BOUNDS \
    "    if ((mode.y & 64) == 0 && filterBounds.z >= filterBounds.x)\n" \
    "        return clamp(p, filterBounds.xy, filterBounds.zw);\n"
#define RENDER_VERTEX_COLOUR "    return c * ((mode.y & 2) != 0 ? worldBrightness : 1.0);\n"

/* Decode the composited scene to linear light, extending sRGB above white. */
#define LINEAR_COLOUR \
    "vec3 LinearColour(vec3 c) {\n" \
    "    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));\n" \
    "}\n"
/* Flat geometric normals keep the original faceted models. Evaluate derivatives
   before texture coverage branches; add light in linear space, then return to
   the encoded colour used by the console's blend operations and HDR transport.
   Light from the shield wraps around the hull, with more light at grazing angles. */
#define RENDER_LIGHTING \
    LINEAR_COLOUR \
    "flat in float receivesLight;\n" \
    "uniform int lightCount;\n" \
    "uniform vec4 lightPosition[16], lightColour[16];\n" \
    "uniform vec4 shieldGlow;\n" \
    "vec3 illumination;\n" \
    "void SurfaceLighting() {\n" \
    "    vec3 normal = cross(dFdx(vViewPosition), dFdy(vViewPosition));\n" \
    "    float length2 = dot(normal, normal);\n" \
    "    illumination = vec3(0.0);\n" \
    "    if (receivesLight < 0.5 || (lightCount == 0 && shieldGlow.a == 0.0) || length2 < 0.00000001 || mode.z == 3) return;\n" \
    "    normal *= inversesqrt(length2);\n" \
    "    if (dot(normal, vViewPosition) > 0.0) normal = -normal;\n" \
    "    if (shieldGlow.a > 0.0) {\n" \
    "        vec3 viewDir = -vViewPosition * inversesqrt(max(dot(vViewPosition, vViewPosition), 1.0));\n" \
    "        float rim = 1.0 - clamp(dot(normal, viewDir), 0.0, 1.0);\n" \
    "        vec3 tint = mix(vec3(1.0), shieldGlow.rgb, 0.72);\n" \
    "        illumination += tint * shieldGlow.a * (0.35 + 0.95 * rim * rim);\n" \
    "    }\n" \
    "    for (int i = 0; i < 16; i++) {\n" \
    "        if (i >= lightCount) break;\n" \
    "        vec3 delta = lightPosition[i].xyz - vViewPosition;\n" \
    "        float distance2 = dot(delta, delta);\n" \
    "        float radius2 = lightPosition[i].w * lightPosition[i].w;\n" \
    "        float falloff = max(1.0 - distance2 / radius2, 0.0);\n" \
    "        float diffuse = max(dot(normal, delta) * inversesqrt(max(distance2, 1.0)), 0.0);\n" \
    "        illumination += lightColour[i].rgb * lightColour[i].w * diffuse * falloff * falloff;\n" \
    "    }\n" \
    "}\n" \
    "vec3 LightMaterial(vec3 colour, vec3 albedo, float coverage) {\n" \
    "    if (coverage <= 0.0 || all(equal(illumination, vec3(0.0)))) return colour;\n" \
    "    vec3 lit = LinearColour(max(colour / coverage, 0.0)) +\n" \
    "               LinearColour(max(albedo / coverage, 0.0)) * illumination;\n" \
    "    return mix(lit * 12.92, 1.055 * pow(lit, vec3(1.0 / 2.4)) - 0.055,\n" \
    "               step(vec3(0.0031308), lit)) * coverage;\n" \
    "}\n"
#define RENDER_AUX_FLAGS
#define RENDER_AUX_COLOUR \
    "uniform bool hdrActive;\n" \
    "uniform float sparkGain;\n" \
    "flat in int auxFlags;\n" \
    "flat in ivec4 emissionUv;\n" \
    "layout(location = 1) out vec4 hdrColour;\n" \
    "layout(location = 2) out vec4 hdrMask;\n" \
    "layout(location = 3) out vec4 hullVelocity;\n" \
    "in vec3 vViewPosition;\n" \
    "in vec3 shutterDelta;\n" \
    "bool BoostSpark() { return (auxFlags & 8) != 0 && sparkGain > 1.0; }\n" \
    "vec3 HdrMaterial(vec3 rgb, float coverage) {\n" \
    "    if (BoostSpark()) return rgb * sparkGain;\n" \
    "    return (auxFlags & 2) != 0 ? min(rgb, vec3(coverage)) : rgb;\n" \
    "}\n" \
    "void WriteAux(vec4 c, float covered) {\n" \
    /* A spark without a precise projection still belongs to the scene. */ \
    "    bool overlay = (auxFlags & 2) != 0 && !BoostSpark();\n" \
    "    bool emits = (auxFlags & 1) != 0;\n" \
    "    if (emissionUv.z > 0) {\n" \
    "        vec2 t = vTexel.xy / vTexel.z;\n" \
    "        emits = emits && all(lessThan(abs(t - vec2(emissionUv.xy)), vec2(emissionUv.zw)));\n" \
    "    }\n" \
    "    hdrColour = c;\n" \
    "    bool hull = (auxFlags & 4) != 0;\n" \
    "    float guard = (overlay || (hull && c.a < 0.999)) && covered > 0.0 ? 1.0 : 0.0;\n" \
    "    vec3 past = vViewPosition + shutterDelta;\n" \
    "    vec2 velocity = vec2(0.0);\n" \
    "    if (hull && vViewPosition.z > 1.0 && past.z > 1.0)\n" \
    "        velocity = past.xy / past.z - vViewPosition.xy / vViewPosition.z;\n" \
    "    hullVelocity = vec4(vec3(velocity, hull ? 1.0 : 0.0) * c.a, c.a);\n" \
    "    hdrMask = vec4(emits && !overlay ? c.a : 0.0, overlay ? c.a : 0.0,\n" \
    "                   mode.z == 2 ? 0.0 : guard, mode.z == 2 ? guard : c.a);\n" \
    "}\n"

static const char *modernVertexSource =
#include "shaders/scene_vertex.h"
;
static const char *modernFragmentSource =
#include "shaders/material_fragment.h"
;
static RenderProgram modernProgram;
static GLint uniformWorldBrightness, uniformHdrActive, uniformSparkGain;
static GLint uniformLightCount, uniformLightPosition, uniformLightColour, uniformShieldGlow;

static void Lights(const void *profile, const GpuLightSet *lights, const float shieldGlow[4])
{
    const ModernRenderSettings *settings = profile;
    int count = settings->weaponLights && lights ? SDL_clamp(lights->count, 0, GPU_POINT_LIGHTS) : 0;
    float position[GPU_POINT_LIGHTS][4], colour[GPU_POINT_LIGHTS][4];
    static const float noGlow[4];
    for (int i = 0; i < count; i++) {
        const GpuPointLight *light = &lights->lights[i];
        for (int k = 0; k < 3; k++) {
            position[i][k] = light->position[k];
            colour[i][k] = light->colour[k];
        }
        position[i][3] = SDL_max(light->radius, 1.0f);
        colour[i][3] = light->intensity;
    }
    glUniform1i(uniformLightCount, count);
    glUniform4fv(uniformShieldGlow, 1, settings->weaponLights ? shieldGlow : noGlow);
    if (count) {
        glUniform4fv(uniformLightPosition, count, &position[0][0]);
        glUniform4fv(uniformLightColour, count, &colour[0][0]);
    }
}

static void Bind(const void *profile, int hdrActive)
{
    const ModernRenderSettings *settings = profile;
    glUniform1f(uniformWorldBrightness, settings->brightness / (float)MODERN_BRIGHTNESS_MAX);
    glUniform1i(uniformHdrActive, hdrActive);
    glUniform1f(uniformSparkGain, settings->bloom ? 8.0f : 1.0f);
}

static RenderProgram *ModernScene(void)
{
    if (!modernProgram.program) {
        if (!RenderGlProgramInit(&modernProgram, modernVertexSource, modernFragmentSource)) abort();
        GLuint program = modernProgram.program;
        uniformWorldBrightness = glGetUniformLocation(program, "worldBrightness");
        uniformHdrActive = glGetUniformLocation(program, "hdrActive");
        uniformSparkGain = glGetUniformLocation(program, "sparkGain");
        uniformLightCount = glGetUniformLocation(program, "lightCount");
        uniformLightPosition = glGetUniformLocation(program, "lightPosition");
        uniformLightColour = glGetUniformLocation(program, "lightColour");
        uniformShieldGlow = glGetUniformLocation(program, "shieldGlow");
        glUniform1f(uniformWorldBrightness, 1.0f);
    }
    return &modernProgram;
}

static const char *fullscreenVertexSource =
    "out vec2 uv;\n"
    "void main() {\n"
    "    uv = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

static const char *motionFragmentSource =
    "#ifdef GL_ES\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "precision highp sampler2D;\n"
    "#endif\n"
    "uniform sampler2D mask, depth, velocity, field;\n"
    "uniform ivec4 crop, depthRect;\n"
    "uniform vec4 projection, earlier[3];\n"
    "uniform int pass, tileSize;\n"
    "uniform float amount, maxRadius;\n"
    "out vec4 motion;\n"
    "vec4 Field(ivec2 p) {\n"
    "    vec2 guard = texelFetch(mask, p, 0).ba;\n"
    "    vec3 objectMotion = texelFetch(velocity, p, 0).xyz;\n"
    "    float reciprocal = 1.0 - texelFetch(depth, p - depthRect.xy, 0).r;\n"
    "    bool hull = objectMotion.z > 0.0;\n"
    "    if (guard.x > 0.0 || guard.y < 1.0 || (hull && objectMotion.z < 0.999))\n"
    "        return vec4(0.0, 0.0, reciprocal, -1.0);\n"
    "    if (reciprocal <= 0.0) return vec4(0.0);\n"
    "    vec2 current = vec2(p) + 0.5;\n"
    "    vec2 travel;\n"
    "    if (hull) {\n"
    "        travel = objectMotion.xy * projection.zw;\n"
    "    } else {\n"
    "        float z = 1.0 / reciprocal;\n"
    "        vec4 point = vec4((current - projection.xy) * z / projection.zw, z, 1.0);\n"
    "        vec3 past = vec3(dot(earlier[0], point), dot(earlier[1], point), dot(earlier[2], point));\n"
    "        travel = past.z > 1.0 ? projection.xy + past.xy * projection.zw / past.z - current : vec2(0.0);\n"
    "    }\n"
    "    float radius = length(travel);\n"
    "    travel *= min(amount * 0.5, maxRadius / max(radius, 0.00001));\n"
    "    return vec4(travel, reciprocal, hull ? 1.0 : 0.0);\n"
    "}\n"
    "float Score(vec4 candidate) {\n"
    "    return dot(candidate.xy, candidate.xy) * candidate.z;\n"
    "}\n"
    "void main() {\n"
    "    ivec2 at = ivec2(gl_FragCoord.xy);\n"
    "    if (pass == 0) {\n"
    "        motion = Field(at + crop.xy);\n"
    "        return;\n"
    "    }\n"
    /* Nearer motion with a larger footprint gets priority when choosing the
       direction to search. The resolve still checks each actual tap. */
    "    ivec2 size = textureSize(field, 0);\n"
    "    motion = vec4(0.0);\n"
    "    float best = 0.0;\n"
    /* Reduce all pixels in fixed-size blocks before scanning the variable tiles. */
    "    if (pass == 1) {\n"
    "        ivec2 base = at * 4;\n"
    "        for (int y = 0; y < 4; ++y) {\n"
    "            for (int x = 0; x < 4; ++x) {\n"
    "                vec4 candidate = texelFetch(field, min(base + ivec2(x, y), size - 1), 0);\n"
    "                float score = Score(candidate);\n"
    "                if (score > best) { motion = candidate; best = score; }\n"
    "            }\n"
    "        }\n"
    "        return;\n"
    "    }\n"
    "    int blocks = tileSize / 4;\n"
    "    ivec2 lo = pass == 2 ? at * blocks : max(at - 1, ivec2(0));\n"
    "    ivec2 hi = min(pass == 2 ? lo + blocks : at + 2, size);\n"
    "    for (int y = lo.y; y < hi.y; ++y) {\n"
    "        for (int x = lo.x; x < hi.x; ++x) {\n"
    "            vec4 candidate = texelFetch(field, ivec2(x, y), 0);\n"
    "            float score = Score(candidate);\n"
    "            if (score > best) {\n"
    "                motion = candidate;\n"
    "                best = score;\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "}\n";

enum { MOTION_FIELD, MOTION_BLOCKS, MOTION_TILES, MOTION_NEARBY, MOTION_TARGETS };
static RenderProgram motionProgram;
static GLuint motionArray, motionTextures[MOTION_TARGETS], motionTargets[MOTION_TARGETS];
static GLint motionCrop, motionDepthRect, motionProjection, motionEarlier, motionAmount, motionRadius, motionTile;
static int motionWidth, motionHeight, motionTileSize, motionReady, motionCachedAmount;
static RenderPictureKey motionCache;

static void MotionInit(void)
{
    if (motionProgram.program) return;
    if (!RenderGlProgramInit(&motionProgram, fullscreenVertexSource, motionFragmentSource)) abort();
    GLuint program = motionProgram.program;
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "mask"), 0);
    glUniform1i(glGetUniformLocation(program, "depth"), 1);
    glUniform1i(glGetUniformLocation(program, "velocity"), 2);
    glUniform1i(glGetUniformLocation(program, "field"), 3);
    motionCrop = glGetUniformLocation(program, "crop");
    motionDepthRect = glGetUniformLocation(program, "depthRect");
    motionProjection = glGetUniformLocation(program, "projection");
    motionEarlier = glGetUniformLocation(program, "earlier[0]");
    motionAmount = glGetUniformLocation(program, "amount");
    motionRadius = glGetUniformLocation(program, "maxRadius");
    motionTile = glGetUniformLocation(program, "tileSize");
    glGenVertexArrays(1, &motionArray);
}

static int MotionPicture(const RenderPresent *p, int amountPercent)
{
    const RenderMotionPicture *motion = p->motion;
    int width = p->right - p->left, height = p->bottom - p->top;
    float amount = 1.5f * (amountPercent / 100.0f);
    float radius = 0.007f * height * amount;
    /* Round up so tiles still cover the full search radius and contain whole blocks. */
    int tile = 4 * SDL_max(1, (int)SDL_ceilf(radius / 4));
    int sizes[MOTION_TARGETS][2] = {
        {width, height},
        {(width + 3) / 4, (height + 3) / 4},
        {(width + tile - 1) / tile, (height + tile - 1) / tile},
        {(width + tile - 1) / tile, (height + tile - 1) / tile}
    };
    if (motionReady && motionTileSize == tile && motionCachedAmount == amountPercent &&
        RenderGlPictureMatches(&motionCache, p, motion->depth)) return 1;
    MotionInit();
    RenderGlBeginFullscreen(motionProgram.program, motionArray);
    glActiveTexture(GL_TEXTURE0);
    int resizeField = width != motionWidth || height != motionHeight || !motionReady;
    if (width != motionWidth || height != motionHeight || tile != motionTileSize) {
        motionWidth = width;
        motionHeight = height;
        motionTileSize = tile;
        motionCache = (RenderPictureKey){0};
        motionReady = 1;
        for (int i = resizeField ? MOTION_FIELD : MOTION_TILES; i < MOTION_TARGETS && motionReady; i++) {
            motionReady = RenderGlResizeTarget(&motionTextures[i], &motionTargets[i],
                sizes[i][0], sizes[i][1], GL_RGBA16F, GL_HALF_FLOAT, GL_NEAREST);
        }
    }
    if (!motionReady) return 0;

    glBindTexture(GL_TEXTURE_2D, p->hdrMask);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, motion->depth);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, motion->velocity);
    glActiveTexture(GL_TEXTURE3);
    /* Bind a separate texture during the field pass to avoid framebuffer feedback. */
    glBindTexture(GL_TEXTURE_2D, p->hdrMask);
    glUniform4i(motionCrop, p->left, p->top, p->right, p->bottom);
    glUniform4i(motionDepthRect, motion->left, motion->top, motion->width, motion->height);
    glUniform4fv(motionProjection, 1, motion->projection);
    glUniform4fv(motionEarlier, 3, &motion->earlier[0][0]);
    glUniform1f(motionAmount, amount);
    glUniform1f(motionRadius, radius);
    glUniform1i(motionTile, tile);
    for (int i = 0; i < MOTION_TARGETS; i++) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, motionTargets[i]);
        glViewport(0, 0, sizes[i][0], sizes[i][1]);
        glUniform1i(motionProgram.uniformPass, i);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindTexture(GL_TEXTURE_2D, motionTextures[i]);
    }
    glActiveTexture(GL_TEXTURE0);
    motionCache = RenderGlPictureKey(p, motion->depth);
    motionCachedAmount = amountPercent;
    return 1;
}

static const char *bloomFragmentSource =
    "#ifdef GL_ES\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "precision highp sampler2D;\n"
    "#endif\n"
    "uniform sampler2D source, mask, blurred, sdr, motionField, motionNearby;\n"
    "uniform ivec4 crop;\n"
    "uniform vec2 stepSize;\n"
    "uniform int pass, downsample, motionTileSize;\n"
    "uniform bool softShadows, bloomEnabled, hdrEnabled;\n"
    "in vec2 uv;\n"
    "layout(location = 0) out vec4 colour;\n"
    "layout(location = 1) out vec4 coverage;\n"
    LINEAR_COLOUR
    "vec3 Srgb(vec3 c) {\n"
    "    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));\n"
    "}\n"
    "float Peak(vec3 c) { return max(max(c.r, c.g), c.b); }\n"
    "vec3 Seed(ivec2 p) {\n"
    "    p = min(p, crop.zw - 1);\n"
    "    vec3 encoded = texelFetch(source, p, 0).rgb;\n"
    "    vec2 flags = texelFetch(mask, p, 0).rg;\n"
    "    if (Peak(encoded) <= 1.0 && flags.r == 0.0) return vec3(0.0);\n"
    "    vec3 c = LinearColour(max(encoded, 0.0));\n"
    "    float peak = Peak(c);\n"
    "    float excess = max(peak - 1.0, 0.0) * smoothstep(1.0, 1.18, peak);\n"
    "    float tagged = flags.r * smoothstep(0.10, 0.42, peak);\n"
    "    return c * (excess / max(peak, 0.00001) + tagged) * (1.0 - flags.g);\n"
    "}\n"
    /* Preserve the SDR midtones and compress highlights together to retain hue. */
    "vec3 ToneMap(vec3 c) {\n"
    "    float peak = Peak(c);\n"
    "    float excess = max(peak - 0.9, 0.0);\n"
    "    return c * (1.0 - (excess / (0.1 + excess)) * (excess / max(peak, 0.00001)));\n"
    "}\n"
    /* https://github.com/KhronosGroup/ToneMapping/tree/main/PBR_Neutral */
    "vec3 ToneMapPbrNeutral(vec3 c) {\n"
    "    float low = min(min(c.r, c.g), c.b);\n"
    "    float offset = low < 0.08 ? low * (1.0 - low / 0.16) : 0.04;\n"
    "    c -= offset;\n"
    "    const float knee = 0.76;\n"
    "    float peak = Peak(c);\n"
    "    if (peak <= knee) return c;\n"
    "    const float headroom = 1.0 - knee;\n"
    "    float mappedPeak = 1.0 - headroom * headroom / (headroom + (peak - knee));\n"
    "    float desaturation = 0.15 * (peak - mappedPeak);\n"
    "    return mix(c * (mappedPeak / peak), vec3(mappedPeak), desaturation / (1.0 + desaturation));\n"
    "}\n"
    "vec3 Scene(ivec2 p) {\n"
    /* Restore the SDR floor where the console clamped a subtractive blend. */
    "    return LinearColour(max(texelFetch(source, p, 0).rgb, texelFetch(sdr, p, 0).rgb));\n"
    "}\n"
    "bool InPicture(ivec2 p) {\n"
    "    return all(greaterThanEqual(p, ivec2(0))) && all(lessThan(p, crop.zw - crop.xy));\n"
    "}\n"
    "float SweepCoverage(vec2 motion, vec2 offset) {\n"
    "    float reach = length(motion);\n"
    "    if (reach < 0.5) return 0.0;\n"
    "    vec2 axis = motion / reach;\n"
    "    float along = dot(axis, offset);\n"
    "    float across = length(offset - axis * along);\n"
    "    return (1.0 - smoothstep(0.45, 1.35, across)) * (1.0 - smoothstep(reach, reach + 1.0, abs(along)));\n"
    "}\n"
    "bool MotionColour(ivec2 p, inout vec3 scene) {\n"
    "    ivec2 rel = p - crop.xy;\n"
    "    vec4 centre = texelFetch(motionField, rel, 0);\n"
    "    if (centre.w < 0.0) return false;\n"
    "    vec2 at = vec2(rel) + 0.5;\n"
    "    vec3 sum = scene;\n"
    "    float total = 1.0;\n"
    /* Along the centre's motion, gather its surface and any background that
       the moving edge exposes. Nearer objects are gathered separately. */
    "    float reach = length(centre.xy);\n"
    "    if (reach >= 0.5) {\n"
    "        int steps = clamp(int(ceil(reach)), 3, 12);\n"
    "        vec2 tapStep = centre.xy / float(steps);\n"
    "        float falloff = 0.6 / float(steps);\n"
    "        for (int i = -steps; i <= steps; ++i) {\n"
    "            if (i == 0) continue;\n"
    "            ivec2 tap = ivec2(floor(at + tapStep * float(i)));\n"
    "            if (all(equal(tap, rel)) || !InPicture(tap)) continue;\n"
    "            vec4 surface = texelFetch(motionField, tap, 0);\n"
    "            if (surface.w < 0.0 || surface.z > centre.z * 1.02) continue;\n"
    "            bool same = surface.w == centre.w && surface.z >= centre.z * 0.88;\n"
    "            if (!same && surface.z >= centre.z * 0.98) continue;\n"
    "            float weight = (1.0 - falloff * float(abs(i))) * (same ? 1.0 : 0.65);\n"
    "            sum += Scene(tap + crop.xy) * weight;\n"
    "            total += weight;\n"
    "        }\n"
    "    }\n"
    /* Nearby motion proposes a search direction. Each actual foreground tap
       must be closer and its own swept segment must reach the output pixel. */
    "    vec4 nearby = texelFetch(motionNearby, rel / motionTileSize, 0);\n"
    "    float nearReach = length(nearby.xy);\n"
    "    if (nearby.z > centre.z * 1.02 && nearReach >= 0.5) {\n"
    "        int steps = clamp(int(ceil(nearReach)), 3, 12);\n"
    "        vec2 tapStep = nearby.xy / float(steps);\n"
    "        float falloff = 0.6 / float(steps);\n"
    "        for (int i = -steps; i <= steps; ++i) {\n"
    "            if (i == 0) continue;\n"
    "            ivec2 tap = ivec2(floor(at + tapStep * float(i)));\n"
    "            if (all(equal(tap, rel)) || !InPicture(tap)) continue;\n"
    "            vec4 surface = texelFetch(motionField, tap, 0);\n"
    "            if (surface.w < 0.0 || surface.z <= centre.z * 1.02) continue;\n"
    "            float weight = SweepCoverage(surface.xy, vec2(rel - tap)) * (1.0 - falloff * float(abs(i)));\n"
    "            if (weight <= 0.0) continue;\n"
    "            sum += Scene(tap + crop.xy) * weight;\n"
    "            total += weight;\n"
    "        }\n"
    "    }\n"
    "    if (total <= 1.0) return false;\n"
    "    scene = sum / total;\n"
    "    return true;\n"
    "}\n"
    "void main() {\n"
    "    coverage = vec4(0.0);\n"
    "    if (pass == 1) {\n"
    "        ivec2 p = crop.xy + downsample * ivec2(gl_FragCoord.xy);\n"
    "        vec3 sum = vec3(0.0);\n"
    "        for (int y = 0; y < downsample; ++y)\n"
    "            for (int x = 0; x < downsample; ++x)\n"
    "                sum += Seed(p + ivec2(x, y));\n"
    "        colour = vec4(sum / float(downsample * downsample), 1.0);\n"
    "    } else if (pass == 2) {\n"
    "        vec3 sum = texture(source, uv).rgb * 0.227027;\n"
    "        sum += (texture(source, uv - stepSize * 1.384615).rgb + texture(source, uv + stepSize * 1.384615).rgb) * 0.316216;\n"
    "        sum += (texture(source, uv - stepSize * 3.230769).rgb + texture(source, uv + stepSize * 3.230769).rgb) * 0.070270;\n"
    "        colour = vec4(sum, 1.0);\n"
    "    } else {\n"
    /* Pad the resolved crop for linear resampling at its edges. */
    "        ivec2 rel = clamp(ivec2(gl_FragCoord.xy) - 1, ivec2(0), crop.zw - crop.xy - 1);\n"
    "        ivec2 p = crop.xy + rel;\n"
    "        vec4 original = texelFetch(sdr, p, 0);\n"
    "        float overlay = texelFetch(mask, p, 0).g;\n"
    "        vec3 scene = Scene(p);\n"
    "        bool mixed = false;\n"
    "        if (motionTileSize > 0) mixed = MotionColour(p, scene);\n"
    "        if (!hdrEnabled) {\n"
    "            colour = mixed ? vec4(Srgb(scene), original.a) : original;\n"
    "            return;\n"
    "        }\n"
    "        vec2 glowSize = float(downsample) * vec2(textureSize(blurred, 0));\n"
    "        vec3 glow = texture(blurred, (vec2(rel) + 0.5) / glowSize).rgb;\n"
    "        vec3 hdr = scene + (bloomEnabled ? glow * 0.25 : vec3(0.0));\n"
    "        vec3 mapped = ToneMap(hdr);\n"
    /* Keep more of the original midtone colour as the blend approaches the highlights. */
    "        if (softShadows) {\n"
    "            float weight = smoothstep(0.15, 0.75, Peak(hdr));\n"
    "            mapped = mix(mapped, ToneMapPbrNeutral(hdr), weight * weight);\n"
    "        }\n"
    "        colour = vec4(mix(Srgb(mapped), original.rgb, overlay), original.a);\n"
    "    }\n"
    "}\n";

enum { BLOOM_EXTRACT = 1, BLOOM_BLUR, BLOOM_RESOLVE };
enum { BLOOM_COMPOSITE = 2, BLOOM_TARGETS = 3 };
static RenderProgram bloomProgram;
static GLuint bloomArray, bloomTextures[BLOOM_TARGETS], bloomTargets[BLOOM_TARGETS];
static GLint bloomCrop, bloomStep, bloomDownsample, bloomSoftShadows, bloomEnabled, bloomHdr, bloomMotionTile;
static int bloomWidth, bloomHeight, bloomDivisor, bloomReady;
static struct {
    RenderPictureKey picture;
    int tonemapper, enabled, hdr, motionAmount;
} bloomCache;

static void BloomInit(void)
{
    if (bloomProgram.program) return;
    if (!RenderGlProgramInit(&bloomProgram, fullscreenVertexSource, bloomFragmentSource)) abort();
    GLuint program = bloomProgram.program;
    glUniform1i(glGetUniformLocation(program, "source"), 0);
    glUniform1i(glGetUniformLocation(program, "mask"), 1);
    glUniform1i(glGetUniformLocation(program, "blurred"), 2);
    glUniform1i(glGetUniformLocation(program, "sdr"), 3);
    glUniform1i(glGetUniformLocation(program, "motionField"), 4);
    glUniform1i(glGetUniformLocation(program, "motionNearby"), 5);
    bloomCrop = glGetUniformLocation(program, "crop");
    bloomStep = glGetUniformLocation(program, "stepSize");
    bloomDownsample = glGetUniformLocation(program, "downsample");
    bloomSoftShadows = glGetUniformLocation(program, "softShadows");
    bloomEnabled = glGetUniformLocation(program, "bloomEnabled");
    bloomHdr = glGetUniformLocation(program, "hdrEnabled");
    bloomMotionTile = glGetUniformLocation(program, "motionTileSize");
    glGenVertexArrays(1, &bloomArray);
}

static int BloomResize(int w, int h, int divisor)
{
    if (w == bloomWidth && h == bloomHeight && divisor == bloomDivisor) return bloomReady;
    bloomWidth = w;
    bloomHeight = h;
    bloomDivisor = divisor;
    memset(&bloomCache, 0, sizeof(bloomCache));
    GLint maxSize;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
    if (w > maxSize - 2 || h > maxSize - 2) return bloomReady = 0;
    for (int i = 0; i < BLOOM_TARGETS; i++) {
        int composite = i == BLOOM_COMPOSITE;
        int width = composite ? w + 2 : (w + divisor - 1) / divisor;
        int height = composite ? h + 2 : (h + divisor - 1) / divisor;
        if (!RenderGlResizeTarget(&bloomTextures[i], &bloomTargets[i], width, height,
                                  composite ? GL_RGBA8 : GL_RGBA16F,
                                  composite ? GL_UNSIGNED_BYTE : GL_HALF_FLOAT, GL_LINEAR)) {
            glDeleteTextures(BLOOM_TARGETS, bloomTextures);
            glDeleteFramebuffers(BLOOM_TARGETS, bloomTargets);
            memset(bloomTextures, 0, sizeof(bloomTextures));
            memset(bloomTargets, 0, sizeof(bloomTargets));
            SDL_Log("Modern bloom targets unavailable at %dx%d", w, h);
            return bloomReady = 0;
        }
    }
    return bloomReady = 1;
}

static void ResolvePicture(const RenderPresent *p, int tonemapper, int enabled, int hdr, int motionAmount)
{
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, motionAmount ? motionTextures[MOTION_NEARBY] : p->texture);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, motionAmount ? motionTextures[MOTION_FIELD] : p->texture);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, p->texture);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, p->texture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, p->hdrMask);
    glActiveTexture(GL_TEXTURE0);
    glUniform4i(bloomCrop, p->left, p->top, p->right, p->bottom);
    glUniform1i(bloomDownsample, bloomDivisor);

    int w = (bloomWidth + bloomDivisor - 1) / bloomDivisor;
    int h = (bloomHeight + bloomDivisor - 1) / bloomDivisor;
    if (enabled) {
        glViewport(0, 0, w, h);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, bloomTargets[0]);
        glBindTexture(GL_TEXTURE_2D, p->hdrTexture);
        glUniform1i(bloomProgram.uniformPass, BLOOM_EXTRACT);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        /* The bilinear tap pairs reconstruct a contiguous nine-texel kernel. */
        glUniform1i(bloomProgram.uniformPass, BLOOM_BLUR);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, bloomTargets[1]);
        glBindTexture(GL_TEXTURE_2D, bloomTextures[0]);
        glUniform2f(bloomStep, 1.0f / w, 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, bloomTargets[0]);
        glBindTexture(GL_TEXTURE_2D, bloomTextures[1]);
        glUniform2f(bloomStep, 0.0f, 1.0f / h);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, bloomTargets[BLOOM_COMPOSITE]);
    glViewport(0, 0, bloomWidth + 2, bloomHeight + 2);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, bloomTextures[0]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, p->hdrTexture);
    glUniform1i(bloomProgram.uniformPass, BLOOM_RESOLVE);
    glUniform1i(bloomSoftShadows, tonemapper == MODERN_TONEMAP_SOFT_SHADOWS);
    glUniform1i(bloomEnabled, enabled);
    glUniform1i(bloomHdr, hdr);
    glUniform1i(bloomMotionTile, motionAmount ? motionTileSize : 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    bloomCache.picture = RenderGlPictureKey(p, p->hdrTexture);
    bloomCache.tonemapper = tonemapper;
    bloomCache.enabled = enabled;
    bloomCache.hdr = hdr;
    bloomCache.motionAmount = motionAmount;
}

static void ModernPresent(const void *profile, const RenderPresent *p)
{
    const ModernRenderSettings *settings = profile;
    GLenum filter = settings->resample ? GL_LINEAR : GL_NEAREST;
    int h = p->bottom - p->top;
    int hdr = (settings->bloom || settings->weaponLights) && p->hdrTexture;
    int blur = settings->cameraBlur && settings->cameraBlurAmount > 0 && p->motion && p->hdrMask;
    if ((!hdr && !blur) || !p->race || p->right <= p->left || h <= 0 ||
        p->x1 <= p->x0 || p->y0 == p->y1) {
        RenderGlPresentDirect(p, filter);
        return;
    }

    RenderPresent crop = *p;
    if (p->hdrWidth) {
        crop.left = 0;
        crop.right = p->hdrWidth;
    }
    int width = crop.right - crop.left;
    /* Size the bloom buffer in game pixels so the blur taps stay adjacent. */
    int divisor = SDL_max(2, (3 * p->scale + 1) / 2);
    int amount = blur ? settings->cameraBlurAmount : 0;
    /* A repeated presentation can blit the resolved picture directly. */
    int resolved = bloomReady && bloomDivisor == divisor &&
        RenderGlPictureMatches(&bloomCache.picture, &crop, p->hdrTexture) &&
        bloomCache.tonemapper == settings->tonemapper && bloomCache.enabled == settings->bloom &&
        bloomCache.hdr == hdr && bloomCache.motionAmount == amount;
    if (!resolved) {
        GLint destination, previousProgram;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &destination);
        glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
        if (amount && !MotionPicture(&crop, amount)) amount = 0;
        if (hdr || amount) {
            BloomInit();
            RenderGlBeginFullscreen(bloomProgram.program, bloomArray);
            glActiveTexture(GL_TEXTURE0);
            resolved = BloomResize(width, h, divisor);
            if (resolved) ResolvePicture(&crop, settings->tonemapper, settings->bloom, hdr, amount);
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)destination);
        glUseProgram((GLuint)previousProgram);
    }

    RenderPresent shown = *p;
    if (resolved) {
        shown.framebuffer = bloomTargets[BLOOM_COMPOSITE];
        shown.left = p->left - crop.left + 1;
        shown.right = p->right - crop.left + 1;
        shown.top = 1;
        shown.bottom = h + 1;
    }
    RenderGlPresentDirect(&shown, filter);
}

static void ModernDestroy(void)
{
    RenderGlProgramDestroy(&modernProgram);
    RenderGlProgramDestroy(&bloomProgram);
    RenderGlProgramDestroy(&motionProgram);
    glDeleteVertexArrays(1, &motionArray);
    glDeleteTextures(MOTION_TARGETS, motionTextures);
    glDeleteFramebuffers(MOTION_TARGETS, motionTargets);
    memset(motionTextures, 0, sizeof(motionTextures));
    memset(motionTargets, 0, sizeof(motionTargets));
    motionArray = 0;
    motionWidth = motionHeight = motionTileSize = motionReady = 0;
    motionCache = (RenderPictureKey){0};
    glDeleteVertexArrays(1, &bloomArray);
    glDeleteTextures(BLOOM_TARGETS, bloomTextures);
    glDeleteFramebuffers(BLOOM_TARGETS, bloomTargets);
    memset(bloomTextures, 0, sizeof(bloomTextures));
    memset(bloomTargets, 0, sizeof(bloomTargets));
    memset(&bloomCache, 0, sizeof(bloomCache));
    bloomArray = 0;
    bloomWidth = bloomHeight = bloomDivisor = bloomReady = 0;
}

const RenderPipeline modernPipeline = {.scene = ModernScene, .present = ModernPresent,
    .destroy = ModernDestroy, .bind = Bind, .lights = Lights};
