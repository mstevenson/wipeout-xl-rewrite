#include "renderer.h"
#include "../settings.h"
#include "../original.h"
#include "../pc.h"
#include "../saturn.h"
#include "../psx/psx_io.h"
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

static RenderFrame current;
static int valid;

typedef struct Profile {
    const Renderer *renderer;
    const char *key, *scaleKey;
    size_t offset, size, scaleOffset;
    int width, height, palHeight, fps, palFps;
} Profile;

static const Profile profiles[RENDER_COUNT] = {
#define PROFILE(field, w, h, palH, rate, palRate) \
    { &field##Renderer, #field, #field "_render_scale", offsetof(RenderSettings, field), \
      sizeof(((RenderSettings *)0)->field), offsetof(RenderSettings, field.scale), w, h, palH, rate, palRate }
    [RENDER_PS1] = PROFILE(ps1, 320, 240, 256, 30, 25),
    [RENDER_PC] = PROFILE(pc, 640, 480, 480, 60, 60),
    [RENDER_N64] = PROFILE(n64, 320, 240, 240, 30, 30),
    [RENDER_SATURN] = PROFILE(saturn, 352, 240, 240, 20, 20),
    [RENDER_MODERN] = PROFILE(modern, 320, 240, 256, 60, 60),
#undef PROFILE
};

static const Profile *ProfileFor(RendererId id)
{
    return &profiles[id >= RENDER_PS1 && id < RENDER_COUNT ? id : RENDER_PS1];
}

int RenderOriginalFrameRate(int pal)
{
    const Profile *profile = ProfileFor(RenderCurrent()->id);
    return pal ? profile->palFps : profile->fps;
}

int RenderPcViewLimits(const RenderFrame *frame, int *sections, int *scenery)
{
    /* POWERVR.EXE, indexed by the original F3 view-detail level. */
    static const int sectionLimits[] = {50, 45, 40, 36, 32, 28, 24, 20};
    static const int sceneryLimits[] = {36000, 29000, 23000, 17000, 12000, 8000, 5000, 3000};
    if (frame->id != RENDER_PC) return 0;
    int level = frame->settings.pc.drawDistance - 1;
    if (level < 0 || level >= PC_DRAW_DISTANCE_LEVELS) level = 0;
    *sections = sectionLimits[level];
    *scenery = sceneryLimits[level];
    return 1;
}

const Renderer *RenderGet(RendererId id)
{
    return ProfileFor(id)->renderer;
}

RendererId RenderIdFromName(const char *name)
{
    for (int id = RENDER_PS1; id < RENDER_COUNT; id++) {
        if (!strcmp(name, profiles[id].key)) return (RendererId)id;
    }
    return RENDER_AUTO;
}

int RenderModernBloom(const RenderSettings *s)
{
    return s->modernBloomOverride ? s->modernBloomOverride > 0 : s->modern.bloom;
}

const void *RenderProfile(const RenderFrame *f)
{
    return (const char *)&f->settings + ProfileFor(f->id)->offset;
}

int RenderDepth(const RenderFrame *f)
{
    return RenderGet(f->id)->depth(RenderProfile(f));
}

const char *RenderScaleKey(RendererId id)
{
    return ProfileFor(id)->scaleKey;
}

int *RenderScaleSetting(RenderSettings *s, RendererId id)
{
    return (int *)((char *)s + ProfileFor(id)->scaleOffset);
}

int RenderScale(const RenderFrame *f)
{
    return *(const int *)((const char *)&f->settings + ProfileFor(f->id)->scaleOffset);
}

void RenderNativeSize(RendererId id, int pal, int *width, int *height)
{
    const Profile *profile = ProfileFor(id);
    *width = profile->width;
    *height = pal && id >= RENDER_PS1 && id < RENDER_COUNT ? profile->palHeight : profile->height;
}

RenderStyle RenderPrepare(const RenderFrame *f, const RenderMaterial *m)
{
    return RenderGet(f->id)->prepare(RenderProfile(f), m);
}

