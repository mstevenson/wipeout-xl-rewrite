/* Real settings migration/persistence without touching the player's file. */
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
static void Load(const char *contents)
{
    FILE *file = fopen(PortSettingsPath(), "w"); assert(file);
    fputs(contents, file); fclose(file); PortSettingsLoad();
}
static void Policies(void)
{
    RenderFrame f = {.settings = RENDER_DEFAULTS, .id = RENDER_PS1};
    RenderMaterial m = {.textured = 1, .spatial = 1, .dither = 1};
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
    f.id = RENDER_PC;
    assert(RenderPrepare(&f, &m).filter == 2);
    f.settings.pc.bilinear = 0;
    assert(RenderPrepare(&f, &m).filter == 0);
    f.id = RENDER_N64;
    assert(RenderPrepare(&f, &m).filter == 1); /* explicit N64 over PS1 assets */
    f.settings.n64.threePoint = 0;
    assert(RenderPrepare(&f, &m).filter == 0);
    m.n64 = 1;
    for (int id = RENDER_PS1; id <= RENDER_N64; id++) {
        f.id = (RendererId)id;
        assert(RenderPrepare(&f, &m).shade & SHADE_N64); /* encoding survives every renderer */
    }
    f.id = RENDER_N64; f.n64Track = 1; m.n64 = 0;
    a = RenderPrepare(&f, &m);
    assert(!(a.shade & (SHADE_SNAP | SHADE_AFFINE_UV | SHADE_PS1_COLOUR | SHADE_DITHER | SHADE_PC_TINT)));
    assert(!RenderGet(RENDER_PS1)->depth(&f.settings.ps1) && RenderGet(RENDER_PC)->depth(&f.settings.pc) && RenderGet(RENDER_N64)->depth(&f.settings.n64));
    f.settings.ps1.depth = 1; f.settings.pc.depth = 0;
    assert(RenderGet(RENDER_PS1)->depth(&f.settings.ps1) && !RenderGet(RENDER_PC)->depth(&f.settings.pc) && RenderGet(RENDER_N64)->depth(&f.settings.n64));
    f.settings = (RenderSettings){0};
    for (f.n64Track = 0; f.n64Track <= 1; f.n64Track++) {
        assert(RenderGet(RENDER_N64)->depth(&f.settings.n64)); /* independent of settings and source track */
    }
}

static void SaturnProfile(void)
{
    Load("renderer = saturn\nsaturn_depth = 1\nsaturn_mesh = 0\n");
    assert(RenderCurrent()->id == RENDER_SATURN && RenderDepth(RenderCurrent()));
    assert(!RenderCurrent()->settings.saturn.cameraAlignedSky);
    RenderMaterial m = {.textured = 1, .spatial = 1, .dither = 1};
    RenderStyle style = RenderPrepare(RenderCurrent(), &m);
    assert(style.filter == RENDER_SAMPLE_SATURN_ALPHA && !(style.shade & SHADE_DITHER));
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
    assert(RenderPrepare(RenderCurrent(), &m).filter == RENDER_SAMPLE_MESH);
    m.ui = 1;
    assert(RenderPrepare(RenderCurrent(), &m).filter == 0);
    m.n64 = 1;
    assert(RenderPrepare(RenderCurrent(), &m).shade & SHADE_N64);
    Load("renderer = saturn\nsaturn_original_sky_scroll = 0\n");
    assert(!RenderCurrent()->settings.saturn.cameraAlignedSky);
    Load("renderer = saturn\nsaturn_original_sky_scroll = 1\n");
    assert(!RenderCurrent()->settings.saturn.cameraAlignedSky);
    Load("renderer = auto\nautomatic_renderer = saturn\n");
    n64 = 1; RenderCommit(); assert(RenderCurrent()->id == RENDER_N64);
    n64 = 0; RenderCommit(); assert(RenderCurrent()->id == RENDER_SATURN);
    SDL_Environment *env = SDL_GetEnvironment();
    SDL_SetEnvironmentVariable(env, "WIPEOUT_RENDERER", "saturn", true);
    Load("renderer = pc\n"); assert(RenderCurrent()->id == RENDER_SATURN);
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_RENDERER");
}

