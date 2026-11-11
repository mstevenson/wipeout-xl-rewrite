/* Production submission, scene shaders and resolve on the selected real device. */
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#define main HudProbeMain
#include "test_gpu_hud.c"
#undef main
static void Quad(float left, float top, float right, float bottom, float depth, uint8_t r, uint8_t g, uint8_t b,
                 GpuDrawState draw)
{
    const float xy[6][2] = {{left,top}, {right,top}, {left,bottom}, {right,top}, {left,bottom}, {right,bottom}};
    GpuVertex v[6] = {0};
    for (int i = 0; i < 6; i++) {
        v[i].x = xy[i][0]; v[i].y = xy[i][1]; v[i].w = depth;
        v[i].r = r; v[i].g = g; v[i].b = b; v[i].a = 255;
        v[i].world = depth > 0.0f;
        v[i].view[0] = (v[i].x - 160) * depth / 160;
        v[i].view[1] = (v[i].y - 120) * depth / 160;
        v[i].view[2] = depth;
        if (draw.hullMotion) v[i].shutterDelta[0] = 20;
    }
    GpuTriangles(&draw, v, 6);
}

static uint8_t Brightness(const uint8_t *rgba, int x, int y)
{
    return rgba[(y * 320 + x) * 4];
}

static void FilteredCutouts(void)
{
    RenderSettings saved = portSettings.render;
    const float samples[] = {0.5f, 1.0f, 1.5f}; /* transparent, half covered, opaque */
    for (int modern = 0; modern <= 1; modern++) {
        for (int filtered = 0; filtered <= 1; filtered++) {
            portSettings.render.selection = modern ? RENDER_MODERN : RENDER_EXISTING;
            portSettings.render.modern.bilinear = filtered;
            RenderCommit();
            GpuSetDrawArea(0, 0, 320, 240, 0, 0);
            for (int depth = 0; depth <= 2; depth++) {
                /* A transparent texel beside opaque white, in all three PS1 formats. */
                psxVram[0][512] = depth == 0 ? 0x10 : depth == 1 ? 0x100 : 0;
                psxVram[0][513] = depth == 2 ? 0x7fff : 0;
                psxVram[0][768] = 0;
                psxVram[0][769] = 0x7fff;
                GpuVramChanged(512, 0, 2, 1);
                GpuVramChanged(768, 0, 2, 1);
                GpuClearArea(0, 0, 320, 240, 0, 0, 0);
                Quad(0, 0, 320, 240, 200, 0, 100, 0, (GpuDrawState){.semi = -1});
                /* A preceding additive primitive must not suppress the cutout's depth pass. */
                Quad(96, 0, 116, 20, 100, 20, 20, 20, (GpuDrawState){.semi = 1});
                GpuDrawState draw = {.textured = 1, .raw = 1, .depth = depth,
                                     .pageX = 512, .clutX = 768, .semi = -1};
                for (int sample = 0; sample < 3; sample++) {
                    float x = sample * 24.0f;
                    GpuVertex v[3] = {
                        {.x = x, .y = 0}, {.x = x + 20, .y = 0}, {.x = x, .y = 20}
                    };
                    for (int i = 0; i < 3; i++) {
                        v[i].w = v[i].view[2] = 100;
                        v[i].world = 1;
                        v[i].u = samples[sample]; v[i].v = 0.5f;
                        v[i].r = v[i].g = v[i].b = 128; v[i].a = 255;
                    }
                    GpuTriangles(&draw, v, 3);
                }
                /* Full coverage still hides geometry drawn later behind it. */
                Quad(48, 0, 68, 20, 150, 255, 0, 0, (GpuDrawState){.semi = -1});
                for (int sample = 0; sample < 3; sample++) {
                    int width, height;
                    const uint8_t *pixel = GpuReadArea(sample * 24 + 2, 2, 1, 1, &width, &height);
                    int half = modern && filtered && sample == 1;
                    int white = sample ? (half ? 124 : 248) : 0;
                    int green = sample ? (half ? 174 : 248) : 100;
                    assert(abs(pixel[0] - white) <= 1 && abs(pixel[1] - green) <= 1 && abs(pixel[2] - white) <= 1);
                }
            }
        }
    }
    portSettings.render = saved;
    RenderCommit();
    puts("Filtered cutouts: transparent, partial and opaque coverage over geometry in all PS1 texture formats: OK");
}

