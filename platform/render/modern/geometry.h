/* Renderer-neutral decisions used by game packet builders and the GPU adapter. */
#ifndef RENDER_GEOMETRY_H
#define RENDER_GEOMETRY_H
#ifdef __cplusplus
extern "C" {
#endif

enum { RENDER_LIGHT_PS1, RENDER_LIGHT_RGB8 };
typedef struct RenderGeometry {
    int lightFormat;      /* RENDER_LIGHT_* expected for shared object lighting */
    int worldBillboards;  /* upright 3D sprite quads instead of screen-space sprite arithmetic */
    int smoothTrackLighting; /* attach interpolated colour to track packets */
    int originalSceneryColours; /* preserve textured corner colours on imported 1995 scenery */
} RenderGeometry;
RenderGeometry PortRenderGeometry(void);
#ifdef __cplusplus
}
#endif
#endif