static void DisplayResampling(void)
{
    Load(""); /* Missing display settings use each renderer's defaults. */
    assert(!portSettings.render.ps1.resample && portSettings.render.pc.resample);
    assert(portSettings.render.n64.resample && !portSettings.render.saturn.resample);
    Load("renderer = ps1\nps1_resample = 0\npc_resample = 0\nsaturn_resample = 1\nn64_resample = 0\n");
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
    Load("ps1_resample = bogus\npc_resample = -1\nsaturn_resample = 2\n");
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
        {.spatial = 1, .dither = 1}, /* XL untextured particle */
        {.textured = 1, .spatial = 1, .dither = 1}, /* XL ship/textured effect */
        {.textured = 1, .raw = 1},
        {.fullColour = 1, .textured = 1, .ui = 1}, /* platform glyph */
        {.n64 = 1, .textured = 1, .spatial = 1, .bounds = 1, .dither = 1},
        {.n64 = 1, .textured = 1, .spatial = 1}, /* missing bounds never selects a different renderer */
        {.fullColour = 1, .textured = 1, .spatial = 1}, /* full-colour world material */
        {.n64 = 1, .textured = 1, .bounds = 1, .ui = 1}, /* even N64-tagged UI stays point sampled */
        {.textured = 1} /* affine world billboard: no perspective depth, but not UI */
    };
    for (int id = RENDER_PS1; id <= RENDER_N64; id++) {
        for (int enabled = 0; enabled < 2; enabled++) {
            RenderFrame f = {.settings = RENDER_DEFAULTS, .id = (RendererId)id};
            for (size_t key = 0; key < sizeof(keys) / sizeof(keys[0]); key++)
                assert(RenderSet(&f.settings, keys[key], enabled ? "1" : "0"));
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
                    RenderFrame changed = f;
                    assert(RenderSet(&changed.settings, keys[key], enabled ? "0" : "1"));
                    for (changed.n64Track = 0; changed.n64Track <= 1; changed.n64Track++) {
                        RenderStyle actual = RenderPrepare(&changed, m);
                        assert(actual.shade == baseline.shade && actual.filter == baseline.filter);
                        assert(RenderDepth(&changed) == depth);
                    }
                }
            }
            portSettings.render = f.settings;
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