static void OpaqueFilteredTextures(void)
{
    RenderSettings saved = portSettings.render;
    portSettings.render.selection = RENDER_MODERN;
    portSettings.render.modern.bilinear = 1;
    RenderCommit();
    GpuSetDrawArea(0, 0, 320, 240, 0, 0);
    const GpuVertex triangle[3] = {
        {.x = 0, .y = 0, .u = 1, .v = 0.5f, .a = 255, .world = 1, .w = 100},
        {.x = 20, .y = 0, .u = 1, .v = 0.5f, .a = 255, .world = 1, .w = 100},
        {.x = 0, .y = 20, .u = 1, .v = 0.5f, .a = 255, .world = 1, .w = 100}
    };
    for (int depth = 0; depth <= 2; depth++) {
        GpuDrawState draw = {.textured = 1, .raw = 1, .depth = depth,
                             .pageX = 512, .pageY = 32, .clutX = 768, .clutY = 32, .semi = -1};
        for (int y = 32; y < 34; y++) {
            psxVram[y][512] = depth == 0 ? 0x1121 : depth == 1 ? 0x0201 : 0x7fff;
            psxVram[y][513] = depth == 0 ? 0x1111 : depth == 1 ? 0x0101 : 0x7fff;
        }
        psxVram[32][768] = 0; /* Unused transparent palette entry. */
        psxVram[32][769] = psxVram[32][770] = 0x7fff;
        GpuVramChanged(512, 32, 2, 2);
        GpuVramUploaded(512, 32, 2, 2);
        GpuVramChanged(768, 32, 3, 1);
        GpuClearArea(0, 0, 320, 240, 0, 100, 0);
        GpuTriangles(&draw, triangle, 3);
        assert(batchCount == 3 && !batchBlendRuns[0].blend);

        /* Pending geometry samples the new data when the batch uploads. */
        int x = depth == 2 ? 512 : 769;
        psxVram[32][x] = 0;
        GpuVramChanged(x, 32, 1, 1);
        assert(batchBlendDirty);
        int width, height;
        const uint8_t *pixel = GpuReadArea(2, 2, 1, 1, &width, &height);
        assert(abs(pixel[0] - 124) <= 1 && abs(pixel[1] - 174) <= 1 && abs(pixel[2] - 124) <= 1);

        GpuTriangles(&draw, triangle, 3);
        assert(batchBlendRuns[0].blend); /* The old opaque cache entry was invalidated. */
        GpuFlush();
    }
    for (int y = 64; y < 66; y++)
        psxVram[y][512] = psxVram[y][513] = 0x7fff;
    GpuVramChanged(512, 64, 2, 2);
    GpuVramUploaded(512, 64, 2, 2);
    GpuDrawState draw = {.textured = 1, .raw = 1, .depth = 2, .pageX = 512, .pageY = 64, .semi = -1};
    GpuTriangles(&draw, triangle, 3);
    draw.pageY = 32;
    GpuTriangles(&draw, triangle, 3);
    assert(batchCount == 6 && batchDepth && batchBlendRunCount == 2);
    assert(!batchBlendRuns[0].blend && batchBlendRuns[1].first == 3 && batchBlendRuns[1].blend);
    GpuFlush();

    portSettings.render = saved;
    RenderCommit();
    puts("Opaque filtered textures skip blending; texel and palette writes restore cutout coverage: OK");
}

static uint8_t expected[320 * 240 * 4];
static int readCompleted;
static void CheckRead(const uint8_t *rgba, int w, int h, void *unused)
{
    (void)unused;
    assert(w == 320 && h == 240);
    assert(!memcmp(expected, rgba, sizeof(expected)));
    readCompleted++;
}

