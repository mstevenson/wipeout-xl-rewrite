#include "../../platform/settings.h"
#include "../../platform/render/modern/modern.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
PortSettings portSettings;
static int flushed, expectedOld;
static void FlushExisting(void) { assert(ModernEnabled() == expectedOld); flushed++; }
int main(void)
{
    unsetenv("WIPEOUT_RENDER_PROFILE"); unsetenv("WIPEOUT_RENDERER_MODE");
    portSettings.render=(RenderSettings)RENDER_DEFAULTS;
    assert(RenderCommit());
    RenderFrame first=*RenderCurrent();
    RenderSet(&portSettings.render,"modern_brightness","67");
    assert(!RenderCommit() && first.generation==RenderCurrent()->generation);
    RenderSet(&portSettings.render,"renderer_mode","modern");
    assert(!ModernEnabled()); /* requested versus committed */
    assert(RenderCommitBefore(FlushExisting) && ModernEnabled());
    assert(flushed == 1);
    assert(!RenderCommitBefore(FlushExisting) && flushed == 1);
    assert(RenderCurrent()->settings.modern.brightness==67);
    /* Old Modern-only keys must not resurrect overrides or alter global preferences. */
    const char *removed[] = {"modern_render_scale", "modern_frame_rate", "modern_frame_cap",
                            "modern_far_view", "modern_fade", "modern_fade_near", "modern_fade_far",
                            "modern_far_flares", "modern_custom_distance", "modern_track_detail"};
    portSettings.renderScale = 3;
    portSettings.frameRate = FRAME_RATE_CONSOLE;
    portSettings.frameCap = 80;
    portSettings.farView = 0;
    portSettings.fadeNear = 12000;
    portSettings.fadeFar = 24000;
    PortSettings saved = portSettings;
    for (size_t i = 0; i < sizeof(removed) / sizeof(*removed); i++)
        assert(!RenderSet(&portSettings.render, removed[i], "999999"));
    assert(!memcmp(&saved, &portSettings, sizeof(saved)));
    RenderSet(&portSettings.render,"modern_brightness","garbage");
    assert(portSettings.render.modern.brightness==67);
    RenderSet(&portSettings.render,"modern_brightness","999999");
    assert(portSettings.render.modern.brightness==100);
    setenv("WIPEOUT_MODERN_BLOOM","0",1); RenderCommit();
    assert(!RenderCurrent()->settings.modern.bloom && portSettings.render.modern.bloom);
    FILE *f=tmpfile(); assert(f); RenderSave(f,&portSettings.render); rewind(f);
    RenderSettings loaded=RENDER_DEFAULTS; char line[256],key[128],value[128];
    while(fgets(line,sizeof(line),f)) if(sscanf(line,"%127s = %127s",key,value)==2) assert(RenderSet(&loaded,key,value));
    fclose(f); assert(!memcmp(&loaded,&portSettings.render,sizeof(loaded)));
    ModernReset(&loaded); assert(loaded.selection==RENDER_MODERN && loaded.modern.brightness==85);
    portSettings.render.selection=RENDER_EXISTING; RenderCommit();
    assert(!RenderResourcesFor(RenderCurrent()).hdr);
    portSettings.render.selection=RENDER_MODERN;
    RenderInvalidate();
    assert(ModernEnabled()); /* a cold mode read initializes the same frame */
    portSettings.render.selection=RENDER_EXISTING;
    assert(ModernEnabled()); /* requests do not publish the hot-path bit */
    expectedOld = 1;
    RenderInvalidate();
    assert(RenderCommitBefore(FlushExisting) && !ModernEnabled());
    assert(flushed == 2);
    ModernReset(&portSettings.render);
    saved.render = portSettings.render;
    assert(!memcmp(&saved, &portSettings, sizeof(saved))); /* toggles/reset preserve shared settings */
    unsetenv("WIPEOUT_MODERN_BLOOM");
    puts("Modern settings: opt-in, commit, inactive edits, defaults, bounds, override and round-trip: OK");
}
