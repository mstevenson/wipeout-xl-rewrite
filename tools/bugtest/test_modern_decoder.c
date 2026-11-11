/* Recorder semantics survive the compact disabled decoder state and mode changes. */
#include "../../platform/psx/libgpu.c"
#include <assert.h>

PortSettings portSettings;
PortConfig portConfig;
static int keepPast;
int PortLinkRole(void) { return LINK_NONE; }
int PortLinkSplit(void) { return 0; }
int PortLinkInterpolate(void) { return 0; }
void PortLinkSetInterpolate(int on) { (void)on; }
int PortTracing(void) { return 0; }
void GteKeepPast(int on) { keepPast = on; }
static FrameCapPrim captured;
FrameCapPrim *FrameCapAdd(void) { return &captured; }
void FrameCapTagRecord(FrameCapPrim *primitive) { (void)primitive; }

int main(void)
{
    unsetenv("WIPEOUT_RENDERER_MODE");
    unsetenv("WIPEOUT_RENDER_PROFILE");
    unsetenv("WIPEOUT_INTERPOLATE");
    portSettings.render = (RenderSettings)RENDER_DEFAULTS;
    uint32_t packet[16] = {0};
    GpuVertex corners[3] = {{.n64 = 1}, {.n64 = 1}, {.n64 = 1}};
    for (int modern = 0; modern < 3; modern++) {
        portSettings.render.selection = modern == 1 ? RENDER_MODERN : RENDER_EXISTING;
        RenderCommit();
        for (int rate = FRAME_RATE_UNCAPPED; rate <= FRAME_RATE_CONSOLE; rate++) {
            portSettings.frameRate = rate;
            assert(PsxInterpolationOn() == (rate != FRAME_RATE_CONSOLE));
            /* Modern may retain blur history without enabling interpolated presentation. */
            assert(keepPast == (rate != FRAME_RATE_CONSOLE || modern == 1));
        }
        GpuDrawState draw;
        memset(&draw, 0xa5, sizeof(draw));
        hudColourOverlay = 1;
        StateFor(&draw, 1, 0, 0);
        Record(FRAMECAP_POLYGON, 0x24, 3, NULL, NULL, corners, &draw, 0, packet + 1, 0, 0);
        assert(captured.draw.textured && captured.draw.semi == -1);
        assert(captured.draw.hudColourOverlay);
        assert(captured.draw.n64 == (modern == 1));
        assert(!captured.draw.emission.enabled && !captured.draw.sparkStreak);
        assert(!captured.draw.smoothTrackLighting && !captured.draw.hullMotion);
        assert(!captured.draw.pointLights && !captured.draw.ps1Light);
    }
    puts("Decoder: global frame rate, disabled capture state, imported encoding and mode changes: OK");
}