RenderResources RenderResourcesFor(const RenderFrame *frame)
{
    const Renderer *renderer = RenderGet(frame->id);
    return renderer->resources ? renderer->resources(RenderProfile(frame)) : (RenderResources){0};
}

void RenderDefaults(RenderSettings *s, RendererId id)
{
    const RenderSettings defaults = RENDER_DEFAULTS;

    if (id >= RENDER_PS1 && id < RENDER_COUNT) {
        const Profile *profile = ProfileFor(id);
        memcpy((char *)s + profile->offset, (const char *)&defaults + profile->offset, profile->size);
    } else {
        *s = defaults;
    }
}

int RenderCommit(void)
{
    RenderFrame next = {0};
    int changed;

    next.settings = portSettings.render;
    next.settings.modern.bloom = RenderModernBloom(&next.settings);
    next.id = next.settings.selection;
    if (next.id == RENDER_AUTO) {
        if (PortOriginalTrackIs64()) {
            next.id = next.settings.n64Selection;
        } else if (PortOriginalTrack() >= 0) {
            next.id = next.settings.ps1Selection;
        } else if (PsxIoDisc() && PortSaturnDisc()) {
            next.id = next.settings.saturnSelection;
        } else if (PsxIoDisc() && PortPcData()) {
            next.id = next.settings.pcSelection;
        } else {
            next.id = next.settings.ps1Selection;
        }
    }
    changed = !valid || current.id != next.id;
    if (!changed) {
        /* Preferences for an inactive renderer must not disturb the displayed frame,
           interpolation or the frame recorder. */
        changed = memcmp(RenderProfile(&current), RenderProfile(&next), ProfileFor(next.id)->size) != 0;
    }
    current = next;
    valid = 1;
    return changed;
}

void RenderInvalidate(void)
{
    valid = 0;
}

const RenderFrame *RenderCurrent(void)
{
    if (!valid) {
        RenderCommit();
    }
    return &current;
}

RenderGeometry RenderGeometryFor(const RenderFrame *frame)
{
    return RenderGet(frame->id)->geometry(RenderProfile(frame));
}

RenderGeometry PortRenderGeometry(void)
{
    return RenderGeometryFor(RenderCurrent());
}

static const struct {
    const char *key;
    size_t offset;
} platforms[] = {
    {"ps1_renderer", offsetof(RenderSettings, ps1Selection)},
    {"pc_renderer", offsetof(RenderSettings, pcSelection)},
    {"n64_renderer", offsetof(RenderSettings, n64Selection)},
    {"saturn_renderer", offsetof(RenderSettings, saturnSelection)},
};
static const struct {
    const char *key;
    size_t offset;
} options[] = {
#define OPTION(key, field) {key, offsetof(RenderSettings, field)}
    OPTION("modern_bilinear", modern.bilinear),
    OPTION("modern_resample", modern.resample),
    OPTION("modern_bloom", modern.bloom),
    OPTION("saturn_quantize_vertices", saturn.quantizeVertices), OPTION("saturn_affine_textures", saturn.affineTextures),
    OPTION("saturn_colour_precision", saturn.colourPrecision), OPTION("saturn_mesh", saturn.mesh),
    OPTION("saturn_depth", saturn.depth), OPTION("saturn_camera_aligned_sky", saturn.cameraAlignedSky),
    OPTION("saturn_resample", saturn.resample),
    OPTION("ps1_quantize_vertices", ps1.quantizeVertices), OPTION("ps1_affine_textures", ps1.affineTextures),
    OPTION("ps1_colour_precision", ps1.colourPrecision), OPTION("ps1_dither", ps1.dither), OPTION("ps1_depth", ps1.depth),
    OPTION("ps1_resample", ps1.resample),
    OPTION("pc_bilinear", pc.bilinear), OPTION("pc_lighting", pc.lighting), OPTION("pc_depth", pc.depth),
    OPTION("pc_resample", pc.resample), OPTION("pc_rgb565", pc.rgb565),
    OPTION("pc_correct_menu_aspect", pc.correctMenuAspect),
    OPTION("n64_three_point", n64.threePoint), OPTION("n64_colour_precision", n64.colourPrecision),
    OPTION("n64_dither", n64.dither), OPTION("n64_dedither", n64.dedither),
    OPTION("n64_divot", n64.divot), OPTION("n64_resample", n64.resample)
#undef OPTION
};

