/* Real settings persistence without touching the player's file. */
#include <SDL3/SDL.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
static char testDir[] = "/tmp/wipeout-render-settings-XXXXXX";
static char *TestPrefPath(const char *org, const char *app)
{
    char *result = NULL;
    (void)org; (void)app;
    SDL_asprintf(&result, "%s/", testDir);
    return result;
}
#define SDL_GetPrefPath TestPrefPath
#include "../../platform/settings.c"
#undef SDL_GetPrefPath
PortConfig portConfig;
static int n64, saturnDisc, pcData, originalTrack = -1, discLoaded;
int PortOriginalTrackIs64(void) { return n64; }
int PortOriginalTrack(void) { return originalTrack; }
int PortPcData(void) { assert(discLoaded); return pcData; }
int PortSaturnDisc(void) { assert(discLoaded); return saturnDisc; }
const struct Disc *PsxIoDisc(void) { return discLoaded ? (const struct Disc *)&discLoaded : NULL; }
static void LoadSettings(const char *prefix, const char *contents)
{
    FILE *file = fopen(PortSettingsPath(), "w"); assert(file);
    fputs(prefix, file); fputs(contents, file); fclose(file); PortSettingsLoad();
}
static void LoadPlatformSettings(const char *contents)
{
    LoadSettings("modern_for_all_platforms = 0\n", contents);
}
static void Policies(void)
{
    RenderSettings settings = RENDER_DEFAULTS;
    RenderFrame f = RenderSnapshot(RENDER_PS1, &settings);
    RenderMaterial m = {.textured = 1, .dither = 1};
    RenderStyle a = RenderPrepare(&f, &m);
    assert(a.filter == 0 && (a.shade & (SHADE_SNAP | SHADE_DITHER | SHADE_AFFINE_UV)) ==
           (SHADE_SNAP | SHADE_DITHER | SHADE_AFFINE_UV));
    f.settings.ps1.quantizeVertices = 0;
    a = RenderPrepare(&f, &m);
    assert(!(a.shade & SHADE_SNAP) && (a.shade & SHADE_DITHER));
    f.settings.ps1.quantizeVertices = 1; f.settings.ps1.colourPrecision = 0;
    a = RenderPrepare(&f, &m);
    assert((a.shade & SHADE_SNAP) && !(a.shade & (SHADE_PS1_COLOUR | SHADE_DITHER)));
    f.settings.ps1.affineTextures = 0;
    assert(!(RenderPrepare(&f, &m).shade & SHADE_AFFINE_UV));
    f = RenderSnapshot(RENDER_PC, &settings);
    assert(RenderPrepare(&f, &m).filter == 2);
    f.settings.pc.bilinear = 0;
    assert(RenderPrepare(&f, &m).filter == 0);
    f = RenderSnapshot(RENDER_N64, &settings);
    assert(RenderPrepare(&f, &m).filter == 1); /* explicit N64 over PS1 assets */
    f.settings.n64.threePoint = 0;
    assert(RenderPrepare(&f, &m).filter == 0);
    m.n64 = 1;
    for (int id = RENDER_PS1; id <= RENDER_N64; id++) {
        f = RenderSnapshot((RendererId)id, &settings);
        assert(RenderPrepare(&f, &m).shade & SHADE_N64); /* encoding survives every renderer */
    }
    m.n64 = 0;
    a = RenderPrepare(&f, &m);
    assert(!(a.shade & (SHADE_SNAP | SHADE_AFFINE_UV | SHADE_PS1_COLOUR | SHADE_DITHER | SHADE_WORLD_TINT)));
    assert(!RenderGet(RENDER_PS1)->depth(&settings.ps1) && RenderGet(RENDER_PC)->depth(&settings.pc) &&
           RenderGet(RENDER_N64)->depth(&settings.n64));
    settings.ps1.depth = 1; settings.pc.depth = 0;
    assert(RenderGet(RENDER_PS1)->depth(&settings.ps1) && !RenderGet(RENDER_PC)->depth(&settings.pc) &&
           RenderGet(RENDER_N64)->depth(&settings.n64));
    f = RenderSnapshot(RENDER_N64, &(RenderSettings){0});
    assert(RenderDepth(&f)); /* N64 depth is always enabled. */
}

