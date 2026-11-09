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
#define RENDER_AUX_FLAGS
#define RENDER_AUX_COLOUR \
    "uniform bool hdrActive;\n" \
    "flat in int auxFlags;\n" \
    "flat in ivec4 emissionUv;\n" \
    "layout(location = 1) out vec4 hdrColour;\n" \
    "layout(location = 2) out vec4 hdrMask;\n" \
    "vec3 HdrMaterial(vec3 rgb, float coverage) {\n" \
    "    return (auxFlags & 2) != 0 ? min(rgb, vec3(coverage)) : rgb;\n" \
    "}\n" \
    "void WriteAux(vec4 c) {\n" \
    "    bool overlay = (auxFlags & 2) != 0;\n" \
    "    bool emits = (auxFlags & 1) != 0;\n" \
    "    if (emissionUv.z > 0) {\n" \
    "        vec2 t = vTexel.xy / vTexel.z;\n" \
    "        emits = emits && all(lessThan(abs(t - vec2(emissionUv.xy)), vec2(emissionUv.zw)));\n" \
    "    }\n" \
    "    hdrColour = c;\n" \
    "    hdrMask = vec4(emits && !overlay ? c.a : 0.0, overlay ? c.a : 0.0, 0.0, c.a);\n" \
    "}\n"

static const char *modernVertexSource =
#include "shaders/scene_vertex.h"
;
static const char *modernFragmentSource =
#include "shaders/material_fragment.h"
;
static RenderProgram modernProgram;
static GLint uniformWorldBrightness, uniformHdrActive;

static void Bind(const void *profile, int hdrActive)
{
    const ModernRenderSettings *settings = profile;
    glUniform1f(uniformWorldBrightness, settings->brightness / (float)MODERN_BRIGHTNESS_MAX);
    glUniform1i(uniformHdrActive, hdrActive);
}

static RenderProgram *ModernScene(void)
{
    if (!modernProgram.program) {
        if (!RenderGlProgramInit(&modernProgram, modernVertexSource, modernFragmentSource)) abort();
        GLuint program = modernProgram.program;
        uniformWorldBrightness = glGetUniformLocation(program, "worldBrightness");
        uniformHdrActive = glGetUniformLocation(program, "hdrActive");
        glUniform1f(uniformWorldBrightness, 1.0f);
    }
    return &modernProgram;
}

static const char *bloomVertexSource =
    "out vec2 uv;\n"
    "void main() {\n"
    "    uv = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

static const char *bloomFragmentSource =
    "#ifdef GL_ES\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "precision highp sampler2D;\n"
    "#endif\n"
    "uniform sampler2D source, mask, blurred, sdr;\n"
    "uniform ivec4 crop;\n"
    "uniform vec2 stepSize;\n"
    "uniform int pass, downsample;\n"
    "uniform bool softShadows;\n"
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
    /* The SDR buffer clamps subtractive blends at zero as they are drawn.
       Recover that floor if later blends would otherwise inherit negative RGB. */
    "        vec3 scene = LinearColour(max(texelFetch(source, p, 0).rgb, original.rgb));\n"
    "        vec2 glowSize = float(downsample) * vec2(textureSize(blurred, 0));\n"
    "        vec3 glow = texture(blurred, (vec2(rel) + 0.5) / glowSize).rgb;\n"
    "        vec3 hdr = scene + glow * 0.25;\n"
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
static GLint bloomCrop, bloomStep, bloomDownsample, bloomSoftShadows;
static int bloomWidth, bloomHeight, bloomDivisor, bloomReady;
static struct {
    RenderPictureKey picture;
    int tonemapper;
} bloomCache;

static void BloomInit(void)
{
    if (bloomProgram.program) return;
    if (!RenderGlProgramInit(&bloomProgram, bloomVertexSource, bloomFragmentSource)) abort();
    GLuint program = bloomProgram.program;
    glUniform1i(glGetUniformLocation(program, "source"), 0);
    glUniform1i(glGetUniformLocation(program, "mask"), 1);
    glUniform1i(glGetUniformLocation(program, "blurred"), 2);
    glUniform1i(glGetUniformLocation(program, "sdr"), 3);
    bloomCrop = glGetUniformLocation(program, "crop");
    bloomStep = glGetUniformLocation(program, "stepSize");
    bloomDownsample = glGetUniformLocation(program, "downsample");
    bloomSoftShadows = glGetUniformLocation(program, "softShadows");
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

static void BloomPicture(const RenderPresent *p, int tonemapper)
{
    if (RenderGlPictureMatches(&bloomCache.picture, p, p->hdrTexture) &&
        bloomCache.tonemapper == tonemapper) return;

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

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, bloomTargets[BLOOM_COMPOSITE]);
    glViewport(0, 0, bloomWidth + 2, bloomHeight + 2);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, bloomTextures[0]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, p->hdrTexture);
    glUniform1i(bloomProgram.uniformPass, BLOOM_RESOLVE);
    glUniform1i(bloomSoftShadows, tonemapper == MODERN_TONEMAP_SOFT_SHADOWS);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    bloomCache.picture = RenderGlPictureKey(p, p->hdrTexture);
    bloomCache.tonemapper = tonemapper;
}

static void ModernPresent(const void *profile, const RenderPresent *p)
{
    const ModernRenderSettings *settings = profile;
    GLenum filter = settings->resample ? GL_LINEAR : GL_NEAREST;
    int h = p->bottom - p->top;
    if (!settings->bloom || !p->race || !p->hdrTexture || p->right <= p->left || h <= 0 ||
        p->x1 <= p->x0 || p->y0 == p->y1) {
        RenderGlPresentDirect(p, filter);
        return;
    }

    GLint destination, previousProgram;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &destination);
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    BloomInit();
    RenderGlBeginFullscreen(bloomProgram.program, bloomArray);
    glActiveTexture(GL_TEXTURE0);
    RenderPresent crop = *p;
    if (p->hdrWidth) {
        crop.left = 0;
        crop.right = p->hdrWidth;
    }
    RenderPresent shown = *p;
    /* Size the bloom buffer in game pixels so the blur taps stay adjacent. */
    int divisor = SDL_max(2, (3 * p->scale + 1) / 2);
    if (BloomResize(crop.right - crop.left, h, divisor)) {
        BloomPicture(&crop, settings->tonemapper);
        shown.framebuffer = bloomTargets[BLOOM_COMPOSITE];
        shown.left = p->left - crop.left + 1;
        shown.right = p->right - crop.left + 1;
        shown.top = 1;
        shown.bottom = h + 1;
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)destination);
    RenderGlPresentDirect(&shown, filter);
    glUseProgram((GLuint)previousProgram);
}

static void ModernDestroy(void)
{
    RenderGlProgramDestroy(&modernProgram);
    RenderGlProgramDestroy(&bloomProgram);
    glDeleteVertexArrays(1, &bloomArray);
    glDeleteTextures(BLOOM_TARGETS, bloomTextures);
    glDeleteFramebuffers(BLOOM_TARGETS, bloomTargets);
    memset(bloomTextures, 0, sizeof(bloomTextures));
    memset(bloomTargets, 0, sizeof(bloomTargets));
    memset(&bloomCache, 0, sizeof(bloomCache));
    bloomArray = 0;
    bloomWidth = bloomHeight = bloomDivisor = bloomReady = 0;
}

const RenderPipeline modernPipeline = {ModernScene, ModernPresent, ModernDestroy, Bind};