static void ModernEffects(int canSwitch)
{
    GpuMotionEligible(1);
    portSettings.render.selection = RENDER_MODERN;
    portSettings.render.modern.cameraBlurAmount = 300;
    RenderCommit();
    GpuUpdateResolution();
    GpuSetDrawArea(0, 0, 320, 240, 0, 0);
    PortWideScene();
    GpuClearArea(0, 0, 320, 240, 20, 30, 40);
    GpuDrawState opaque = {.semi = -1};
    GpuDrawState light = {.semi = -1, .sparkStreak = 1, .emission.enabled = 1, .hullMotion = 1};
    Quad(0, 0, 320, 240, 800, 20, 30, 40, opaque);
    Quad(100, 75, 165, 164, 100, 220, 24, 12, light);
    GpuCameraMotion camera = {.earlier = {{1,0,0,16}, {0,1,0,0}, {0,0,1,0}},
                              .centreX = 160, .centreY = 120, .focal = 160};
    GpuCaptureCameraMotion(0, 0, 320, 240, &camera, 0);
    assert(sceneAux.bands == 1 && sceneAux.velocity.texture && cameraMotion[0].revision);
    GpuClearSceneDepth(0, 0, 320, 240, 0);
    Quad(40, 30, 60, 50, 0, 76, 122, 178, opaque);
    GpuCaptureCameraMotion(0, 0, 320, 240, NULL, 1);
    assert(cameraMotion[0].revision == pictureOwners[0].revision);

    RgEnable(RG_BLEND);
    RgEnable(RG_DEPTH_TEST);
    const uint8_t *withEffects = GpuReadPicture(0, 0, 320, 240, 0, 320, 240);
    assert(RgIsEnabled(RG_BLEND) && RgIsEnabled(RG_DEPTH_TEST));
    memcpy(expected, withEffects, sizeof(expected));
    const uint8_t *hud = &expected[(40 * 320 + 50) * 4];
    assert(hud[0] == 76 && hud[1] == 122 && hud[2] == 178);
    assert(Brightness(expected, 95, 120) > 20);

    ModernRenderSettings settings = pictureOwners[0].profile.settings.modern;
    pictureOwners[0].profile.settings.modern.cameraBlur = 0;
    const uint8_t *noMotion = GpuReadPicture(0, 0, 320, 240, 0, 320, 240);
    assert(memcmp(expected, noMotion, sizeof(expected)));
    assert(!memcmp(hud, noMotion + (40 * 320 + 50) * 4, 3));

    pictureOwners[0].profile.settings.modern.bloom = 0;
    const uint8_t *noBloom = GpuReadPicture(0, 0, 320, 240, 0, 320, 240);
    assert(Brightness(noBloom, 95, 120) < Brightness(expected, 95, 120));
    pictureOwners[0].profile.settings.modern = settings;

    if (canSwitch) {
        assert(GpuReadPictureStart(GPU_READ_RECORDER, 0, 0, 320, 240, 0, 320, 240));
        RenderBackend first = RenderActiveBackend();
        assert(RenderRequestBackend(first == RENDER_METAL ? RENDER_OPENGL : RENDER_METAL));
        assert(RenderApplyBackend());
        assert(GpuReadPictureDone(GPU_READ_RECORDER, 1, CheckRead, NULL));
        assert(readCompleted == 1);
        const uint8_t *migrated = GpuReadPicture(0, 0, 320, 240, 0, 320, 240);
        for (size_t i = 0; i < sizeof(expected); i++) assert(abs(expected[i] - migrated[i]) <= 2);

        /* Force the receiving device to resolve again from the migrated HDR, mask,
           velocity and depth images, independently of the cached RGBA8 image. */
        cameraMotion[0].revision = pictureOwners[0].revision = ++nextPictureRevision;
        migrated = GpuReadPicture(0, 0, 320, 240, 0, 320, 240);
        for (size_t i = 0; i < sizeof(expected); i++)
            assert(abs(migrated[i] - expected[i]) <= 2);
        assert(RenderRequestBackend(first) && RenderApplyBackend());
    }
    SetWideMargin(16);
    Quad(-16, 0, 336, 240, 800, 20, 30, 40, opaque);
    Quad(100, 75, 165, 164, 100, 220, 24, 12, light);
    GpuCaptureCameraMotion(0, 0, 320, 240, &camera, 0);
    assert(cameraMotion[0].wide && cameraMotion[0].region.w == 352);
    const uint8_t *wide = GpuReadPicture(0, 0, 320, 240, 16, 352, 240);
    assert(wide[(120 * 352 + 111) * 4] > 20);
    SetWideMargin(0);
    GpuMotionEligible(0);
    puts("Modern HDR, bloom, shutter motion, HUD protection and widescreen: OK");
    if (canSwitch) puts("Modern in-flight backend switch: OK");
}

