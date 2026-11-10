#include "renderer.h"
#include "../settings.h"
#include "../original.h"
#include "../pc.h"
#include "../saturn.h"
#include "../psx/psx_io.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

static RenderFrame current;
static RenderSettings committedSettings;
static int valid;

typedef struct RenderProfileInfo {
    const Renderer *renderer;
    const char *key, *scaleKey, *rateKey, *capKey;
    size_t offset, size, scaleOffset, pacingOffset, sceneOffset;
    int width, height, palHeight, fps, palFps, interpolate;
} Profile;

static const Profile profiles[RENDER_COUNT] = {
#define RENDER_PROFILE(id, field, w, h, palH, rate, palRate, interpolate) \
    [id] = { &field##Renderer, #field, #field "_render_scale", #field "_frame_rate", #field "_frame_cap", \
             offsetof(RenderSettings, field), sizeof(((RenderSettings *)0)->field), \
             offsetof(RenderSettings, field.scale), offsetof(RenderSettings, field.pacing), \
             offsetof(RenderSettings, field.scene), \
             w, h, palH, rate, palRate, interpolate },
#include "profiles.def"
#undef RENDER_PROFILE
};

static const Profile *ProfileFor(RendererId id)
{
    return &profiles[id >= RENDER_PS1 && id < RENDER_COUNT ? id : RENDER_PS1];
}

RenderPacing *RenderPacingSetting(RenderSettings *s, RendererId id)
{
    return (RenderPacing *)((char *)s + ProfileFor(id)->pacingOffset);
}

RenderScene *RenderSceneSetting(RenderSettings *s, RendererId id)
{
    return (RenderScene *)((char *)s + ProfileFor(id)->sceneOffset);
}

const RenderScene *RenderSceneFor(const RenderFrame *frame)
{
    const Profile *profile = frame->profile;
    return (const RenderScene *)((const char *)&frame->settings + (profile->sceneOffset - profile->offset));
}

int RenderUsesExtendedView(const RenderFrame *frame)
{
    return frame->id == RENDER_MODERN || RenderSceneFor(frame)->farView;
}

static const RenderPacing *FramePacing(const RenderFrame *frame)
{
    const Profile *profile = frame->profile;
    return (const RenderPacing *)((const char *)&frame->settings + (profile->pacingOffset - profile->offset));
}

int PortFrameRate(void)
{
    if (portSettings.frameRateOverride) return portSettings.frameRateOverride - 1;
    return FramePacing(RenderCurrent())->rate;
}

int PortFrameCap(int pal)
{
    int rate = PortFrameRate();
    if (rate == FRAME_RATE_CONSOLE) return RenderOriginalFrameRate(RenderCurrent()->id, pal);
    if (rate == FRAME_RATE_CAPPED) {
        return portSettings.frameCapOverride ? portSettings.frameCapOverride : FramePacing(RenderCurrent())->cap;
    }
    return 0;
}

int RenderOriginalFrameRate(RendererId id, int pal)
{
    const Profile *profile = ProfileFor(id);
    return pal ? profile->palFps : profile->fps;
}

int RenderOriginalInterpolates(void)
{
    return RenderCurrent()->profile->interpolate;
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
    return &f->settings;
}

int RenderDepth(const RenderFrame *f)
{
    return f->profile->renderer->depth(RenderProfile(f));
}

int *RenderScaleSetting(RenderSettings *s, RendererId id)
{
    return (int *)((char *)s + ProfileFor(id)->scaleOffset);
}

int RenderScale(const RenderFrame *f)
{
    return *(const int *)((const char *)&f->settings + (f->profile->scaleOffset - f->profile->offset));
}

void RenderNativeSize(RendererId id, int pal, int *width, int *height)
{
    const Profile *profile = ProfileFor(id);
    *width = profile->width;
    *height = pal && id >= RENDER_PS1 && id < RENDER_COUNT ? profile->palHeight : profile->height;
}

RenderStyle RenderPrepare(const RenderFrame *f, const RenderMaterial *m)
{
    return f->profile->renderer->prepare(RenderProfile(f), m);
}

RenderResources RenderResourcesFor(const RenderFrame *frame)
{
    const Renderer *renderer = frame->profile->renderer;
    return renderer->resources ? renderer->resources(RenderProfile(frame)) : (RenderResources){0};
}

/* Session facts restrict optional motion transport without selecting a profile. */
RenderResources RenderFrameResources(const RenderFrame *frame, int linked, int tracing)
{
    RenderResources resources = RenderResourcesFor(frame);
    if (tracing || (linked && resources.motionSoloOnly)) resources.cameraBlur = 0;
    return resources;
}