static void SaturnProfile(void)
{
    LoadPlatformSettings("ps1_renderer = saturn\nsaturn_depth = 1\nsaturn_mesh = 0\n");
    assert(RenderCurrent()->id == RENDER_SATURN && RenderDepth(RenderCurrent()));
    assert(!RenderCurrent()->settings.saturn.cameraAlignedSky);
    RenderMaterial m = {.textured = 1, .dither = 1};
    RenderStyle style = RenderPrepare(RenderCurrent(), &m);
    assert(style.filter == RENDER_NEAREST && style.coverage == RENDER_COVERAGE_HALF && !(style.shade & SHADE_DITHER));
    assert((style.shade & (SHADE_SNAP | SHADE_AFFINE_UV | SHADE_PS1_COLOUR)) ==
           (SHADE_SNAP | SHADE_AFFINE_UV | SHADE_PS1_COLOUR));
    assert(RenderSet(&portSettings.render, "saturn_camera_aligned_sky", "1"));
    assert(RenderCommit() && RenderCurrent()->settings.saturn.cameraAlignedSky);
    assert(PortSettingsSave());
    RenderSettings saved = portSettings.render;
    PortSettingsLoad();
    assert(!memcmp(&saved, &portSettings.render, sizeof(saved)));
    RenderCommit();
    portSettings.render.ps1.dither = 0;
    assert(!RenderCommit()); /* inactive profile changes do not invalidate a frame */
    RenderDefaults(&portSettings.render, RENDER_SATURN);
    assert(!portSettings.render.ps1.dither && RenderCommit());
    assert(!RenderDepth(RenderCurrent()));
    assert(!RenderCurrent()->settings.saturn.cameraAlignedSky);
    assert(RenderPrepare(RenderCurrent(), &m).coverage == RENDER_COVERAGE_MESH);
    m.ui = 1;
    assert(RenderPrepare(RenderCurrent(), &m).coverage == RENDER_COVERAGE_DEFAULT);
    m.n64 = 1;
    assert(RenderPrepare(RenderCurrent(), &m).shade & SHADE_N64);
    LoadPlatformSettings("ps1_renderer = saturn\n");
    n64 = 1; RenderCommit(); assert(RenderCurrent()->id == RENDER_N64);
    n64 = 0; RenderCommit(); assert(RenderCurrent()->id == RENDER_SATURN);
}

static void ModernProfile(void)
{
    LoadPlatformSettings("ps1_renderer = modern\nmodern_render_scale = 3\n");
    assert(RenderCurrent()->id == RENDER_MODERN && RenderDepth(RenderCurrent()));
    assert(RenderOriginalFrameRate(RENDER_MODERN, 0) == 60 && RenderScale(RenderCurrent()) == 3);
    assert(RenderCurrent()->settings.modern.brightness == 85);
    assert(RenderCurrent()->settings.modern.bilinear);
    assert(RenderCurrent()->settings.modern.bloom);
    assert(RenderGeometryFor(RenderCurrent()).smoothTrackLighting);
    assert(RenderCurrent()->settings.modern.tonemapper == MODERN_TONEMAP_SOFT_SHADOWS);
    LoadPlatformSettings("ps1_renderer = modern\nmodern_render_scale = 3\n"
         "modern_bilinear = 0\nmodern_tonemapper = original\n");
    assert(!RenderCurrent()->settings.modern.bilinear);
    assert(RenderCurrent()->settings.modern.tonemapper == MODERN_TONEMAP_ORIGINAL);
    assert(RenderSet(&portSettings.render, "modern_tonemapper", "pbr_neutral") && !RenderCommit());
    assert(RenderSet(&portSettings.render, "modern_tonemapper", "pbr_neutral_soft_shadows") && RenderCommit());
    assert(RenderCurrent()->settings.modern.tonemapper == MODERN_TONEMAP_SOFT_SHADOWS);
    assert(RenderSet(&portSettings.render, "modern_tonemapper", "unknown") && !RenderCommit());
    assert(RenderSet(&portSettings.render, "modern_bloom", "0") && RenderCommit());
    assert(RenderResourcesFor(RenderCurrent()).hdr); /* lights work without bloom */
    assert(RenderSet(&portSettings.render, "modern_weapon_lights", "0") && RenderCommit());
    assert(!RenderResourcesFor(RenderCurrent()).hdr);
    assert(RenderSet(&portSettings.render, "modern_smooth_track_lighting", "0") && RenderCommit());
    assert(RenderSet(&portSettings.render, "modern_bilinear", "2") && !RenderCommit());
    assert(RenderSet(&portSettings.render, "modern_bilinear", "-1") && !RenderCommit());
    assert(RenderSet(&portSettings.render, "modern_bilinear", "1") && RenderCommit());
    assert(RenderSet(&portSettings.render, "modern_bilinear", "0") && RenderCommit());
    assert(RenderSet(&portSettings.render, "modern_brightness", "50") && RenderCommit());
    assert(RenderSet(&portSettings.render, "modern_brightness", "49") && !RenderCommit());
    assert(RenderSet(&portSettings.render, "modern_brightness", "101") && !RenderCommit());
    assert(PortSettingsSave());
    char *saved = SDL_LoadFile(PortSettingsPath(), NULL);
    assert(saved && strstr(saved, "modern_tonemapper = pbr_neutral_soft_shadows\n"));
    assert(strstr(saved, "modern_bilinear = 0\n"));
    assert(strstr(saved, "modern_weapon_lights = 0\n"));
    assert(strstr(saved, "modern_smooth_track_lighting = 0\n"));
    SDL_free(saved);
    PortSettingsLoad();
    assert(RenderCurrent()->id == RENDER_MODERN && RenderScale(RenderCurrent()) == 3);
    assert(RenderCurrent()->settings.modern.brightness == MODERN_BRIGHTNESS_MIN);
    assert(!RenderCurrent()->settings.modern.bilinear);
    assert(!RenderCurrent()->settings.modern.bloom);
    assert(!RenderCurrent()->settings.modern.weaponLights);
    assert(!RenderGeometryFor(RenderCurrent()).smoothTrackLighting);
    assert(RenderCurrent()->settings.modern.tonemapper == MODERN_TONEMAP_SOFT_SHADOWS);
    assert(RenderSet(&portSettings.render, "modern_tonemapper", "original") && RenderCommit());
    RenderDefaults(&portSettings.render, RENDER_MODERN);
    assert(RenderCommit() && RenderScale(RenderCurrent()) == 0);
    assert(RenderCurrent()->settings.modern.bilinear);
    assert(RenderCurrent()->settings.modern.brightness == MODERN_BRIGHTNESS_DEFAULT);
    assert(RenderCurrent()->settings.modern.bloom);
    assert(RenderGeometryFor(RenderCurrent()).smoothTrackLighting);
    assert(RenderCurrent()->settings.modern.tonemapper == MODERN_TONEMAP_SOFT_SHADOWS);
}