static const char *const tonemapperKeys[] = {
    [MODERN_TONEMAP_ORIGINAL] = "original",
    [MODERN_TONEMAP_SOFT_SHADOWS] = "pbr_neutral_soft_shadows"
};

int RenderSet(RenderSettings *s, const char *key, const char *value)
{
    for (int id = RENDER_PS1; id < RENDER_COUNT; id++) {
        if (!strcmp(key, profiles[id].scaleKey)) {
            char *end;
            long scale = strtol(value, &end, 10);
            if (end != value && !*end && scale >= 0 && scale <= 16)
                *RenderScaleSetting(s, (RendererId)id) = (int)scale;
            return 1;
        }
    }
    if (!strcmp(key, "modern_tonemapper")) {
        for (size_t i = 0; i < sizeof(tonemapperKeys) / sizeof(tonemapperKeys[0]); i++) {
            if (!strcmp(value, tonemapperKeys[i])) {
                s->modern.tonemapper = (int)i;
                break;
            }
        }
        return 1;
    }
    if (!strcmp(key, "modern_brightness")) {
        char *end;
        long brightness = strtol(value, &end, 10);
        if (end != value && !*end && brightness >= MODERN_BRIGHTNESS_MIN && brightness <= MODERN_BRIGHTNESS_MAX)
            s->modern.brightness = (int)brightness;
        return 1;
    }
    if (!strcmp(key, "pc_draw_distance")) {
        char *end;
        long level = strtol(value, &end, 10);
        if (end != value && !*end && level >= 1 && level <= PC_DRAW_DISTANCE_LEVELS)
            s->pc.drawDistance = (int)level;
        return 1;
    }
    for (size_t i = 0; i < sizeof(platforms) / sizeof(platforms[0]); i++) {
        if (!strcmp(key, platforms[i].key)) {
            RendererId id = RenderIdFromName(value);
            if (id != RENDER_AUTO)
                *(RendererId *)((char *)s + platforms[i].offset) = id;
            return 1;
        }
    }
    for (size_t i = 0; i < sizeof(options) / sizeof(options[0]); i++) {
        if (!strcmp(key, options[i].key)) {
            if (!strcmp(value, "0") || !strcmp(value, "1")) {
                *(int *)((char *)s + options[i].offset) = value[0] == '1';
            }
            return 1;
        }
    }
    return 0;
}

void RenderSave(FILE *file, const RenderSettings *s)
{
    for (int id = RENDER_PS1; id < RENDER_COUNT; id++) {
        const int *scale = (const int *)((const char *)s + profiles[id].scaleOffset);
        fprintf(file, "%s = %d\n", profiles[id].scaleKey, *scale);
    }
    fprintf(file, "pc_draw_distance = %d\n", s->pc.drawDistance);
    fprintf(file, "modern_brightness = %d\n", s->modern.brightness);
    unsigned tonemapper = s->modern.tonemapper;
    if (tonemapper >= sizeof(tonemapperKeys) / sizeof(tonemapperKeys[0]))
        tonemapper = MODERN_TONEMAP_SOFT_SHADOWS;
    fprintf(file, "modern_tonemapper = %s\n", tonemapperKeys[tonemapper]);
    for (size_t i = 0; i < sizeof(platforms) / sizeof(platforms[0]); i++) {
        RendererId id = *(const RendererId *)((const char *)s + platforms[i].offset);
        fprintf(file, "%s = %s\n", platforms[i].key, ProfileFor(id)->key);
    }
    for (size_t i = 0; i < sizeof(options) / sizeof(options[0]); i++) {
        fprintf(file, "%s = %d\n", options[i].key, *(const int *)((const char *)s + options[i].offset));
    }
}
