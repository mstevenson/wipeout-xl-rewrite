/* Renderer-neutral decisions used by game packet builders and the GPU adapter. */
#ifndef RENDER_GEOMETRY_H
#define RENDER_GEOMETRY_H
#ifdef __cplusplus
extern "C" {
#endif

enum { RENDER_LIGHT_PS1, RENDER_LIGHT_RGB8 };
enum { RENDER_TRACK_NATIVE, RENDER_TRACK_MEDIUM, RENDER_TRACK_HIGH };
typedef struct RenderGeometry {
    int alternateQuad;    /* object packet diagonal 0-3 instead of 1-2 */
    int gouraudObjects;   /* retain textured corner colours in the plain/sky passes */
    int lightAllVertices; /* replace every lit object's emitted corner colour */
    int lightFormat;      /* RENDER_LIGHT_* expected for shared object lighting */
    int worldBillboards;  /* upright 3D sprite quads instead of screen-space sprite arithmetic */
    int smoothTrackLighting; /* attach interpolated colour to track packets */
    int trackDetail;      /* RENDER_TRACK_* for road packets */
    int originalSceneryColours; /* preserve textured corner colours on imported 1995 scenery */
} RenderGeometry;
RenderGeometry PortRenderGeometry(void);
#ifdef __cplusplus
}
#endif
#endif