static void DisplayResampling(void)
{
    LoadPlatformSettings(""); /* Missing display settings use each renderer's defaults. */
    assert(!portSettings.render.ps1.resample && portSettings.render.pc.resample);
    assert(portSettings.render.n64.resample && !portSettings.render.saturn.resample);
    LoadPlatformSettings("ps1_resample = 0\npc_resample = 0\nsaturn_resample = 1\nn64_resample = 0\n");
    assert(!portSettings.render.ps1.resample && !portSettings.render.pc.resample);
    assert(!portSettings.render.n64.resample && portSettings.render.saturn.resample);
    assert(PortSettingsSave());
    RenderSettings saved = portSettings.render;
    PortSettingsLoad();
    assert(!memcmp(&saved, &portSettings.render, sizeof(saved)));
    assert(!RenderCurrent()->settings.ps1.resample);
    portSettings.render.ps1.resample = 1;
    assert(!RenderCurrent()->settings.ps1.resample); /* applied at the frame boundary */
    assert(RenderCommit() && RenderCurrent()->settings.ps1.resample);
    portSettings.render.saturn.resample = 0;
    assert(!RenderCommit()); /* inactive display policy leaves the current frame alone */
    LoadPlatformSettings("ps1_resample = bogus\npc_resample = -1\nsaturn_resample = 2\n");
    assert(!portSettings.render.ps1.resample && portSettings.render.pc.resample && !portSettings.render.saturn.resample);
}

static void ProfileIsolation(void)
{
    const char *prefixes[] = {"", "ps1_", "pc_", "n64_"};
    const char *keys[] = {
        "ps1_quantize_vertices", "ps1_affine_textures", "ps1_colour_precision", "ps1_dither", "ps1_depth",
        "pc_bilinear", "pc_lighting", "pc_depth", "n64_three_point", "n64_colour_precision",
        "n64_dither", "n64_dedither", "n64_divot", "n64_resample"
    };
    const RenderMaterial materials[] = {
        {.textured = 1, .dither = 1, .ui = 1}, /* XL HUD */
        {.dither = 1}, /* XL untextured particle */
        {.textured = 1, .dither = 1}, /* XL ship/textured effect */
        {.textured = 1, .raw = 1},
        {.fullColour = 1, .textured = 1, .ui = 1}, /* platform glyph */
        {.n64 = 1, .textured = 1, .bounds = 1, .dither = 1},
        {.n64 = 1, .textured = 1}, /* missing bounds never selects a different renderer */
        {.fullColour = 1, .textured = 1}, /* full-colour world material */
        {.n64 = 1, .textured = 1, .bounds = 1, .ui = 1}, /* even N64-tagged UI stays point sampled */
        {.textured = 1} /* affine world billboard: no perspective depth, but not UI */
    };
    for (int id = RENDER_PS1; id <= RENDER_N64; id++) {
        for (int enabled = 0; enabled < 2; enabled++) {
            RenderSettings settings = RENDER_DEFAULTS;
            for (size_t key = 0; key < sizeof(keys) / sizeof(keys[0]); key++)
                assert(RenderSet(&settings, keys[key], enabled ? "1" : "0"));
            RenderFrame f = RenderSnapshot((RendererId)id, &settings);
            int depth = RenderDepth(&f);
            assert(depth == (id == RENDER_N64 || enabled));
            for (size_t i = 0; i < sizeof(materials) / sizeof(materials[0]); i++) {
                const RenderMaterial *m = &materials[i];
                RenderStyle baseline = RenderPrepare(&f, m);
                int filter = id == RENDER_PC ? 2 : id == RENDER_N64;
                assert(baseline.filter == (enabled && m->textured && (!m->n64 || m->bounds) &&
                                           (id != RENDER_N64 || !m->ui) ? filter : 0));
                assert(!!(baseline.shade & SHADE_DITHER) == (id == RENDER_PS1 && enabled && m->dither));
                if (id != RENDER_PS1 || !enabled)
                    assert(!(baseline.shade & (SHADE_SNAP | SHADE_AFFINE_UV | SHADE_PS1_COLOUR | SHADE_DITHER)));
                if (id == RENDER_PS1 && enabled)
                    assert((baseline.shade & (SHADE_SNAP | SHADE_AFFINE_UV | SHADE_PS1_COLOUR)) ==
                           (SHADE_SNAP | SHADE_AFFINE_UV | SHADE_PS1_COLOUR));
                for (size_t key = 0; key < sizeof(keys) / sizeof(keys[0]); key++) {
                    if (!strncmp(keys[key], prefixes[id], strlen(prefixes[id]))) continue;
                    RenderSettings options = settings;
                    assert(RenderSet(&options, keys[key], enabled ? "0" : "1"));
                    RenderFrame changed = RenderSnapshot((RendererId)id, &options);
                    RenderStyle actual = RenderPrepare(&changed, m);
                    assert(actual.shade == baseline.shade && actual.filter == baseline.filter);
                    assert(RenderDepth(&changed) == depth);
                }
            }
            portSettings.render = settings;
            portSettings.render.selection = (RendererId)id;
            for (n64 = 0; n64 <= 1; n64++) {
                RenderCommit();
                assert(RenderCurrent()->id == id);
                assert(PortRenderGeometry().lightAllVertices == ((id == RENDER_PC && enabled) || id == RENDER_SATURN));
                RenderGeometry geometry = PortRenderGeometry();
                assert(geometry.alternateQuad == (id == RENDER_PC));
                assert(geometry.worldBillboards == (id == RENDER_PC));
                assert(geometry.gouraudObjects == ((id == RENDER_PC && enabled) || id == RENDER_SATURN));
                assert(geometry.lightFormat == (id == RENDER_PC && enabled ? RENDER_LIGHT_RGB8 : RENDER_LIGHT_PS1));
            }
        }
    }
    n64 = 0;
    puts("All renderer profiles isolate rasterization, depth and lighting from inactive controls and track selection: OK");
}

