/* Renderer-neutral decisions used by game packet builders and the GPU adapter.
   Modern's world billboards and retained 1995 scenery colours follow ModernEnabled(). */
#ifndef RENDER_GEOMETRY_H
#define RENDER_GEOMETRY_H
#ifdef __cplusplus
extern "C" {
#endif

enum { RENDER_LIGHT_PS1, RENDER_LIGHT_RGB8 };
typedef struct RenderGeometry {
    int smoothTrackLighting; /* attach interpolated colour to track packets */
} RenderGeometry;
RenderGeometry PortRenderGeometry(void);
#ifdef __cplusplus
}
#endif
#endif
