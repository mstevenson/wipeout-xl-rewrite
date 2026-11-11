/* The production sidecars and light selector: reuse, frame epochs and stable budgets. */
#include "../../platform/settings.h"
#include "../../platform/render/modern/metadata.h"
#include "../../platform/render/modern/point_lights.h"
#include "../../platform/psx/gpu.h"
#include "psyq.h"
#include <assert.h>
#include <string.h>
PortSettings portSettings;
static uint32_t frame=1;
uint32_t GteFrameRecorded(void) { return frame; }
float GtePresentationAlong(float along) { return along; }
void PortTagTrackLight(const void *, const uint32_t[4]);
void PortTagSparkStreak(const LINE_G2 *);
int main(void)
{
    portSettings.render=(RenderSettings)RENDER_DEFAULTS;
    portSettings.render.selection=RENDER_MODERN; RenderCommit();
    POLY_FT4 packet={.tag=9u<<24}; uint32_t rgb[4]={1,2,3,4}, result[4];
    MaterialEmission emission={.enabled=1,.uv={174,148,11,9}}, found;
    PortTagTrackLight(&packet,rgb); GpuTagEmission(&packet,&packet+1,&emission);
    PortTagSparkStreak((const LINE_G2 *)&packet); PortTagTrackEncoding(&packet,1);
    assert(ModernPacketTrack(&packet,result) && !memcmp(rgb,result,sizeof(rgb)));
    assert(ModernPacketEmission(&packet,&found) && !memcmp(&emission,&found,sizeof(found)));
    assert(ModernSparkPacket(&packet) && ModernPacketPs1Light(&packet));
    ModernPacketReused(&packet);
    assert(!ModernPacketTrack(&packet,result) && !ModernPacketEmission(&packet,&found));
    assert(!ModernSparkPacket(&packet) && !ModernPacketPs1Light(&packet));
    PortTagTrackLight(&packet,rgb); frame++;
    assert(!ModernPacketTrack(&packet,result));
    GpuLightSource sources[GPU_LIGHT_SOURCES]={0};
    for(int i=0;i<GPU_LIGHT_SOURCES;i++) sources[i]=(GpuLightSource){
        .id=i+1,.generation=1,.life=10,.light={{0,0,100},1000,{i,0,0},1}};
    PsxCaptureLights(sources,GPU_LIGHT_SOURCES);
    const GpuLightSet *lights=ModernSelectLights(1);
    assert(lights->count==GPU_POINT_LIGHTS);
    for(int i=0;i<GPU_POINT_LIGHTS;i++) assert(lights->lights[i].colour[0]==i);
    frame++; sources[0].life=9; sources[0].light.position[0]=100;
    PsxCaptureLights(sources,1); lights=ModernSelectLights(.5f);
    assert(lights->lights[0].position[0]==50);
    frame++; sources[0].generation++; sources[0].life=8; sources[0].light.position[0]=200;
    PsxCaptureLights(sources,1); lights=ModernSelectLights(.5f);
    assert(lights->lights[0].position[0]==200);
    portSettings.render.selection=RENDER_EXISTING; RenderCommit();
    assert(ModernSelectLights(.5f)->count==0 && !ModernSourceLight(1));
    ModernMetadataReset(); ModernLightsReset();
    puts("Modern packet reuse/epochs, light budget/interpolation/generations and disabled dispatch: OK");
}