/* Exercise every profile, including optional resources and sampling requests. */
static void ResourcePolicyIsolation(void)
{
    const char *prefixes[] = {"", "ps1_", "pc_", "n64_", "saturn_", "modern_"};
    const struct { const char *key, *value; } changes[] = {
        {"ps1_quantize_vertices", "0"}, {"ps1_depth", "1"}, {"pc_lighting", "0"},
        {"pc_bilinear", "0"}, {"n64_three_point", "0"}, {"n64_dither", "0"},
        {"saturn_affine_textures", "0"}, {"saturn_mesh", "0"}, {"saturn_camera_aligned_sky", "1"},
        {"modern_bilinear", "0"}, {"modern_brightness", "50"}, {"modern_bloom", "0"},
        {"modern_tonemapper", "pbr_neutral_soft_shadows"}, {"modern_smooth_track_lighting", "0"}, {"modern_weapon_lights", "0"}
    };
    const RenderMaterial cases[] = {
        {.textured = 1, .bounds = 1},
        {.textured = 1, .bounds = 1, .ui = 1},
        {.textured = 1, .bounds = 1, .n64 = 1},
        {.textured = 1, .bounds = 1, .fullColour = 1},
        {.textured = 1}
    };
    for (int id = RENDER_PS1; id < RENDER_COUNT; id++) {
        RenderFrame frame = RenderSnapshot((RendererId)id, &(RenderSettings)RENDER_DEFAULTS);
        RenderResources resources = RenderResourcesFor(&frame);
        RenderPacketPolicy packets = RenderPacketPolicyFor(&frame);
        assert(packets.nativeQuads == (id == RENDER_SATURN));
        assert(packets.nativeSky == (id == RENDER_SATURN));
        assert(packets.shadowMask == (id == RENDER_SATURN));
        assert(packets.smoothTrackLighting == (id == RENDER_MODERN));
        assert(RenderGeometryFor(&frame).smoothTrackLighting == (id == RENDER_MODERN));
        RenderMaterial road = {.textured = 1, .perspective = 1, .smoothTrackLighting = 1};
        assert(!!(RenderPrepare(&frame, &road).shade & SHADE_AFFINE_COLOUR) == (id != RENDER_MODERN));
        assert(resources.hdr == (id == RENDER_MODERN));
        assert(resources.originalTextures == (id == RENDER_MODERN));
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            RenderStyle style = RenderPrepare(&frame, &cases[i]);
            for (size_t key = 0; key < sizeof(changes) / sizeof(changes[0]); key++) {
                if (!strncmp(changes[key].key, prefixes[id], strlen(prefixes[id]))) continue;
                RenderSettings options = RENDER_DEFAULTS;
                assert(RenderSet(&options, changes[key].key, changes[key].value));
                RenderFrame changed = RenderSnapshot((RendererId)id, &options);
                RenderStyle actual = RenderPrepare(&changed, &cases[i]);
                RenderResources actualResources = RenderResourcesFor(&changed);
                RenderPacketPolicy actualPackets = RenderPacketPolicyFor(&changed);
                assert(!memcmp(&packets, &actualPackets, sizeof(packets)));
                assert(!memcmp(&style, &actual, sizeof(style)));
                assert(!memcmp(&resources, &actualResources, sizeof(resources)));
            }
        }
    }
    RenderFrame modern = RenderSnapshot(RENDER_MODERN, &(RenderSettings)RENDER_DEFAULTS);
    for (int bilinear = 0; bilinear <= 1; bilinear++) {
        modern.settings.modern.bilinear = bilinear;
        RenderStyle style = RenderPrepare(&modern, &cases[0]);
        assert(style.nativeBounds == bilinear);
    }
    RenderMaterial road = {.textured = 1, .perspective = 1, .smoothTrackLighting = 1};
    assert(RenderPrepare(&modern, &cases[0]).shade & SHADE_AFFINE_COLOUR);
    modern.settings.modern.smoothTrackLighting = 0;
    assert(RenderPrepare(&modern, &road).shade & SHADE_AFFINE_COLOUR);
    modern.settings.modern.bloom = 0;
    assert(RenderResourcesFor(&modern).hdr);
    modern.settings.modern.weaponLights = 0;
    assert(!RenderResourcesFor(&modern).hdr && RenderResourcesFor(&modern).originalTextures);
    puts("All five profiles isolate resource requirements and source-bound policy from inactive settings: OK");
}

