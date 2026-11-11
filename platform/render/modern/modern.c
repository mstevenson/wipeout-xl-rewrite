#include "modern.h"
#include "../../settings.h"
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static RenderFrame current;
static int valid;
int renderModernActive = -1;
static const char *const toneNames[] = {"original", "pbr_neutral_soft_shadows"};
static const struct Option { const char *key; size_t offset; int low, high; } options[] = {
#define OPTION(key, field, low, high) {"modern_" key, offsetof(ModernRenderSettings, field), low, high}
    OPTION("bilinear", bilinear, 0, 1),
    OPTION("brightness", brightness, MODERN_BRIGHTNESS_MIN, MODERN_BRIGHTNESS_MAX),
    OPTION("smooth_track_lighting", smoothTrackLighting, 0, 1),
    OPTION("weapon_lights", weaponLights, 0, 1),
    OPTION("bloom", bloom, 0, 1),
    OPTION("camera_blur", cameraBlur, 0, 1),
    OPTION("camera_blur_strength", cameraBlurAmount, 0, MODERN_BLUR_MAX),
    OPTION("resample", resample, 0, 1)
#undef OPTION
};
static int Number(const char *text, int low, int high, int *result)
{
    if (!text) return 0;
    char *end;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (end == text || *end || errno == ERANGE) return 0;
    *result = value < low ? low : value > high ? high : (int)value;
    return 1;
}
void ModernReset(RenderSettings *settings)
{
    settings->modern = (ModernRenderSettings)MODERN_DEFAULTS;
}
int RenderSet(RenderSettings *settings, const char *key, const char *value)
{
    if (!strcmp(key, "renderer_mode")) {
        if (!strcmp(value, "existing")) settings->selection = RENDER_EXISTING;
        if (!strcmp(value, "modern")) settings->selection = RENDER_MODERN;
        return 1;
    }
    for (size_t i = 0; i < sizeof(options) / sizeof(options[0]); i++) {
        const struct Option *option = &options[i];
        if (!strcmp(key, option->key)) {
            Number(value, option->low, option->high, (int *)((char *)&settings->modern + option->offset));
            return 1;
        }
    }
    if (!strcmp(key, "modern_tonemapper")) {
        for (int i = 0; i < 2; i++) if (!strcmp(value, toneNames[i])) settings->modern.tonemapper = i;
        return 1;
    }
    return 0;
}
void RenderSave(FILE *file, const RenderSettings *settings)
{
    fprintf(file, "renderer_mode = %s\n", settings->selection == RENDER_MODERN ? "modern" : "existing");
    for (size_t i = 0; i < sizeof(options) / sizeof(options[0]); i++)
        fprintf(file, "%s = %d\n", options[i].key, *(const int *)((const char *)&settings->modern + options[i].offset));
    int tone = settings->modern.tonemapper;
    fprintf(file, "modern_tonemapper = %s\n",
            toneNames[tone >= 0 && tone < 2 ? tone : MODERN_TONEMAP_SOFT_SHADOWS]);
}
int RenderCommitBefore(void (*beforeChange)(void))
{
    RenderFrame next = {0};
    next.id = portSettings.render.selection == RENDER_MODERN ? RENDER_MODERN : RENDER_EXISTING;
    const char *mode = getenv("WIPEOUT_RENDERER_MODE");
    if (!mode) mode = getenv("WIPEOUT_RENDER_PROFILE");
    if (mode && !strcmp(mode, "modern")) next.id = RENDER_MODERN;
    if (mode && !strcmp(mode, "existing")) next.id = RENDER_EXISTING;
    if (next.id == RENDER_MODERN) {
        next.settings.modern = portSettings.render.modern;
        ModernRenderSettings *m = &next.settings.modern;
        /* Overrides affect the immutable frame, never saved preferences. */
        Number(getenv("WIPEOUT_MODERN_BLOOM"), 0, 1, &m->bloom);
    }
    int changed = !valid || next.id != current.id || memcmp(&next.settings, &current.settings, sizeof(next.settings));
    if (!changed) return 0;
    /* A flush may itself query policy. Even after invalidation, it must see
       the old frame rather than recursively publishing the requested one. */
    valid = 1;
    renderModernActive = current.id == RENDER_MODERN;
    if (beforeChange) beforeChange();
    next.generation = current.generation + 1;
    current = next;
    valid = 1;
    renderModernActive = current.id == RENDER_MODERN;
    return changed;
}
int RenderCommit(void) { return RenderCommitBefore(NULL); }
const RenderFrame *RenderCurrent(void)
{
    if (!valid) RenderCommit();
    return &current;
}
void RenderInvalidate(void) { valid = 0; renderModernActive = -1; }
