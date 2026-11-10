/* Exercise the actual shared renderer and both device drivers with synthetic pictures. */
#include "../../platform/psx/gpu_renderer.c"

#include <assert.h>
#include <stdio.h>

PortConfig portConfig;
PortSettings portSettings = {.render = RENDER_DEFAULTS};
uint16_t psxVram[VRAM_HEIGHT][VRAM_WIDTH], psxVramHigh[VRAM_HEIGHT][VRAM_WIDTH];
int portPal;

#include "gpu_test_settings.h"

void PortDrawableSize(int *w, int *h) { *w = 320; *h = 240; }
void HdTexSetScale(int n) {}
int HdTexActive(void) { return 0; }
unsigned HdTexEpoch(void) { return 0; }
int HdTexImageCount(void) { return 0; }
const uint8_t *HdTexImage(int index, int *w, int *h) { return NULL; }
int HdTexFind(int pageX, int pageY, int depth, int clutX, int clutY, float u0, float v0, float u1, float v1, HdTexHit *hit)
{ return 0; }
void HdTexChanged(int x, int y, int w, int h) {}
void HdTexOverwritten(int x, int y, int w, int h) {}

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

static uint8_t expected[320 * 240 * 4];
static int readCompleted;
static void CheckRead(const uint8_t *rgba, int w, int h, void *unused)
{
    (void)unused;
    assert(w == 320 && h == 240);
    assert(!memcmp(expected, rgba, sizeof(expected)));
    readCompleted++;
}

static void Modern(int canSwitch)
{
    portSettings.render.selection = RENDER_MODERN;
    portSettings.render.modern.scale = 1;
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
    GpuClearDepth(0, 0, 320, 240, 0);
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
        assert(!memcmp(expected, migrated, sizeof(expected)));

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
    puts("Modern HDR, bloom, shutter motion, HUD protection, widescreen and in-flight backend switch: OK");
}

static void Saturn(void)
{
    portSettings.render.selection = RENDER_SATURN;
    portSettings.render.saturn.scale = 1;
    RenderCommit();
    GpuUpdateResolution();
    GpuSetDrawArea(0, 0, 320, 240, 0, 0);
    GpuClearArea(0, 0, 320, 240, 24, 24, 24);

    uint8_t skyPixels[] = {1, 1, 1, 1};
    uint16_t skyPalette[256] = {[1] = 0x6000};
    SaturnNativeSky(skyPixels, skyPalette, 2, 2);
    Quad(0, 0, 320, 240, 0, 0, 0, 0, (GpuDrawState){.semi = -1, .saturnSky = 1});
    int w, h;
    uint8_t *pixel = GpuReadArea(160, 140, 1, 1, &w, &h);
    assert(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 192);

    uint8_t texels[] = {1, 0, 2, 1};
    SaturnNativePoly poly = {.mode = 0x88, .width = 2, .height = 2, .textured = 1,
                             .corners = {0,1,2,3}, .pixels = texels, .palette = {[1]=31,[2]=31<<5}};
    GpuVertex corners[4] = {{.x=40,.y=40}, {.x=80,.y=40}, {.x=80,.y=80}, {.x=40,.y=80}};
    for (int i = 0; i < 4; i++) corners[i].a = 255;
    GpuDrawState native = {.semi = -1, .saturn = &poly};
    GpuTriangles(&native, corners, 4);
    assert(rowCount > 0);
    pixel = GpuReadArea(46, 45, 1, 1, &w, &h);
    assert(pixel[0] == 248 && pixel[1] == 0 && pixel[2] == 0);
    pixel = GpuReadArea(74, 45, 1, 1, &w, &h);
    assert(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 192);
    pixel = GpuReadArea(46, 74, 1, 1, &w, &h);
    assert(pixel[0] == 0 && pixel[1] == 248 && pixel[2] == 0);
    SDL_Environment *env = SDL_GetEnvironment();
    assert(SDL_SetEnvironmentVariable(env, "WIPEOUT_SATURN_REFERENCE", "1", 1));
    GpuClearArea(0, 0, 320, 240, 0, 0, 192);
    GpuTriangles(&native, corners, 4);
    assert(rowCount == 0);
    pixel = GpuReadArea(46, 45, 1, 1, &w, &h);
    assert(pixel[0] == 248 && pixel[1] == 0 && pixel[2] == 0);
    pixel = GpuReadArea(74, 45, 1, 1, &w, &h);
    assert(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 192);
    assert(SDL_UnsetEnvironmentVariable(env, "WIPEOUT_SATURN_REFERENCE"));
    puts("Saturn VDP2 sky and both VDP1 quad paths preserve indexed colour and transparent holes: OK");
}

int main(void)
{
    assert(SDL_Init(SDL_INIT_VIDEO));
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window *window = SDL_CreateWindow("Profile device regression", 320, 240, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    assert(window);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    assert(context);
    SDL_SetPointerProperty(SDL_GetWindowProperties(window), "wipeout.opengl.context", context);
    RenderBackend backend = SDL_getenv("WIPEOUT_TEST_METAL") ? RENDER_METAL : RENDER_OPENGL;
    assert(RenderStartBackend(window, backend));
    assert(GpuInit());
    Modern(RenderBackendAvailable(RENDER_METAL));
    Saturn();
    RenderStop();
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