static void ModernOverride(void)
{
    LoadSettings("", "ps1_renderer = pc\npc_renderer = ps1\n");
    assert(portSettings.render.modernForAllPlatforms && RenderCurrent()->id == RENDER_MODERN);
    discLoaded = 1;
    saturnDisc = 1;
    assert(!RenderCommit() && RenderCurrent()->id == RENDER_MODERN);
    saturnDisc = 0; pcData = 1;
    assert(!RenderCommit() && RenderCurrent()->id == RENDER_MODERN);
    originalTrack = 0;
    assert(!RenderCommit() && RenderCurrent()->id == RENDER_MODERN);
    n64 = 1;
    assert(!RenderCommit() && RenderCurrent()->id == RENDER_MODERN);
    discLoaded = pcData = n64 = 0;
    originalTrack = -1;

    portSettings.render.modernForAllPlatforms = 0;
    assert(RenderCurrent()->id == RENDER_MODERN);
    assert(RenderCommit() && RenderCurrent()->id == RENDER_PC);
    assert(PortSettingsSave());
    PortSettingsLoad();
    assert(!portSettings.render.modernForAllPlatforms && RenderCurrent()->id == RENDER_PC);
    assert(portSettings.render.ps1Selection == RENDER_PC && portSettings.render.pcSelection == RENDER_PS1);
    puts("Default Modern override covers discs and imported tracks and restores saved platform choices: OK");
}

static void PlatformSelection(void)
{
    LoadPlatformSettings("");
    discLoaded = 1;
    assert(RenderCurrent()->id == RENDER_PS1);
    saturnDisc = 1;
    assert(RenderCommit() && RenderCurrent()->id == RENDER_SATURN);
    n64 = 1;
    assert(RenderCommit() && RenderCurrent()->id == RENDER_N64);
    n64 = 0;
    assert(RenderCommit() && RenderCurrent()->id == RENDER_SATURN);
    originalTrack = 0;
    assert(RenderCommit() && RenderCurrent()->id == RENDER_PS1);
    originalTrack = -1;
    saturnDisc = 0; pcData = 1;
    assert(RenderCommit() && RenderCurrent()->id == RENDER_PC);
    pcData = 0; saturnDisc = 1;
    assert(RenderCommit() && RenderCurrent()->id == RENDER_SATURN);
    LoadPlatformSettings("ps1_renderer = n64\nn64_renderer = saturn\n"
         "pc_renderer = ps1\nsaturn_renderer = pc\n");
    assert(RenderCurrent()->id == RENDER_PC);
    assert(portSettings.render.ps1Selection == RENDER_N64);
    assert(portSettings.render.n64Selection == RENDER_SATURN);
    assert(PortSettingsSave());
    RenderSettings saved = portSettings.render;
    PortSettingsLoad();
    assert(!memcmp(&saved, &portSettings.render, sizeof(saved)));
    assert(RenderCurrent()->id == RENDER_PC);
    portSettings.render.n64Selection = RENDER_PS1;
    assert(!RenderCommit()); /* Changing another platform does not reset the active frame. */
    n64 = 1;
    assert(RenderCommit() && RenderCurrent()->id == RENDER_PS1);
    discLoaded = saturnDisc = pcData = n64 = 0;
    LoadPlatformSettings("ps1_renderer = auto\npc_renderer = bad\nsaturn_renderer = -1\nn64_renderer = auto\n");
    assert(portSettings.render.ps1Selection == RENDER_PS1 && portSettings.render.pcSelection == RENDER_PC);
    assert(portSettings.render.saturnSelection == RENDER_SATURN && portSettings.render.n64Selection == RENDER_N64);
    puts("Platform routing, imported tracks and independent persistence: OK");
}

static void EnvironmentOverrides(void)
{
    SDL_Environment *env = SDL_GetEnvironment();
    SDL_SetEnvironmentVariable(env, "WIPEOUT_RENDERER", "n64", true);
    LoadSettings("", "");
    assert(portSettings.render.modernForAllPlatforms && RenderCurrent()->id == RENDER_N64);
    LoadPlatformSettings("ps1_renderer = pc\nn64_renderer = saturn\n");
    assert(RenderCurrent()->id == RENDER_N64);
    n64 = 1;
    assert(!RenderCommit() && RenderCurrent()->id == RENDER_N64);
    assert(PortSettingsSave());

    SDL_SetEnvironmentVariable(env, "WIPEOUT_RENDERER", "auto", true);
    PortSettingsLoad();
    assert(RenderCurrent()->id == RENDER_SATURN);
    n64 = 0;
    assert(RenderCommit() && RenderCurrent()->id == RENDER_PC);
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_RENDERER");

    for (int force = 0; force <= 1; force++) {
        SDL_SetEnvironmentVariable(env, "WIPEOUT_MODERN_BLOOM", force ? "1" : "0", true);
        LoadPlatformSettings(force ? "ps1_renderer = modern\nmodern_weapon_lights = 0\nmodern_bloom = 0\n"
                   : "ps1_renderer = modern\nmodern_weapon_lights = 0\nmodern_bloom = 1\n");
        assert(RenderCurrent()->id == RENDER_MODERN);
        assert(portSettings.render.modern.bloom == !force);
        assert(RenderCurrent()->settings.modern.bloom == force);
        assert(RenderResourcesFor(RenderCurrent()).hdr == force);
        assert(PortSettingsSave());
        SDL_UnsetEnvironmentVariable(env, "WIPEOUT_MODERN_BLOOM");
        PortSettingsLoad();
        assert(RenderResourcesFor(RenderCurrent()).hdr == !force);
    }
    puts("N64 environment selection and temporary Modern bloom overrides survive settings round trips: OK");
}

