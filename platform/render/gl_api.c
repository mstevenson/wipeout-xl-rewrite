#include "gl_api.h"
#ifndef PORT_GL_LINKED
#define DEFINE_GL(type, name) type p_##name;
GL_FUNCTIONS(DEFINE_GL)
ASYNC_GL_FUNCTIONS(DEFINE_GL)
int RenderGlLoad(void)
{
#define LOAD_GL(type, name)                                    \
    p_##name = (type)SDL_GL_GetProcAddress(#name);             \
    if (!p_##name) {                                           \
        SDL_Log("OpenGL function %s is missing", #name);       \
        return 0;                                              \
    }
    GL_FUNCTIONS(LOAD_GL)
    ASYNC_GL_FUNCTIONS(LOAD_GL)
    return 1;
}
#else
int RenderGlLoad(void) { return 1; }
#endif