static void ToneFixtures(void)
{
    ModernRenderSettings saved = portSettings.render.modern;
    portSettings.render.modern.bloom = portSettings.render.modern.cameraBlur = 0;
    portSettings.render.modern.tonemapper = MODERN_TONEMAP_ORIGINAL;
    RenderCommit();
    PortWideScene();
    GpuSetDrawArea(0, 0, 320, 240, 0, 0);
    GpuClearArea(0, 0, 320, 240, 0, 0, 0);
    GpuDrawState opaque = {.semi = -1};
    Quad(0, 0, 100, 100, 100, 179, 130, 90, opaque);
    Quad(100, 0, 200, 100, 100, 60, 60, 60, opaque);
    Quad(200, 0, 300, 100, 100, 255, 255, 255, opaque);
    const uint8_t *pixels = GpuReadPicture(0, 0, 320, 240, 0, 320, 240);
    int mid = (50 * 320 + 50) * 4, dark = (50 * 320 + 150) * 4, white = (50 * 320 + 250) * 4;
    assert(pixels[mid] == 179 && pixels[mid+2] == 90 && pixels[dark] == 60);
    assert(abs(pixels[white] - 249) <= 1);
    pictureOwners[0].profile.settings.modern.tonemapper = MODERN_TONEMAP_SOFT_SHADOWS;
    pixels = GpuReadPicture(0, 0, 320, 240, 0, 320, 240);
    assert(abs(pixels[mid] - 177) <= 1 && abs(pixels[mid+2] - 86) <= 1 && pixels[dark] == 60);
    assert(abs(pixels[white] - 240) <= 1);
    portSettings.render.modern = saved; RenderCommit();
    puts("Production Classic/Soft Highlights numeric midtone, dark and white fixtures: OK");
}