static void ProfileResolutions(void)
{
    SDL_Environment *env = SDL_GetEnvironment();
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_RENDER_SCALE");
    LoadPlatformSettings("");
    for (int id = RENDER_PS1; id <= RENDER_SATURN; id++) {
        assert(*RenderScaleSetting(&portSettings.render, (RendererId)id) == 1);
        for (int pal = 0; pal <= 1; pal++) {
            int w, h;
            RenderNativeSize((RendererId)id, pal, &w, &h);
            assert(w == (id == RENDER_PC ? 640 : id == RENDER_SATURN ? 352 : 320));
            assert(h == (id == RENDER_PC ? 480 : id == RENDER_PS1 && pal ? 256 : 240));
        }
    }
    LoadPlatformSettings("ps1_render_scale = 3\npc_render_scale = 0\nsaturn_render_scale = 2\n");
    assert(portSettings.render.ps1.scale == 3 && portSettings.render.pc.scale == 0);
    assert(portSettings.render.saturn.scale == 2 && portSettings.render.n64.scale == 1);
    assert(portSettings.render.modern.scale == 0);
    assert(PortSettingsSave());
    RenderSettings expected = portSettings.render;
    PortSettingsLoad();
    assert(!memcmp(&expected, &portSettings.render, sizeof(expected)));
    assert(RenderScale(RenderCurrent()) == 3);
    portSettings.render.saturn.scale = 5;
    assert(!RenderCommit());
    portSettings.render.ps1.scale = 2;
    assert(RenderScale(RenderCurrent()) == 3); /* immutable until the frame boundary */
    assert(RenderCommit() && RenderScale(RenderCurrent()) == 2);
    RenderDefaults(&portSettings.render, RENDER_PS1);
    assert(portSettings.render.ps1.scale == 1 && portSettings.render.saturn.scale == 5);
    LoadPlatformSettings("pc_render_scale = bad\nn64_render_scale = -1\nsaturn_render_scale = 17\nps1_render_scale = 2x\n");
    assert(portSettings.render.ps1.scale == 1 && portSettings.render.pc.scale == 1);
    assert(portSettings.render.n64.scale == 1 && portSettings.render.saturn.scale == 1);
    SDL_SetEnvironmentVariable(env, "WIPEOUT_RENDER_SCALE", "2", true);
    LoadPlatformSettings("ps1_render_scale = 1\npc_render_scale = 4\n");
    assert(portSettings.render.ps1.scale == 2 && portSettings.render.pc.scale == 2);
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_RENDER_SCALE");
    puts("Per-profile native sizes, scale defaults, persistence, reset and frame boundaries: OK");
}

static void OriginalFrameRates(void)
{
    LoadPlatformSettings("ps1_renderer = saturn\nn64_renderer = pc\nps1_frame_rate = 3\npc_frame_rate = 3\n"
         "n64_frame_rate = 3\nsaturn_frame_rate = 3\n");
    n64 = 0; RenderCommit();
    assert(PortFrameCap(0) == 20 && PortFrameCap(1) == 20);
    n64 = 1; RenderCommit();
    assert(PortFrameCap(0) == 60 && PortFrameCap(1) == 60);
    portSettings.render.n64Selection = RENDER_N64;
    assert(PortFrameCap(0) == 60); /* committed profile only */
    RenderCommit();
    assert(PortFrameCap(0) == 30 && PortFrameCap(1) == 30);
    portSettings.render.selection = RENDER_PS1; RenderCommit();
    assert(PortFrameCap(0) == 30 && PortFrameCap(1) == 25);
    n64 = 0; RenderCommit();
    assert(PortFrameCap(1) == 25);
    puts("Original frame rate follows the committed renderer, including cross-platform overrides: OK");
}