RenderPacketPolicy RenderPacketPolicyFor(const RenderFrame *frame)
{
    const Renderer *renderer = frame->profile->renderer;
    return renderer->packets ? renderer->packets(RenderProfile(frame)) : (RenderPacketPolicy){0};
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

RenderFrame RenderSnapshot(RendererId id, const RenderSettings *settings)
{
    RenderFrame frame = {.id = id, .profile = ProfileFor(id)};
    memcpy(&frame.settings, (const char *)settings + frame.profile->offset, frame.profile->size);
    if (id == RENDER_MODERN) {
        ModernRenderSettings *modern = &frame.settings.modern;
        modern->bloom = RenderModernBloom(settings);
        if (!modern->scene.farView) {
            /* Custom fades over the last third of the selected world distance. */
            modern->scene.fade = 1;
            modern->scene.fadeNear = modern->customDistance * 2 / 3;
            modern->scene.fadeFar = modern->customDistance;
        }
    }
    return frame;
}

int RenderCommit(void)
{
    committedSettings = portSettings.render;
    committedSettings.modern.bloom = RenderModernBloom(&committedSettings);
    RendererId id = committedSettings.selection;
    if (id == RENDER_AUTO) {
        if (committedSettings.modernForAllPlatforms) {
            id = RENDER_MODERN;
        } else if (PortOriginalTrackIs64()) {
            id = committedSettings.n64Selection;
        } else if (PortOriginalTrack() >= 0) {
            id = committedSettings.ps1Selection;
        } else if (PsxIoDisc() && PortSaturnDisc()) {
            id = committedSettings.saturnSelection;
        } else if (PsxIoDisc() && PortPcData()) {
            id = committedSettings.pcSelection;
        } else {
            id = committedSettings.ps1Selection;
        }
    }
    const RenderScene *override = &portSettings.sceneOverride;
    RenderScene *scene = RenderSceneSetting(&committedSettings, id);
    if (override->farView) scene->farView = override->farView > 0;
    if (override->farFlares) scene->farFlares = override->farFlares > 0;
    if (override->fade) scene->fade = override->fade > 0;
    if (override->fadeNear) {
        scene->fadeNear = override->fadeNear;
        scene->fadeFar = override->fadeFar;
    }
    RenderFrame next = RenderSnapshot(id, &committedSettings);
    /* Inactive preferences must not disturb interpolation or recording. */
    int changed = !valid || current.id != next.id ||
        memcmp(RenderProfile(&current), RenderProfile(&next), next.profile->size) != 0;
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

const RenderSettings *RenderCommittedSettings(void)
{
    RenderCurrent();
    return &committedSettings;
}

RenderGeometry RenderGeometryFor(const RenderFrame *frame)
{
    return frame->profile->renderer->geometry(RenderProfile(frame));
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
    OPTION("modern_for_all_platforms", modernForAllPlatforms),
    OPTION("modern_bilinear", modern.bilinear),
    OPTION("modern_resample", modern.resample),
    OPTION("modern_bloom", modern.bloom),
    OPTION("modern_camera_blur", modern.cameraBlur),
    OPTION("modern_weapon_lights", modern.weaponLights),
    OPTION("modern_smooth_track_lighting", modern.smoothTrackLighting),
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
    OPTION("n64_resample", n64.resample)
#undef OPTION
};

static const char *const tonemapperKeys[] = {
    [MODERN_TONEMAP_ORIGINAL] = "original",
    [MODERN_TONEMAP_SOFT_SHADOWS] = "pbr_neutral_soft_shadows"
};

static const char *const trackDetailKeys[] = {
    [RENDER_TRACK_NATIVE] = "native",
    [RENDER_TRACK_MEDIUM] = "medium",
    [RENDER_TRACK_HIGH] = "high"
};

static int SetScene(RenderScene *scene, const char *key, const char *value)
{
    long number = strtol(value, NULL, 10);

    if (!strcmp(key, "far_view")) {
        scene->farView = number != 0;
    } else if (!strcmp(key, "fade")) {
        scene->fade = number != 0;
    } else if (!strcmp(key, "far_flares")) {
        scene->farFlares = number != 0;
    } else if (!strcmp(key, "fade_near")) {
        if (number > 0 && number <= INT_MAX - 1000) scene->fadeNear = (int)number;
    } else if (!strcmp(key, "fade_far")) {
        if (number > 0 && number <= INT_MAX) scene->fadeFar = (int)number;
    } else {
        return 0;
    }
    return 1;
}

int RenderSet(RenderSettings *s, const char *key, const char *value)
{
    for (int id = RENDER_PS1; id < RENDER_COUNT; id++) {
        const Profile *profile = &profiles[id];
        if (!strcmp(key, profile->rateKey)) {
            char *end;
            long rate = strtol(value, &end, 10);
            if (end != value && !*end && rate >= FRAME_RATE_UNCAPPED && rate <= FRAME_RATE_CONSOLE)
                RenderPacingSetting(s, (RendererId)id)->rate = (int)rate;
            return 1;
        }
        if (!strcmp(key, profile->capKey)) {
            char *end;
            long cap = strtol(value, &end, 10);
            if (end != value && !*end && cap > 0)
                RenderPacingSetting(s, (RendererId)id)->cap = cap < FRAME_CAP_MIN ? FRAME_CAP_MIN :
                    cap > FRAME_CAP_MAX ? FRAME_CAP_MAX : (int)cap;
            return 1;
        }
        if (!strcmp(key, profile->scaleKey)) {
            char *end;
            long scale = strtol(value, &end, 10);
            if (end != value && !*end && scale >= 0 && scale <= 16)
                *RenderScaleSetting(s, (RendererId)id) = (int)scale;
            return 1;
        }
        size_t prefix = strlen(profile->key);
        if (!strncmp(key, profile->key, prefix) && key[prefix] == '_' &&
            SetScene(RenderSceneSetting(s, (RendererId)id), key + prefix + 1, value)) return 1;
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
    if (!strcmp(key, "modern_track_detail")) {
        if (!strcmp(value, "medium_plus")) value = trackDetailKeys[RENDER_TRACK_MEDIUM];
        for (size_t i = 0; i < sizeof(trackDetailKeys) / sizeof(trackDetailKeys[0]); i++) {
            if (!strcmp(value, trackDetailKeys[i])) {
                s->modern.trackDetail = (int)i;
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
    if (!strcmp(key, "modern_camera_blur_strength")) {
        char *end;
        long amount = strtol(value, &end, 10);
        if (end != value && !*end && amount >= 0 && amount <= MODERN_BLUR_MAX)
            s->modern.cameraBlurAmount = (int)amount;
        return 1;
    }
    if (!strcmp(key, "modern_custom_distance")) {
        char *end;
        long distance = strtol(value, &end, 10);
        if (end != value && !*end) {
            if (distance < MODERN_DISTANCE_MIN) distance = MODERN_DISTANCE_MIN;
            if (distance > MODERN_DISTANCE_MAX) distance = MODERN_DISTANCE_MAX;
            s->modern.customDistance = (int)distance;
        }
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
        const Profile *profile = &profiles[id];
        const int *scale = (const int *)((const char *)s + profile->scaleOffset);
        const RenderPacing *pacing = (const RenderPacing *)((const char *)s + profile->pacingOffset);
        const RenderScene *scene = (const RenderScene *)((const char *)s + profile->sceneOffset);
        fprintf(file, "%s = %d\n%s = %d\n%s = %d\n", profile->scaleKey, *scale,
                profile->rateKey, pacing->rate, profile->capKey, pacing->cap);
        fprintf(file, "%s_far_view = %d\n%s_fade = %d\n%s_fade_near = %d\n%s_fade_far = %d\n%s_far_flares = %d\n",
                profile->key, scene->farView, profile->key, scene->fade, profile->key, scene->fadeNear,
                profile->key, scene->fadeFar, profile->key, scene->farFlares);
    }
    fprintf(file, "pc_draw_distance = %d\n", s->pc.drawDistance);
    fprintf(file, "modern_brightness = %d\n", s->modern.brightness);
    fprintf(file, "modern_custom_distance = %d\n", s->modern.customDistance);
    fprintf(file, "modern_camera_blur_strength = %d\n", s->modern.cameraBlurAmount);
    unsigned tonemapper = s->modern.tonemapper;
    if (tonemapper >= sizeof(tonemapperKeys) / sizeof(tonemapperKeys[0]))
        tonemapper = MODERN_TONEMAP_SOFT_SHADOWS;
    fprintf(file, "modern_tonemapper = %s\n", tonemapperKeys[tonemapper]);
    unsigned trackDetail = s->modern.trackDetail;
    if (trackDetail >= sizeof(trackDetailKeys) / sizeof(trackDetailKeys[0]))
        trackDetail = RENDER_TRACK_NATIVE;
    fprintf(file, "modern_track_detail = %s\n", trackDetailKeys[trackDetail]);
    for (size_t i = 0; i < sizeof(platforms) / sizeof(platforms[0]); i++) {
        RendererId id = *(const RendererId *)((const char *)s + platforms[i].offset);
        fprintf(file, "%s = %s\n", platforms[i].key, ProfileFor(id)->key);
    }
    for (size_t i = 0; i < sizeof(options) / sizeof(options[0]); i++) {
        fprintf(file, "%s = %d\n", options[i].key, *(const int *)((const char *)s + options[i].offset));
    }
}