static void Scene(int y)
{
    GpuSetDrawArea(0,y,320,240,0,y);
    GpuFill(0,y,320,240,32,32,32);
    PortWideScene();
    GpuClearSceneDepth(0,y,320,240,1);
    GpuVertex v[3] = {
        {.x=20,.y=y+20,.w=100,.r=128,.g=128,.b=128,.a=255,.world=1,.view={-20,-20,100}},
        {.x=300,.y=y+20,.w=100,.r=128,.g=128,.b=128,.a=255,.world=1,.view={20,-20,100}},
        {.x=20,.y=y+220,.w=100,.r=128,.g=128,.b=128,.a=255,.world=1,.view={-20,20,100}}
    };
    GpuLightSet lights={.count=1,.lights={{{0,0,20},500,{1,.2f,.1f},3}}};
    GpuDrawState draw={.semi=-1,.pointLights=&lights};
    GpuTriangles(&draw,v,3); GpuFlush();
}
int main(void)
{
    assert(SDL_Init(SDL_INIT_VIDEO));
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
#ifdef __EMSCRIPTEN__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
#endif
    SDL_Window *window=SDL_CreateWindow("Modern production",320,240,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    assert(window); SDL_GLContext context=SDL_GL_CreateContext(window); assert(context);
    portSettings.renderScale=1;
    portSettings.render=(RenderSettings)RENDER_DEFAULTS;
    portSettings.render.selection=RENDER_MODERN;
    RenderCommit();
#ifdef __EMSCRIPTEN__
    assert(RenderStartBackend(window,RENDER_WEBGL));
#else
    assert(RenderStartBackend(window,RENDER_OPENGL));
#endif
    assert(GpuInit());
    /* Both paths use the global resolution, including changes while Modern is active. */
    for (int mode = RENDER_EXISTING; mode <= RENDER_MODERN; mode++) {
        portSettings.render.selection = mode;
        RenderCommit();
        for (int requested = 2; requested >= 1; requested--) {
            portSettings.renderScale = requested;
            GpuUpdateResolution();
            assert(GpuScale() == requested);
        }
    }
#ifdef __EMSCRIPTEN__
    EM_ASM({
        var info = GLctx.getExtension('WEBGL_debug_renderer_info');
        out('WebGL device: ' + GLctx.getParameter(info ? info.UNMASKED_RENDERER_WEBGL : GLctx.RENDERER));
    });
#endif
    if (SDL_getenv("WIPEOUT_TEST_METAL")) { assert(RenderRequestBackend(RENDER_METAL)); assert(RenderApplyBackend()); }
    FilteredCutouts();
    OpaqueFilteredTextures();
    ToneFixtures();
    ModernEffects(RenderBackendAvailable(RENDER_METAL));
    Scene(0);
    assert(sceneAux.hdr.width && sceneAux.mask.width);
    int w,h; uint8_t *raw=GpuReadArea(60,60,1,1,&w,&h); uint8_t before[4]; memcpy(before,raw,4);
    uint8_t *picture=GpuReadPicture(0,0,320,240,0,320,240); assert(picture);
    uint8_t *copy=malloc(320*240*4); assert(copy); memcpy(copy,picture,320*240*4);
    uint64_t resolves = ModernResolveCount();
    picture=GpuReadPicture(0,0,320,240,0,320,240); assert(!memcmp(copy,picture,320*240*4));
    assert(ModernResolveCount() == resolves);
    raw=GpuReadArea(60,60,1,1,&w,&h); assert(!memcmp(before,raw,4));
    /* A changed back picture does not adopt the front picture's tone policy. */
    portSettings.render.modern.tonemapper=MODERN_TONEMAP_ORIGINAL;
    RenderCommit(); Scene(256);
    picture=GpuReadPicture(0,0,320,240,0,320,240); assert(!memcmp(copy,picture,320*240*4));
    /* Keep Modern materials active while all post/lighting requests become off.
       The old front still owns HDR until its band has also been replaced. */
    ModernRenderSettings enabled = portSettings.render.modern;
    portSettings.render.modern.bloom = portSettings.render.modern.weaponLights = portSettings.render.modern.cameraBlur = 0;
    RenderCommit(); Scene(256); assert(sceneAux.hdr.texture);
    Scene(0); assert(!sceneAux.hdr.texture && !wideAux.hdr.texture);
    portSettings.render.modern = enabled; RenderCommit(); Scene(0); Scene(256);
    /* Both bands must become Existing before optional attachments can be trimmed. */
    portSettings.render.selection=RENDER_EXISTING; RenderCommit(); Scene(0); Scene(256);
    assert(!sceneAux.hdr.texture && !wideAux.hdr.texture);
    /* Inject unsupported HDR on the real host path. Geometry and SDR survive. */
    RenderDevice *device = (RenderDevice *)RenderActiveDevice();
    int capability = device->caps.hdrTargets; device->caps.hdrTargets = 0;
    portSettings.render.selection = RENDER_MODERN; RenderCommit(); Scene(0);
    assert(GpuModernFailures() & 8); assert(!sceneAux.hdr.texture);
    assert(GpuReadPicture(0,0,320,240,0,320,240));
    device->caps.hdrTargets = capability;
    free(copy); GpuRendererShutdown(); RenderStop();
    SDL_GL_DestroyContext(context); SDL_DestroyWindow(window); SDL_Quit();
    puts("Modern production: HDR lights, resolve, raw SDR, repeated/front-buffer capture and disable cleanup: OK");
}