static void ProfilePacing(void)
{
    SDL_Environment *env = SDL_GetEnvironment();
    LoadPlatformSettings("ps1_frame_rate = 2\nps1_frame_cap = 45\npc_frame_rate = 2\npc_frame_cap = 90\n"
         "n64_frame_rate = 1\nsaturn_frame_cap = 1\nmodern_frame_cap = 1001\n");
    assert(PortFrameRate() == FRAME_RATE_CAPPED && PortFrameCap(0) == 45);
    assert(portSettings.render.n64.pacing.rate == FRAME_RATE_VSYNC && portSettings.render.n64.pacing.cap == FRAME_CAP_DEFAULT);
    assert(portSettings.render.saturn.pacing.cap == FRAME_CAP_MIN && portSettings.render.modern.pacing.cap == FRAME_CAP_MAX);
    portSettings.render.ps1Selection = RENDER_PC;
    assert(PortFrameCap(0) == 45);
    assert(RenderCommit() && PortFrameRate() == FRAME_RATE_CAPPED && PortFrameCap(0) == 90);
    assert(PortSettingsSave());
    PortSettingsLoad();
    assert(PortFrameCap(0) == 90 && portSettings.render.ps1.pacing.cap == 45);
    RenderDefaults(&portSettings.render, RENDER_PC);
    assert(RenderCommit() && PortFrameRate() == FRAME_RATE_CONSOLE && PortFrameCap(0) == 60);
    assert(portSettings.render.pc.pacing.cap == FRAME_CAP_DEFAULT && portSettings.render.ps1.pacing.cap == 45);

    SDL_SetEnvironmentVariable(env, "WIPEOUT_FRAME_RATE", "60", true);
    LoadPlatformSettings("ps1_frame_rate = 1\nps1_frame_cap = 45\n");
    assert(PortFrameRate() == FRAME_RATE_CAPPED && PortFrameCap(0) == 60);
    assert(PortSettingsSave());
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_FRAME_RATE");
    PortSettingsLoad();
    assert(PortFrameRate() == FRAME_RATE_VSYNC && portSettings.render.ps1.pacing.cap == 45);
    SDL_SetEnvironmentVariable(env, "WIPEOUT_FRAME_RATE", "vsync", true);
    LoadPlatformSettings("ps1_frame_rate = 3\n");
    assert(PortFrameRate() == FRAME_RATE_VSYNC && portSettings.render.ps1.pacing.rate == FRAME_RATE_CONSOLE);
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_FRAME_RATE");
    PortSettingsLoad();
    assert(PortFrameRate() == FRAME_RATE_CONSOLE);
    puts("Per-profile frame rates and caps follow the committed renderer, persist, reset and respect run overrides: OK");
}

static void ProfileScene(void)
{
    SDL_Environment *env = SDL_GetEnvironment();
    LoadPlatformSettings("pc_far_view = 0\npc_fade = 1\npc_fade_near = 81000\npc_fade_far = 80000\n"
         "\nps1_far_view = 1\nps1_fade = 0\nps1_fade_near = 42000\nps1_fade_far = 80000\nps1_far_flares = 1\n");
    const RenderScene *scene = RenderSceneFor(RenderCurrent());
    assert(scene->farView && !scene->fade && scene->farFlares);
    assert(scene->fadeNear == 42000 && scene->fadeFar == 80000);
    portSettings.render.ps1Selection = RENDER_PC;
    assert(RenderSceneFor(RenderCurrent())->farView);
    assert(RenderCommit());
    scene = RenderSceneFor(RenderCurrent());
    assert(!scene->farView && scene->fade && !scene->farFlares);
    assert(scene->fadeNear == 81000 && scene->fadeFar == 82000);
    assert(PortSettingsSave());
    RenderSettings saved = portSettings.render;
    PortSettingsLoad();
    assert(!memcmp(&saved, &portSettings.render, sizeof(saved)));
    RenderDefaults(&portSettings.render, RENDER_PC);
    assert(RenderCommit());
    scene = RenderSceneFor(RenderCurrent());
    assert(!scene->farView && !scene->fade && !scene->farFlares);
    assert(scene->fadeNear == 64000 && scene->fadeFar == 96000);
    assert(portSettings.render.ps1.scene.farFlares && !portSettings.render.ps1.scene.fade);

    SDL_SetEnvironmentVariable(env, "WIPEOUT_FAR_VIEW", "0", true);
    SDL_SetEnvironmentVariable(env, "WIPEOUT_FADE", "33000,55000", true);
    SDL_SetEnvironmentVariable(env, "WIPEOUT_FAR_FLARES", "1", true);
    LoadPlatformSettings("ps1_fade = 0\npc_fade = 0\n");
    for (int id = RENDER_PS1; id <= RENDER_PC; id++) {
        portSettings.render.ps1Selection = (RendererId)id;
        RenderCommit();
        scene = RenderSceneFor(RenderCurrent());
        assert(!scene->farView && scene->fade && scene->farFlares);
        assert(scene->fadeNear == 33000 && scene->fadeFar == 55000);
    }
    assert(PortSettingsSave());
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_FAR_VIEW");
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_FADE");
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_FAR_FLARES");
    PortSettingsLoad();
    scene = RenderSceneFor(RenderCurrent());
    assert(!scene->farView && !scene->fade && !scene->farFlares);
    assert(scene->fadeNear == 64000 && scene->fadeFar == 96000);
    puts("Scene controls follow the committed renderer, persist, reset and respect run overrides: OK");
}

static void NativeViewLimits(void)
{
    const int road[] = {50, 45, 40, 36, 32, 28, 24, 20};
    const int scenery[] = {36000, 29000, 23000, 17000, 12000, 8000, 5000, 3000};
    RenderFrame frame = RenderSnapshot(RENDER_PC, &(RenderSettings)RENDER_DEFAULTS);
    int sections, distance;
    for (int i = 0; i < PC_DRAW_DISTANCE_LEVELS; i++) {
        frame.settings.pc.drawDistance = i + 1;
        assert(RenderPcViewLimits(&frame, &sections, &distance));
        assert(sections == road[i] && distance == scenery[i]);
    }
    for (int id = RENDER_PS1; id <= RENDER_SATURN; id++) {
        if (id == RENDER_PC) continue;
        frame = RenderSnapshot((RendererId)id, &(RenderSettings)RENDER_DEFAULTS);
        assert(!RenderPcViewLimits(&frame, &sections, &distance));
    }
    puts("PC native road and scenery limits follow F3 and the active profile: OK");
}

