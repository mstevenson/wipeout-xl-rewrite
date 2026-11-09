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

int RenderOriginalFrameRate(int pal)
{
    switch (RenderCurrent()->id) {
    case RENDER_SATURN: return 20;
    case RENDER_MODERN:
    case RENDER_PC: return 60;
    case RENDER_N64: return 30; /* the supported USA Wipeout 64 import */
    default: return pal ? 25 : 30;
    }
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
    switch (id) {
    case RENDER_MODERN: return &modernRenderer;
    case RENDER_SATURN: return &saturnRenderer;
    case RENDER_PC: return &pcRenderer;
    case RENDER_N64: return &n64Renderer;
    default: return &ps1Renderer;
    }
}

const void *RenderProfile(const RenderFrame *f)
{
    switch (f->id) {
    case RENDER_MODERN: return &f->settings.modern;
    case RENDER_SATURN: return &f->settings.saturn;
    case RENDER_PC: return &f->settings.pc;
    case RENDER_N64: return &f->settings.n64;
    default: return &f->settings.ps1;
    }
}

int RenderDepth(const RenderFrame *f)
{
    return RenderGet(f->id)->depth(RenderProfile(f));
}

int *RenderScaleSetting(RenderSettings *s, RendererId id)
{
    switch (id) {
    case RENDER_PC: return &s->pc.scale;
    case RENDER_N64: return &s->n64.scale;
    case RENDER_MODERN: return &s->modern.scale;
    case RENDER_SATURN: return &s->saturn.scale;
    default: return &s->ps1.scale;
    }
}

int RenderScale(const RenderFrame *f)
{
    switch (f->id) {
    case RENDER_PC: return f->settings.pc.scale;
    case RENDER_N64: return f->settings.n64.scale;
    case RENDER_MODERN: return f->settings.modern.scale;
    case RENDER_SATURN: return f->settings.saturn.scale;
    default: return f->settings.ps1.scale;
    }
}

void RenderNativeSize(RendererId id, int pal, int *width, int *height)
{
    *width = id == RENDER_PC ? 640 : id == RENDER_SATURN ? 352 : 320;
    *height = id == RENDER_PC ? 480 : (id == RENDER_PS1 || id == RENDER_MODERN) && pal ? 256 : 240;
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

    switch (id) {
    case RENDER_PS1: s->ps1 = defaults.ps1; break;
    case RENDER_MODERN: s->modern = defaults.modern; break;
    case RENDER_SATURN: s->saturn = defaults.saturn; break;
    case RENDER_PC: s->pc = defaults.pc; break;
    case RENDER_N64: s->n64 = defaults.n64; break;
    default: *s = defaults; break;
    }
}

int RenderCommit(void)
{
    RenderFrame next = {0};
    int changed;

    next.settings = portSettings.render;
    next.n64Track = PortOriginalTrackIs64();
    next.id = next.settings.selection;
    if (next.id == RENDER_AUTO) {
        if (next.n64Track) {
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
           interpolation or the frame recorder. Selection aliases are not policy. */
        switch (next.id) {
        case RENDER_MODERN:
            changed = memcmp(&current.settings.modern, &next.settings.modern, sizeof(next.settings.modern)) != 0;
            break;
        case RENDER_SATURN:
            changed = memcmp(&current.settings.saturn, &next.settings.saturn, sizeof(next.settings.saturn)) != 0;
            break;
        case RENDER_PC:
            changed = memcmp(&current.settings.pc, &next.settings.pc, sizeof(next.settings.pc)) != 0;
            break;
        case RENDER_N64:
            changed = memcmp(&current.settings.n64, &next.settings.n64, sizeof(next.settings.n64)) != 0;
            break;
        default:
            changed = memcmp(&current.settings.ps1, &next.settings.ps1, sizeof(next.settings.ps1)) != 0;
            break;
        }
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

static const char *const names[] = {"auto", "ps1", "pc", "n64", "saturn", "modern"};
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

static const char *const scaleKeys[] = {"", "ps1_render_scale", "pc_render_scale", "n64_render_scale", "saturn_render_scale", "modern_render_scale"};
static const char *const tonemapperKeys[] = {
    [MODERN_TONEMAP_ORIGINAL] = "original",
    [MODERN_TONEMAP_SOFT_SHADOWS] = "pbr_neutral_soft_shadows"
};

int RenderSet(RenderSettings *s, const char *key, const char *value)
{
    for (int id = RENDER_PS1; id < RENDER_COUNT; id++) {
        if (!strcmp(key, scaleKeys[id])) {
            char *end;
            long scale = strtol(value, &end, 10);
            if (end != value && !*end && scale >= 0 && scale <= 16)
                *RenderScaleSetting(s, (RendererId)id) = (int)scale;
            return 1;
        }
    }
    if (!strcmp(key, "modern_tonemapper")) {
        if (!strcmp(value, "pbr_neutral")) {
            s->modern.tonemapper = MODERN_TONEMAP_SOFT_SHADOWS;
            return 1;
        }
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
    RendererId *platform = !strcmp(key, "ps1_renderer") ? &s->ps1Selection :
        !strcmp(key, "pc_renderer") ? &s->pcSelection :
        !strcmp(key, "n64_renderer") ? &s->n64Selection :
        !strcmp(key, "saturn_renderer") ? &s->saturnSelection : NULL;
    if (platform) {
        for (int i = RENDER_PS1; i < RENDER_COUNT; i++) {
            if (!strcmp(value, names[i])) *platform = (RendererId)i;
        }
        return 1;
    }
    if (!strcmp(key, "renderer") || !strcmp(key, "automatic_renderer")) {
        for (int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++) {
            if (!strcmp(value, names[i])) {
                if (!strcmp(key, "renderer")) {
                    /* The former forced N64 option now applies only to Wipeout 64 tracks. */
                    s->selection = i == RENDER_N64 ? RENDER_AUTO : (RendererId)i;
                } else if (i == RENDER_PS1 || i == RENDER_PC || i == RENDER_SATURN) {
                    s->ps1Selection = (RendererId)i;
                }
                return 1;
            }
        }
        return 1; /* unknown value leaves a safe selection intact */
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
        RenderFrame frame = {.settings = *s, .id = (RendererId)id};
        fprintf(file, "%s = %d\n", scaleKeys[id], RenderScale(&frame));
    }
    fprintf(file, "pc_draw_distance = %d\n", s->pc.drawDistance);
    fprintf(file, "modern_brightness = %d\n", s->modern.brightness);
    unsigned tonemapper = s->modern.tonemapper;
    if (tonemapper >= sizeof(tonemapperKeys) / sizeof(tonemapperKeys[0]))
        tonemapper = MODERN_TONEMAP_SOFT_SHADOWS;
    fprintf(file, "modern_tonemapper = %s\n", tonemapperKeys[tonemapper]);
    fprintf(file, "ps1_renderer = %s\npc_renderer = %s\nn64_renderer = %s\nsaturn_renderer = %s\n",
            names[s->ps1Selection], names[s->pcSelection], names[s->n64Selection], names[s->saturnSelection]);
    for (size_t i = 0; i < sizeof(options) / sizeof(options[0]); i++) {
        fprintf(file, "%s = %d\n", options[i].key, *(const int *)((const char *)s + options[i].offset));
    }
}
