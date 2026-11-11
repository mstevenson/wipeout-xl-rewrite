#include "point_lights.h"
#include "modern.h"
#include "../../psx/gte.h"
#include <SDL3/SDL.h>
#include <string.h>
static struct {
    uint32_t frame;
    int count;
    GpuLightSource sources[GPU_LIGHT_SOURCES];
} lightFrame, previousLights;
static GpuLightSet drawnLights;

void PsxCaptureLights(const GpuLightSource *sources, int count)
{
    uint32_t frame = GteFrameRecorded();
    if (lightFrame.frame != frame) previousLights = lightFrame;
    lightFrame.frame = frame;
    lightFrame.count = SDL_clamp(count, 0, GPU_LIGHT_SOURCES);
    if (lightFrame.count) memcpy(lightFrame.sources, sources, lightFrame.count * sizeof(*sources));
}

/* A draw pass owns its list until all batches have flushed. Select by influence
   near the camera, with a stable source-order tie break; offscreen emitters can
   still illuminate visible surfaces. Each linked game has its own snapshots. */
const GpuLightSet *ModernSelectLights(float along)
{
    drawnLights.count = 0;
    if (!ModernEnabled() || !RenderCurrent()->settings.modern.weaponLights ||
        lightFrame.frame != GteFrameRecorded() || !lightFrame.count) return &drawnLights;
    float scores[GPU_POINT_LIGHTS];
    along = GtePresentationAlong(along);
    for (int i = 0; i < lightFrame.count; i++) {
        const GpuLightSource *source = &lightFrame.sources[i];
        GpuPointLight light = source->light;
        if (along < 1 && previousLights.frame + 1 == lightFrame.frame) {
            for (int j = 0; j < previousLights.count; j++) {
                const GpuLightSource *past = &previousLights.sources[j];
                if (past->generation != source->generation || past->id != source->id || source->life > past->life) continue;
                float distance = 0;
                for (int k = 0; k < 3; k++) {
                    float d = light.position[k] - past->light.position[k];
                    distance += d * d;
                }
                if (distance < 12000.0f * 12000.0f) {
                    for (int k = 0; k < 3; k++)
                        light.position[k] = past->light.position[k] + (light.position[k] - past->light.position[k]) * along;
                    light.intensity = past->light.intensity + (light.intensity - past->light.intensity) * along;
                }
                break;
            }
        }
        if (light.radius <= 0 || light.intensity <= 0 || light.position[2] < -light.radius) continue;
        float distance = 0;
        for (int k = 0; k < 3; k++) distance += light.position[k] * light.position[k];
        float score = light.intensity * light.radius * light.radius / (distance + light.radius * light.radius);
        int slot = drawnLights.count;
        if (slot == GPU_POINT_LIGHTS && score <= scores[slot - 1]) continue;
        if (slot < GPU_POINT_LIGHTS) drawnLights.count++;
        else slot--;
        while (slot > 0 && score > scores[slot - 1]) {
            drawnLights.lights[slot] = drawnLights.lights[slot - 1];
            scores[slot] = scores[slot - 1];
            slot--;
        }
        drawnLights.lights[slot] = light;
        scores[slot] = score;
    }
    return &drawnLights;
}

const GpuLightSource *ModernSourceLight(unsigned id)
{
    if (lightFrame.frame != GteFrameRecorded() || !ModernEnabled()) return NULL;
    for (int i = 0; i < lightFrame.count; i++) if (lightFrame.sources[i].id == id) return &lightFrame.sources[i];
    return NULL;
}
void ModernLightsReset(void)
{
    memset(&lightFrame, 0, sizeof(lightFrame));
    memset(&previousLights, 0, sizeof(previousLights));
    memset(&drawnLights, 0, sizeof(drawnLights));
}