int main(void)
{
    NativeViewLimits();
    Policies();
    ProfileIsolation();
    ResourcePolicyIsolation();
    SDL_Environment *env = SDL_GetEnvironment();
    assert(mkdtemp(testDir));
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_RENDERER");
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_FRAME_RATE");
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_FAR_VIEW");
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_FADE");
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_FAR_FLARES");
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_MODERN_BLOOM");
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_DEPTH_BUFFER");
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_RENDER_SCALE");
    PortSettingsLoad();
    assert(RenderCurrent()->id == RENDER_MODERN && !portSettings.render.ps1.depth);
    ModernOverride();
    LoadPlatformSettings("");
    for (int id = RENDER_PS1; id < RENDER_COUNT; id++) {
        RendererId renderer = (RendererId)id;
        int modern = id == RENDER_MODERN;
        assert(RenderPacingSetting(&portSettings.render, renderer)->rate ==
               (modern ? FRAME_RATE_VSYNC : FRAME_RATE_CONSOLE));
        const RenderScene *scene = RenderSceneSetting(&portSettings.render, renderer);
        assert(scene->farView == modern && scene->fade == modern && scene->farFlares == modern);
    }
    assert(portSettings.render.pc.rgb565);
    assert(portSettings.render.pc.drawDistance == 1);
    assert(!portSettings.render.pc.correctMenuAspect);
    LoadPlatformSettings("pc_rgb565 = 0\npc_draw_distance = 8\npc_correct_menu_aspect = 1\n");
    assert(!portSettings.render.pc.rgb565 && portSettings.render.pc.drawDistance == 8);
    assert(portSettings.render.pc.correctMenuAspect);
    assert(PortSettingsSave());
    PortSettingsLoad();
    assert(!portSettings.render.pc.rgb565 && portSettings.render.pc.drawDistance == 8);
    assert(portSettings.render.pc.correctMenuAspect);
    RenderDefaults(&portSettings.render, RENDER_PC);
    assert(portSettings.render.pc.rgb565 && portSettings.render.pc.drawDistance == 1);
    assert(!portSettings.render.pc.correctMenuAspect);
    LoadPlatformSettings("pc_draw_distance = 9\n");
    assert(portSettings.render.pc.drawDistance == 1);
    LoadSettings("", "renderer = saturn\nautomatic_renderer = pc\npc_rendering = 1\nsmooth_textures = 1\n"
         "render_scale = 4\ndepth_buffer = 0\nframe_rate = 2\nframe_cap = 60\nps1_vsync = 1\n"
         "far_view = 0\nfade = 1\nfade_near = 20000\nfade_far = 50000\nfar_flares = 1\n");
    assert(RenderCurrent()->id == RENDER_MODERN);
    const RenderSettings defaults = RENDER_DEFAULTS;
    assert(!memcmp(&defaults, &portSettings.render, sizeof(defaults)));
    LoadPlatformSettings("pc_depth = 0\n");
    assert(!portSettings.render.pc.depth);
    n64 = 1; RenderCommit(); assert(RenderCurrent()->id == RENDER_N64);
    assert(RenderDepth(RenderCurrent()));
    SDL_SetEnvironmentVariable(env, "WIPEOUT_DEPTH_BUFFER", "0", true);
    LoadPlatformSettings("pc_depth = 1\n");
    assert(!portSettings.render.pc.depth && RenderCurrent()->id == RENDER_N64);
    assert(RenderDepth(RenderCurrent()));
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_DEPTH_BUFFER");
    n64 = 0;
    LoadPlatformSettings("ps1_quantize_vertices = 0\nps1_dither = 0\npc_bilinear = 0\nn64_divot = 0\n");
    assert(RenderCurrent()->id == RENDER_PS1);
    assert(!portSettings.render.ps1.dither && !portSettings.render.ps1.quantizeVertices);
    assert(!portSettings.render.pc.bilinear && !portSettings.render.n64.divot);
    portSettings.render.selection = RENDER_PC;
    assert(RenderCurrent()->id == RENDER_PS1); /* pending changes cannot split a frame */
    assert(RenderCommit() && (RenderCurrent()->id == RENDER_PC));
    assert(!RenderCommit());
    RenderDefaults(&portSettings.render, RENDER_PC);
    assert(portSettings.render.pc.bilinear && !portSettings.render.n64.divot && !portSettings.render.ps1.dither);
    portSettings.render.selection = RENDER_AUTO;
    portSettings.render.ps1Selection = RENDER_PC;
    assert(PortSettingsSave());
    RenderSettings expected = portSettings.render;
    PortSettingsLoad();
    assert(!memcmp(&expected, &portSettings.render, sizeof(expected)));
    LoadPlatformSettings("ps1_dither = garbage\npc_depth = -1\n");
    assert(portSettings.render.selection == RENDER_AUTO && portSettings.render.ps1.dither && portSettings.render.pc.depth);
    ProfileResolutions();
    PlatformSelection();
    EnvironmentOverrides();
    OriginalFrameRates();
    ProfilePacing();
    ProfileScene();
    SaturnProfile();
    DisplayResampling();
    ModernProfile();
    portConfig.hidden = 1; PortSettingsLoad();
    assert(portSettings.render.selection == RENDER_AUTO && !PortSettingsSave());
    unlink(PortSettingsPath()); rmdir(testDir);
    puts("Renderer defaults, ignored obsolete keys, independent persistence/reset and frame-boundary selection: OK");
    return 0;
}
