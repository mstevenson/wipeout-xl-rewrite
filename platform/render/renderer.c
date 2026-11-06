#include "renderer.h"
#include "../settings.h"
#include "../original.h"
#include <stddef.h>
#include <string.h>

static RenderFrame current;
static int valid;

const Renderer *RenderGet(RendererId id)
{
    switch (id) {
    case RENDER_PC: return &pcRenderer;
    case RENDER_N64: return &n64Renderer;
    default: return &ps1Renderer;
    }
}

const void *RenderProfile(const RenderFrame *f)
{
    switch (f->id) {
    case RENDER_PC: return &f->settings.pc;
    case RENDER_N64: return &f->settings.n64;
    default: return &f->settings.ps1;
    }
}

int RenderDepth(const RenderFrame *f)
{
    return RenderGet(f->id)->depth(RenderProfile(f));
}

RenderStyle RenderPrepare(const RenderFrame *f, const RenderMaterial *m)
{
    return RenderGet(f->id)->prepare(RenderProfile(f), m);
}

void RenderDefaults(RenderSettings *s, RendererId id)
{
    const RenderSettings defaults = RENDER_DEFAULTS;

    switch (id) {
    case RENDER_PS1: s->ps1 = defaults.ps1; break;
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
        next.id = next.n64Track ? RENDER_N64 : next.settings.automaticBase;
    }
    changed = !valid || current.id != next.id;
    if (!changed) {
        /* Preferences for an inactive renderer must not disturb the displayed frame,
           interpolation or the frame recorder. Selection aliases are not policy. */
        switch (next.id) {
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

static const char *const names[] = {"auto", "ps1", "pc", "n64"};
static const struct {
    const char *key;
    size_t offset;
} options[] = {
#define OPTION(key, field) {key, offsetof(RenderSettings, field)}
    OPTION("ps1_quantize_vertices", ps1.quantizeVertices), OPTION("ps1_affine_textures", ps1.affineTextures),
    OPTION("ps1_colour_precision", ps1.colourPrecision), OPTION("ps1_dither", ps1.dither), OPTION("ps1_depth", ps1.depth),
    OPTION("pc_bilinear", pc.bilinear), OPTION("pc_lighting", pc.lighting), OPTION("pc_depth", pc.depth),
    OPTION("n64_three_point", n64.threePoint), OPTION("n64_colour_precision", n64.colourPrecision),
    OPTION("n64_dither", n64.dither), OPTION("n64_dedither", n64.dedither),
    OPTION("n64_divot", n64.divot), OPTION("n64_resample", n64.resample)
#undef OPTION
};

int RenderSet(RenderSettings *s, const char *key, const char *value)
{
    if (!strcmp(key, "renderer") || !strcmp(key, "automatic_renderer")) {
        for (int i = 0; i < 4; i++) {
            if (!strcmp(value, names[i])) {
                if (!strcmp(key, "renderer")) {
                    /* The former forced N64 option now applies only to Wipeout 64 tracks. */
                    s->selection = i == RENDER_N64 ? RENDER_AUTO : (RendererId)i;
                } else if (i == RENDER_PS1 || i == RENDER_PC) {
                    s->automaticBase = (RendererId)i;
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
    fprintf(file, "renderer = %s\nautomatic_renderer = %s\n", names[s->selection], names[s->automaticBase]);
    for (size_t i = 0; i < sizeof(options) / sizeof(options[0]); i++) {
        fprintf(file, "%s = %d\n", options[i].key, *(const int *)((const char *)s + options[i].offset));
    }
}