static void PlatformSelection(void)
{
    Load("renderer = auto\nautomatic_renderer = ps1\n");
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
    Load("renderer = pc\n"); /* Legacy global choice does not override Saturn's default. */
    pcData = 0; saturnDisc = 1;
    assert(RenderCommit() && RenderCurrent()->id == RENDER_SATURN);
    Load("ps1_renderer = n64\nn64_renderer = saturn\nrenderer = pc\nautomatic_renderer = ps1\n"
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
    SDL_Environment *env = SDL_GetEnvironment();
    SDL_SetEnvironmentVariable(env, "WIPEOUT_RENDERER", "saturn", true);
    Load("saturn_renderer = pc\n");
    assert(RenderCurrent()->id == RENDER_SATURN);
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_RENDERER");
    discLoaded = saturnDisc = pcData = n64 = 0;
    Load("ps1_renderer = auto\npc_renderer = bad\nsaturn_renderer = -1\nn64_renderer = auto\n");
    assert(portSettings.render.ps1Selection == RENDER_PS1 && portSettings.render.pcSelection == RENDER_PC);
    assert(portSettings.render.saturnSelection == RENDER_SATURN && portSettings.render.n64Selection == RENDER_N64);
    puts("Platform routing, imported tracks, migration precedence and independent persistence: OK");
}

static void ProfileResolutions(void)
{
    SDL_Environment *env = SDL_GetEnvironment();
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_RENDER_SCALE");
    Load("");
    for (int id = RENDER_PS1; id <= RENDER_SATURN; id++) {
        assert(*RenderScaleSetting(&portSettings.render, (RendererId)id) == 1);
        for (int pal = 0; pal <= 1; pal++) {
            int w, h;
            RenderNativeSize((RendererId)id, pal, &w, &h);
            assert(w == (id == RENDER_PC ? 640 : id == RENDER_SATURN ? 352 : 320));
            assert(h == (id == RENDER_PC ? 480 : id == RENDER_PS1 && pal ? 256 : 240));
        }
    }
    Load("render_scale = 0\n");
    assert(portSettings.render.saturn.scale == 1 && portSettings.render.pc.scale == 1);
    Load("ps1_render_scale = 3\npc_render_scale = 0\nsaturn_render_scale = 2\nrender_scale = 4\n");
    assert(portSettings.render.ps1.scale == 3 && portSettings.render.pc.scale == 0);
    assert(portSettings.render.saturn.scale == 2 && portSettings.render.n64.scale == 4);
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
    Load("pc_render_scale = bad\nn64_render_scale = -1\nsaturn_render_scale = 17\nps1_render_scale = 2x\n");
    assert(portSettings.render.ps1.scale == 1 && portSettings.render.pc.scale == 1);
    assert(portSettings.render.n64.scale == 1 && portSettings.render.saturn.scale == 1);
    SDL_SetEnvironmentVariable(env, "WIPEOUT_RENDER_SCALE", "2", true);
    Load("ps1_render_scale = 1\npc_render_scale = 4\n");
    assert(portSettings.render.ps1.scale == 2 && portSettings.render.pc.scale == 2);
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_RENDER_SCALE");
    puts("Per-profile native sizes, scale defaults, migration, persistence, reset and frame boundaries: OK");
}

static void OriginalFrameRates(void)
{
    Load("ps1_renderer = saturn\nn64_renderer = pc\n");
    n64 = 0; RenderCommit();
    assert(RenderOriginalFrameRate(0) == 20 && RenderOriginalFrameRate(1) == 20);
    n64 = 1; RenderCommit();
    assert(RenderOriginalFrameRate(0) == 60 && RenderOriginalFrameRate(1) == 60);
    portSettings.render.n64Selection = RENDER_N64;
    assert(RenderOriginalFrameRate(0) == 60); /* committed profile only */
    RenderCommit();
    assert(RenderOriginalFrameRate(0) == 30 && RenderOriginalFrameRate(1) == 30);
    portSettings.render.selection = RENDER_PS1; RenderCommit();
    assert(RenderOriginalFrameRate(0) == 30 && RenderOriginalFrameRate(1) == 25);
    n64 = 0; RenderCommit();
    assert(RenderOriginalFrameRate(1) == 25);
    puts("Original frame rate follows the committed renderer, including cross-platform overrides: OK");
}

static void NativeViewLimits(void)
{
    const int road[] = {50, 45, 40, 36, 32, 28, 24, 20};
    const int scenery[] = {36000, 29000, 23000, 17000, 12000, 8000, 5000, 3000};
    RenderFrame frame = {.settings = RENDER_DEFAULTS, .id = RENDER_PC};
    int sections, distance;
    for (int i = 0; i < PC_DRAW_DISTANCE_LEVELS; i++) {
        frame.settings.pc.drawDistance = i + 1;
        assert(RenderPcViewLimits(&frame, &sections, &distance));
        assert(sections == road[i] && distance == scenery[i]);
    }
    for (int id = RENDER_PS1; id <= RENDER_SATURN; id++) {
        if (id == RENDER_PC) continue;
        frame.id = (RendererId)id;
        assert(!RenderPcViewLimits(&frame, &sections, &distance));
    }
    puts("PC native road and scenery limits follow F3 and the active profile: OK");
}

int main(void)
{
    NativeViewLimits();
    Policies();
    ProfileIsolation();
    SDL_Environment *env = SDL_GetEnvironment();
    assert(mkdtemp(testDir));
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_PC_RENDERING");
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_SMOOTH_TEXTURES");
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_RENDERER");
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_DEPTH_BUFFER");
    PortSettingsLoad();
    assert(RenderCurrent()->id == RENDER_PS1 && !portSettings.render.ps1.depth);
    assert(portSettings.render.pc.rgb565);
    assert(portSettings.render.pc.drawDistance == 1);
    Load("pc_mipmapping = 1\npc_rgb565 = 0\npc_draw_distance = 8\n");
    assert(!portSettings.render.pc.rgb565 && portSettings.render.pc.drawDistance == 8);
    assert(PortSettingsSave());
    char *acceleratorSettings = SDL_LoadFile(PortSettingsPath(), NULL);
    assert(acceleratorSettings && !strstr(acceleratorSettings, "pc_mipmapping"));
    assert(strstr(acceleratorSettings, "pc_draw_distance = 8\n"));
    SDL_free(acceleratorSettings);
    PortSettingsLoad();
    assert(!portSettings.render.pc.rgb565 && portSettings.render.pc.drawDistance == 8);
    RenderDefaults(&portSettings.render, RENDER_PC);
    assert(portSettings.render.pc.rgb565 && portSettings.render.pc.drawDistance == 1);
    Load("pc_draw_distance = 9\n");
    assert(portSettings.render.pc.drawDistance == 1);
    Load("smooth_textures = 1\n"); assert(RenderCurrent()->id == RENDER_PC);
    Load("pc_rendering = 0\nsmooth_textures = 1\n"); assert(RenderCurrent()->id != RENDER_PC);
    Load("smooth_textures = 1\npc_rendering = 0\n"); assert(RenderCurrent()->id != RENDER_PC);
    Load("automatic_renderer = ps1\npc_rendering = 1\n"); assert(RenderCurrent()->id != RENDER_PC);
    Load("pc_depth = 1\ndepth_buffer = 0\n");
    assert(portSettings.render.pc.depth && !portSettings.render.ps1.depth);
    Load("depth_buffer = 0\npc_depth = 1\n");
    assert(portSettings.render.pc.depth);
    Load("renderer = n64\ndepth_buffer = 0\nn64_depth = 0\n");
    assert(!portSettings.render.pc.depth && RenderCurrent()->id == RENDER_PS1);
    n64 = 1; RenderCommit(); assert(RenderCurrent()->id == RENDER_N64);
    assert(RenderDepth(RenderCurrent()));
    n64 = 0;
    SDL_SetEnvironmentVariable(env, "WIPEOUT_PC_RENDERING", "1", true);
    Load("pc_rendering = 0\n"); assert(RenderCurrent()->id == RENDER_PC);
    n64 = 1; RenderCommit(); assert(RenderCurrent()->id == RENDER_N64);
    SDL_SetEnvironmentVariable(env, "WIPEOUT_DEPTH_BUFFER", "0", true);
    Load("pc_depth = 1\n");
    assert(!portSettings.render.pc.depth && RenderCurrent()->id == RENDER_N64);
    assert(RenderDepth(RenderCurrent()));
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_DEPTH_BUFFER");
    SDL_SetEnvironmentVariable(env, "WIPEOUT_RENDERER", "pc", true);
    Load(""); assert(RenderCurrent()->id == RENDER_PC); /* explicit overrides guest identity */
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_PC_RENDERING");
    SDL_UnsetEnvironmentVariable(env, "WIPEOUT_RENDERER");
    Load("renderer = ps1\nps1_quantize_vertices = 0\nps1_dither = 0\npc_bilinear = 0\nn64_divot = 0\n");
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
    char *saved = SDL_LoadFile(PortSettingsPath(), NULL); assert(saved);
    assert(!strstr(saved, "n64_depth"));
    SDL_free(saved);
    RenderSettings expected = portSettings.render;
    PortSettingsLoad();
    assert(!memcmp(&expected, &portSettings.render, sizeof(expected)));
    for (int pc = 0; pc <= 1; pc++) {
        for (int override = 0; override <= 1; override++) {
            char text[128];
            const char *base = pc ? "pc" : "ps1";
            snprintf(text, sizeof(text), "renderer = %s\nautomatic_renderer = %s\n",
                     override ? "auto" : base, base);
            Load(text);
            expected = portSettings.render;
            assert(PortSettingsSave());
            PortSettingsLoad();
            assert(!memcmp(&expected, &portSettings.render, sizeof(expected)));
            for (n64 = 0; n64 <= 1; n64++) {
                RenderCommit();
                assert(RenderCurrent()->id == (n64 && override ? RENDER_N64 : pc ? RENDER_PC : RENDER_PS1));
            }
        }
    }
    n64 = 0;
    Load("renderer = bogus\nps1_dither = garbage\npc_depth = -1\n");
    assert(portSettings.render.selection == RENDER_AUTO && portSettings.render.ps1.dither && portSettings.render.pc.depth);
    ProfileResolutions();
    PlatformSelection();
    OriginalFrameRates();
    SaturnProfile();
    DisplayResampling();
    portConfig.hidden = 1; PortSettingsLoad();
    assert(portSettings.render.selection == RENDER_AUTO && !PortSettingsSave());
    unlink(PortSettingsPath()); rmdir(testDir);
    puts("Renderer defaults, legacy migration, precedence, independent persistence/reset and frame-boundary selection: OK");
    return 0;
}
