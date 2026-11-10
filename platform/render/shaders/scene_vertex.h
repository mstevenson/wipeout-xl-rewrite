/* Common vertex transport. Rasterization operations come only from its owner. */
#ifdef RENDER_LIGHTING
    "in vec4 viewPositionIn;\n"
    "out vec3 vViewPosition;\n"
    "flat out float receivesLight;\n"
#endif
    "in vec3 position;\n"
    "in vec4 colour;\n"
    "in vec2 texel;\n"
    "in ivec4 textureIn;\n"
    "in uvec4 window;\n"              /* unsigned bytes: WebGL wants an attribute's signedness kept */
    "in uvec4 modeIn;\n"
    "in uvec4 hdIn;\n"
    "in ivec4 hdTexelsIn;\n"
    "in ivec4 filterBoundsIn;\n"
    "flat out ivec4 filterBounds;\n"
    "flat out int filtered;\n"
    "flat out int addressFlags;\n"
    "flat out int ps1Light;\n"
#ifdef RENDER_AUX_FLAGS
    "flat out int auxFlags;\n"
    "in ivec4 emissionUvIn;\n"
    "flat out ivec4 emissionUv;\n"
    "in vec3 shutterDeltaIn;\n"
    "out vec3 shutterDelta;\n"
#endif
    "flat out uvec4 hd;\n"
    "flat out ivec4 hdTexels;\n"
    "flat out ivec4 texturePage;\n"
    "flat out ivec4 clut;\n"
    "flat out ivec4 vWindow;\n"
    "flat out ivec4 mode;\n"
    "flat out int semiMode;\n"        /* the primitive's blending: -1 opaque, else the console's mode 0-3 */
    "uniform int pass;\n"
    "uniform float depthPull;\n"       /* the depth buffer (Flush): depth tested this much nearer */
    "uniform int depthPass;\n"         /* ... 1: depth written, at the true depth */
    "uniform vec2 target;\n"           /* framebuffer size in video-memory pixels */
    "uniform vec2 origin;\n"           /* the video-memory position of its first pixel */
    "out vec4 vColour;\n"
    "out float vColourWeight;\n"
    "out vec3 vPixel;\n"
    "out vec3 vTexel;\n"
    "void main() {\n"
    /* position.z: the depth (perspective), negative when left out of the depth buffer, 0 in 2D. The depth buffer
       holds 1 - 1/depth; what is left out and 2D are in front of it (always drawn) and never written. */
    "    float w = position.z != 0.0 ? abs(position.z) : 1.0;\n"
#ifdef RENDER_POSITION
    RENDER_POSITION
#else
    "    vec2 pixel = position.xy;\n"
#endif
    "    vec2 ndc = (pixel - origin) / target * 2.0 - 1.0;\n"
    /* modeIn.w: bit 0 a polygon seen from behind or exactly placed, tested without the slack (hidden by anything
       nearer); from bit 4
       the blending mode + 1 */
    "    float pull = depthPass == 1 || (modeIn.w & 1u) == 1u ? 1.0 : depthPull;\n"
    "    semiMode = int(modeIn.w >> 4u) - 1;\n"
    "    float z = position.z > 0.0 ? w - 2.0 * pull : (depthPass == 1 ? w : -w);\n"
    "    gl_Position = vec4(ndc * w, z, w);\n"
    /* Multiply/divide by w implements screen-linear colour on ES 3 too (no noperspective qualifier). */
    "    vColourWeight = (modeIn.y & 16u) != 0u ? w : 1.0;\n"
    "    vColour = vec4(colour.rgb * vColourWeight, colour.a);\n"
    "    vPixel = vec3(pixel * w, w);\n"
#ifdef RENDER_LIGHTING
    "    vViewPosition = viewPositionIn.xyz;\n"
    "    receivesLight = viewPositionIn.w;\n"
#endif
    "    texturePage = ivec4(textureIn.xy, 0, 0);\n"
    "    clut = ivec4(textureIn.zw, 0, 0);\n"
    "    vWindow = ivec4(window);\n"
    "    mode = ivec4(int(modeIn.x), int(modeIn.y), pass, int(modeIn.z & 3u));\n"
#ifdef RENDER_AFFINE_TEXTURES
    "    vTexel = (modeIn.y & 32u) != 0u ? vec3(texel * w, w) : vec3(texel, 1.0);\n"
#else
    "    vTexel = vec3(texel, 1.0);\n"
#endif
    "    hd = hdIn;\n"
    "    hdTexels = hdTexelsIn;\n"
    "    filterBounds = filterBoundsIn;\n"
    "    filtered = int((modeIn.w >> 1u) & 3u);\n"
    "    addressFlags = int((modeIn.z >> 2u) & 7u);\n"
    "    ps1Light = int((modeIn.z >> 5u) & 1u);\n"
#ifdef RENDER_AUX_FLAGS
    "    auxFlags = int((modeIn.z >> 6u) | ((modeIn.w & 8u) >> 1u));\n"
    "    emissionUv = emissionUvIn;\n"
    "    shutterDelta = shutterDeltaIn;\n"
#endif
    "}\n"
