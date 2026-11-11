/* Presentation-only point lights, in the same view space as precise geometry. */
#ifndef RENDER_POINT_LIGHTS_H
#define RENDER_POINT_LIGHTS_H
#include <stdint.h>

/* 30 projectiles, 32 explosions, and engine, shield and wall spark lights for 15 ships. */
enum { GPU_POINT_LIGHTS = 16, GPU_LIGHT_SOURCES = 112 };
enum { GPU_SHIELD_LIGHT_BASE = 4096 }; /* one source ID per ship, in ship slot order */
typedef struct GpuPointLight {
    float position[3], radius;
    float colour[3], intensity;
} GpuPointLight;
typedef struct GpuLightSource {
    GpuPointLight light;
    unsigned id;
    uint32_t generation;
    int life; /* falling lifetime; a rise identifies a reused effect slot */
} GpuLightSource;
typedef struct GpuLightSet {
    int count;
    GpuPointLight lights[GPU_POINT_LIGHTS];
} GpuLightSet;

/* Copy source metadata while building a race; never retain pointers into game state. */
const GpuLightSet *ModernSelectLights(float along);
const GpuLightSource *ModernSourceLight(unsigned id);
void ModernLightsReset(void);
void PsxCaptureLights(const GpuLightSource *sources, int count);
#endif
