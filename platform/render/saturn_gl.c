#include "gl_pipeline.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "saturn_native.h"
#include "../psx/gpu.h"

#define SATURN_FRAGMENT_PRECISION \
    "#ifdef GL_ES\n" \
    "precision highp float;\n" \
    "precision highp int;\n" \
    "precision highp usampler2D;\n" \
    "#endif\n"

#define RENDER_AFFINE_TEXTURES
#define RENDER_RGB5_VERTEX_COLOUR
#define RENDER_FRAGMENT_UNIFORMS "uniform vec2 nativeGrid;\n"
#define RENDER_POSITION "    vec2 pixel = (modeIn.y & 128u) != 0u ? floor(position.xy * nativeGrid) / nativeGrid : position.xy;\n    if (pointSize > 0.0) pixel += vec2(0.5) / nativeGrid;\n    gl_PointSize = max(pointSize, 1.0);\n"
/* VDP2 places RGB555 in the high five bits of each 8-bit channel. */
#define RENDER_OUTPUT_COLOUR \
    "vec3 OutputColour(vec3 col) {\n" \
    "    if ((mode.y & 4) == 0) return col;\n" \
    "    return floor(clamp(col * 255.0 + 0.0001, 0.0, 255.0) / 8.0) * (8.0 / 255.0);\n" \
    "}\n"
/* Mesh selects coverage, not the colour operation. Compatibility flares and
   trails still add light: replacing their overlapping gradients makes dark
   ray edges erase the bright centre. Native VDP1 sprites keep their own mode. */
#define RENDER_COVERAGE \
    "    if (filtered != 0 && blended && semiMode != 2) {\n" \
    "        if (semiMode != 0 && all(equal(col, vec3(0.0)))) discard;\n" \
    "        if (filtered == 3) {\n" \
    "            ivec2 p = ivec2(floor(vPixel.xy / vPixel.z * nativeGrid));\n" \
    "            if (((p.x + p.y) & 1) != 0) discard;\n" \
    "        } else {\n" \
    "            col *= 0.5;\n" \
    "            cov *= 0.5;\n" \
    "        }\n" \
    "        if (semiMode == 0) blended = false;\n" \
    "    }\n"

static const char *saturnVertexSource = "uniform float pointSize; uniform vec2 nativeGrid;\n"
#include "shaders/scene_vertex.h"
;
static const char *saturnFragmentSource =
#include "shaders/material_fragment.h"
;
static RenderProgram saturnProgram, backgroundProgram;
static GLuint backgroundTexture, backgroundArray;
static uint64_t backgroundRevision;
static RenderProgram pixelsProgram;
static GLuint pixelsTexture, pixelsArray;

static RenderProgram *SaturnScene(void)
{
    if (!RenderGlProgramInit(&saturnProgram, saturnVertexSource, saturnFragmentSource)) abort();
    return &saturnProgram;
}

static void SaturnPresent(const void *profile, const RenderPresent *p)
{
    const SaturnRenderSettings *settings = profile;
    RenderGlPresentDirect(p, settings->resample ? GL_LINEAR : GL_NEAREST);
}

static void QuadDestroy(void);

#define SATURN_MARGIN (MAX_WIDE_MARGIN * 11 / 10)
#define SATURN_WIDTH (352 + 2*SATURN_MARGIN)
static uint8_t saturnPixels[VRAM_HEIGHT][SATURN_WIDTH][4];
static int saturnLeft=SATURN_WIDTH, saturnTop=VRAM_HEIGHT, saturnRight, saturnBottom;
static int saturnSkyDrawn;
static uint64_t saturnSkyPass;


static void SaturnDestroy(void)
{
    QuadDestroy();
    saturnSkyDrawn = 0;
    memset(saturnPixels, 0, sizeof(saturnPixels));
    saturnLeft = SATURN_WIDTH; saturnTop = VRAM_HEIGHT;
    saturnRight = saturnBottom = 0;
    RenderGlProgramDestroy(&saturnProgram);
    RenderGlProgramDestroy(&backgroundProgram);
    RenderGlProgramDestroy(&pixelsProgram);
    if (pixelsTexture) glDeleteTextures(1, &pixelsTexture);
    if (pixelsArray) glDeleteVertexArrays(1, &pixelsArray);
    pixelsTexture=pixelsArray=0;
    if (backgroundTexture) glDeleteTextures(1, &backgroundTexture);
    if (backgroundArray) glDeleteVertexArrays(1, &backgroundArray);
    backgroundTexture = backgroundArray = 0; backgroundRevision = 0;
}

/* A VDP2 plane is a single screen rectangle, sampled at the framebuffer's
   resolution. The literal RBG0 reference keeps the native dot grid. */
static void RenderSaturnBackground(const uint8_t *pixels, const uint16_t *palette, int width, int height,
    uint64_t revision, const GpuDrawState *state, int originalScroll, int left, int top, int right, int bottom,
    float cx, float cy, float targetW, float targetH, float originX, float scaleX, float scaleY, int scale)
{
    static const char *vertex =
        "uniform vec2 target, origin; uniform ivec4 bounds;\n"
        "void main() {\n"
        " vec2 uv=vec2((gl_VertexID==1 || gl_VertexID==2 || gl_VertexID==4)?1:0,"
        " (gl_VertexID==2 || gl_VertexID==4 || gl_VertexID==5)?1:0);\n"
        " vec2 p=mix(vec2(bounds.xy),vec2(bounds.zw),uv);\n"
        " gl_Position=vec4((p-origin)/target*2.0-1.0,0,1); }\n";
    static const char *fragment = SATURN_FRAGMENT_PRECISION
        "uniform sampler2D picture; uniform vec2 origin, centre, rotation, scroll;\n"
        "uniform vec2 pitchRotation; uniform float heading, focal; uniform int originalScroll;\n"
        "uniform vec2 scale, nativeGrid; uniform float nativeScale; out vec4 result;\n"
        "void main() {\n"
        " vec2 p=floor(gl_FragCoord.xy)/scale+origin-centre;\n"
        " vec2 q; if(originalScroll!=0) {\n"
        /* The original RBG0 covers 352x240 dots; the host's 4:3 scene is
           320x240. Widescreen extends that scene without stretching its centre. */
        /* Keep integral source dots exact, including half-dot viewport centres.
           Floating 1.1 can put X=0 just below zero and wrap to texel 2047. */
        " p=(floor(gl_FragCoord.xy/nativeScale)/nativeGrid+origin-centre)*vec2(1.1,nativeGrid.y);\n"
        " q=vec2(rotation.x*p.x+rotation.y*p.y,-rotation.y*p.x+rotation.x*p.y)+scroll;\n"
        " } else {\n"
        /* Invert the view rotation on the same perspective ray as the world.
           Preserve panorama angular extent and the original neutral centre;
           changing yaw alone would break the seam after a complete turn. */
        " vec3 ray=vec3(rotation.x*p.x-rotation.y*p.y,rotation.y*p.x+rotation.x*p.y,focal);\n"
        " ray.yz=vec2(pitchRotation.x*ray.y-pitchRotation.y*ray.z,pitchRotation.y*ray.y+pitchRotation.x*ray.z);\n"
        " q=vec2(176.0+(heading+atan(ray.x,ray.z))*(2048.0/6.28318530718),\n"
        "        240.0+atan(ray.y,length(ray.xz))*(4096.0/(3.0*6.28318530718)));\n"
        " } q=floor(q+vec2(0.00001));\n"
        " ivec2 size=textureSize(picture,0); ivec2 at=ivec2(mod(q,vec2(size)));\n"
        " result=texelFetch(picture,at,0); }\n";
    if (!backgroundProgram.program) {
        if (!RenderGlProgramInit(&backgroundProgram, vertex, fragment)) abort();
        glGenTextures(1, &backgroundTexture);
        glGenVertexArrays(1, &backgroundArray);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, backgroundTexture);
    if (backgroundRevision != revision) {
        uint8_t *rgba=malloc((size_t)width*height*4);
        if (!rgba) abort();
        for (int i=0;i<width*height;i++) {
            uint16_t rgb=palette[pixels[i]];
            for(int c=0;c<3;c++) rgba[i*4+c]=(uint8_t)(((rgb>>(5*c))&31)*8);
            rgba[i*4+3]=255;
        }
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
        free(rgba);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        backgroundRevision=revision;
    }
    GLuint program=backgroundProgram.program;
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program,"picture"),0);
    glUniform2f(backgroundProgram.uniformTarget,(float)targetW,(float)targetH);
    glUniform2f(backgroundProgram.uniformOrigin,(float)originX,0);
    glUniform4i(glGetUniformLocation(program,"bounds"),left,top,right,bottom);
    glUniform2f(glGetUniformLocation(program,"centre"),cx,cy);
    glUniform2f(glGetUniformLocation(program,"rotation"),roundf(cosf(state->skyRoll)*65536)/65536,
        roundf(sinf(state->skyRoll)*65536)/65536);
    glUniform2f(glGetUniformLocation(program,"scroll"),176+(((int)state->skyYaw/2)&2047),240-(int)state->skyPitch/3);
    const char *reference=getenv("WIPEOUT_SATURN_VDP2_REFERENCE");
    glUniform1i(glGetUniformLocation(program,"originalScroll"),originalScroll || (reference && atoi(reference)!=0));
    float pitch=state->skyPitch*(6.28318530718f/4096);
    glUniform2f(glGetUniformLocation(program,"pitchRotation"),cosf(pitch),sinf(pitch));
    glUniform1f(glGetUniformLocation(program,"heading"),state->skyYaw*(6.28318530718f/4096));
    glUniform1f(glGetUniformLocation(program,"focal"),state->skyFocal>0 ? state->skyFocal : 160);
    glUniform2f(glGetUniformLocation(program,"scale"),scaleX,scaleY);
    glUniform2f(glGetUniformLocation(program,"nativeGrid"),scaleX/scale,scaleY/scale);
    glUniform1f(glGetUniformLocation(program,"nativeScale"),(float)scale);
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST);
    glBindVertexArray(backgroundArray);
    glDrawArrays(GL_TRIANGLES,0,6);
}

static void RenderSaturnPixels(const uint8_t *rgba, int width, int height, int margin,
    int left, int top, int right, int bottom, float targetW, float targetH, float originX, float scaleX, float scaleY)
{
    static const char *vertex=
        "uniform vec2 target,origin; uniform ivec4 bounds;\n"
        "void main(){ vec2 uv=vec2((gl_VertexID==1 || gl_VertexID==2 || gl_VertexID==4)?1:0,"
        "(gl_VertexID==2 || gl_VertexID==4 || gl_VertexID==5)?1:0);\n"
        "gl_Position=vec4((mix(vec2(bounds.xy),vec2(bounds.zw),uv)-origin)/target*2.0-1.0,0,1); }\n";
    static const char *fragment=SATURN_FRAGMENT_PRECISION
        "uniform sampler2D picture; uniform vec2 origin, scale; uniform int margin;\n"
        "out vec4 result; void main(){ivec2 at=ivec2(floor(gl_FragCoord.xy/scale)+origin)+ivec2(margin,0);"
        "result=texelFetch(picture,at,0); }\n";
    if(!pixelsProgram.program) {
        if(!RenderGlProgramInit(&pixelsProgram,vertex,fragment)) abort();
        glGenTextures(1,&pixelsTexture); glGenVertexArrays(1,&pixelsArray);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,pixelsTexture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    }
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,pixelsTexture);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,width);
    glTexSubImage2D(GL_TEXTURE_2D,0,left+margin,top,right-left,bottom-top,GL_RGBA,GL_UNSIGNED_BYTE,
        rgba+((size_t)top*width+left+margin)*4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    GLuint program=pixelsProgram.program;glUseProgram(program);
    glUniform1i(glGetUniformLocation(program,"picture"),0);
    glUniform1i(glGetUniformLocation(program,"margin"),margin);
    glUniform2f(glGetUniformLocation(program,"scale"),scaleX,scaleY);
    glUniform2f(pixelsProgram.uniformTarget,targetW,targetH);
    glUniform2f(pixelsProgram.uniformOrigin,(float)originX,0);
    glUniform4i(glGetUniformLocation(program,"bounds"),left,top,right,bottom);
    glDisable(GL_DEPTH_TEST);glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_ONE,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);
    glBindVertexArray(pixelsArray);glDrawArrays(GL_TRIANGLES,0,6);
}

/* Native commands are batched in draw order. One GPU instance draws one VDP1
   joining line; the fragment shader owns its integer coverage, texture lookup,
   additive shading, mesh and blend. No CPU pixel loop is used here. */
#define QUAD_COMMANDS 56 /* 18 vec4 words each: fits the ES 3 minimum 16 KiB UBO. */
#define QUAD_WORDS 18
#define QUAD_COORD_LIMIT 32768
/* A single command must fit even at the maximum distance between corners. */
#define QUAD_ROWS (2 * QUAD_COORD_LIMIT * RENDER_MAX_SCALE + 1)
#define QUAD_ATLAS 2048
#define QUAD_IMAGES 8192
static RenderProgram quadProgram;
static GLuint quadCommands, quadAtlas, quadArray, quadBuffer;
static float commandData[QUAD_COMMANDS][QUAD_WORDS][4];
static int rowData[QUAD_ROWS][2], commandCount, rowCount, quadDepth, quadDepthWrite;
static uint64_t quadRevision;
static int atlasX, atlasY, atlasRow;
static struct { const uint8_t *pixels; int x,y,w,h; } quadImages[QUAD_IMAGES];
static GLint quadSourceUniform, quadClipUniform, quadScaleUniform;

static void QuadInit(void)
{
    static const char *vertex=
#include "shaders/saturn_quad_vertex.h"
    ;
    static const char *fragment=SATURN_FRAGMENT_PRECISION
#include "shaders/saturn_quad_fragment.h"
    ;
    if(quadProgram.program) return;
    if(!RenderGlProgramInit(&quadProgram,vertex,fragment)) abort();
    quadSourceUniform=glGetUniformLocation(quadProgram.program,"sourcePixels");
    quadClipUniform=glGetUniformLocation(quadProgram.program,"clipBounds");
    quadScaleUniform=glGetUniformLocation(quadProgram.program,"renderScale");
    glGenBuffers(1,&quadCommands);
    glUniformBlockBinding(quadProgram.program,glGetUniformBlockIndex(quadProgram.program,"QuadCommands"),0);
    glGenTextures(1,&quadAtlas);glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D,quadAtlas);
    glTexImage2D(GL_TEXTURE_2D,0,GL_R8UI,QUAD_ATLAS,QUAD_ATLAS,0,GL_RED_INTEGER,GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glGenVertexArrays(1,&quadArray);glBindVertexArray(quadArray);
    glGenBuffers(1,&quadBuffer);glBindBuffer(GL_ARRAY_BUFFER,quadBuffer);
    glEnableVertexAttribArray(0);glVertexAttribIPointer(0,2,GL_INT,sizeof(rowData[0]),NULL);
    glVertexAttribDivisor(0,1);
    glActiveTexture(GL_TEXTURE0);
}

static void QuadDestroy(void)
{
    RenderGlProgramDestroy(&quadProgram);
    if(quadCommands) glDeleteBuffers(1,&quadCommands);
    if(quadAtlas) glDeleteTextures(1,&quadAtlas);
    if(quadArray) glDeleteVertexArrays(1,&quadArray);
    if(quadBuffer) glDeleteBuffers(1,&quadBuffer);
    quadCommands=quadAtlas=quadArray=quadBuffer=0;
    commandCount=rowCount=atlasX=atlasY=atlasRow=0;quadRevision=0;
    memset(quadImages,0,sizeof(quadImages));
}

static int RenderSaturnQuadsPending(void) { return rowCount!=0; }

/* 0 requests a flush before retrying, including atlas reuse and depth-state changes. */
static int RenderSaturnQuad(const SaturnNativePoly *p,const GpuVertex *v,const GpuDrawState *state,
                     uint64_t revision,int depth,float scaleX,float scaleY,int left,int top,int right,int bottom)
{
    GpuVertex q[4];
    const int limit=(int)(QUAD_COORD_LIMIT*fmaxf(scaleX,scaleY));
    int forward = !p->clipped && !p->trackTile;
    int edge=0,write=depth && !v[0].noDepth && !p->alphaMesh && (p->mode&3)!=1 && (p->mode&3)!=3;
    for(int k=0;k<4;k++) {
        if(p->corners[k]<0 || p->corners[k]>3) return 1;
        q[k]=v[p->corners[k]];
        if(!isfinite(q[k].x) || !isfinite(q[k].y)) return 1;
        /* Snap joining lines on the framebuffer grid. UVs and Gouraud offsets
           still interpolate across the original four corners. */
        q[k].x*=scaleX;
        q[k].y*=scaleY;
        if(forward) {
            q[k].x=floorf(q[k].x);q[k].y=floorf(q[k].y);
            if(fabsf(q[k].x)>limit || fabsf(q[k].y)>limit) return 1;
        }
        write &= q[k].a==255 && q[k].w>0;
    }
    if(forward) edge=(int)fmaxf(fmaxf(fabsf(q[3].x-q[0].x),fabsf(q[3].y-q[0].y)),
                                  fmaxf(fabsf(q[2].x-q[1].x),fabsf(q[2].y-q[1].y)));
    if(commandCount==QUAD_COMMANDS || rowCount+edge+1>QUAD_ROWS ||
       (commandCount && (revision!=quadRevision || depth!=quadDepth || write!=quadDepthWrite))) return 0;
    QuadInit();
    if(revision!=quadRevision) {
        memset(quadImages,0,sizeof(quadImages));atlasX=atlasY=atlasRow=0;quadRevision=revision;
    }
    int imageX=0,imageY=0,imageW=0,imageH=0;
    if(p->pixels && p->width>0 && p->height>0) {
        if(p->width>QUAD_ATLAS || p->height>QUAD_ATLAS) {
            SDL_Log("Saturn texture exceeds native atlas: %dx%d",p->width,p->height);abort();
        }
        size_t slot=((uintptr_t)p->pixels>>4)&(QUAD_IMAGES-1);
        size_t first=slot;
        while(quadImages[slot].pixels && (quadImages[slot].pixels!=p->pixels ||
              quadImages[slot].w!=p->width || quadImages[slot].h!=p->height)) {
            slot=(slot+1)&(QUAD_IMAGES-1);if(slot==first) break;
        }
        if(quadImages[slot].pixels!=p->pixels || quadImages[slot].w!=p->width || quadImages[slot].h!=p->height) {
            int x=atlasX,y=atlasY,h=atlasRow;
            if(x+p->width>QUAD_ATLAS) { x=0;y+=h;h=0; }
            if(y+p->height>QUAD_ATLAS || quadImages[slot].pixels) {
                if(commandCount) return 0;
                memset(quadImages,0,sizeof(quadImages));x=y=h=0;
                slot=((uintptr_t)p->pixels>>4)&(QUAD_IMAGES-1);
            }
            quadImages[slot].pixels=p->pixels;
            quadImages[slot].x=x;quadImages[slot].y=y;quadImages[slot].w=p->width;quadImages[slot].h=p->height;
            atlasX=x+p->width;atlasY=y;atlasRow=SDL_max(h,p->height);
            glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,quadAtlas);
            GLint alignment;glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);
            glPixelStorei(GL_UNPACK_ALIGNMENT,1);
            glTexSubImage2D(GL_TEXTURE_2D,0,x,y,p->width,p->height,GL_RED_INTEGER,GL_UNSIGNED_BYTE,p->pixels);
            glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);glActiveTexture(GL_TEXTURE0);
        }
        imageX=quadImages[slot].x;imageY=quadImages[slot].y;imageW=p->width;imageH=p->height;
    }
    float (*cmd)[4]=commandData[commandCount];
    memset(cmd,0,sizeof(commandData[0]));
    cmd[0][0]=cmd[0][2]=q[0].x;cmd[0][1]=cmd[0][3]=q[0].y;
    for(int k=0;k<4;k++) {
        cmd[0][0]=fminf(cmd[0][0],q[k].x);cmd[0][2]=fmaxf(cmd[0][2],q[k].x);
        cmd[0][1]=fminf(cmd[0][1],q[k].y);cmd[0][3]=fmaxf(cmd[0][3],q[k].y);
        cmd[1+k/2][(k%2)*2]=q[k].x;cmd[1+k/2][(k%2)*2+1]=q[k].y;
        /* The shared depth buffer is linear in screen-space reciprocal depth. */
        cmd[3][k]=q[k].w > 0 ? 1.0f/q[k].w : 0;cmd[4][k]=q[k].a;
        cmd[5+k/2][(k%2)*2]=q[k].u;cmd[5+k/2][(k%2)*2+1]=q[k].v;
        for(int c=0;c<3;c++) cmd[7+c][k]=p->vertexAttributes ? q[k].saturnShade[c] : (p->gouraud[k]>>(5*c))&31;
    }
    cmd[0][0]=floorf(cmd[0][0]);cmd[0][1]=floorf(cmd[0][1]);
    cmd[0][2]=ceilf(cmd[0][2])+1;cmd[0][3]=ceilf(cmd[0][3])+1;
    for(int i=0;i<16;i++) cmd[10+i/4][i%4]=p->palette[i];
    /* Bit 16 is a host coverage flag, above the 16-bit CMDPMOD. */
    cmd[14][0]=p->mode | (p->alphaMesh ? 0x10000 : 0);
    cmd[14][1]=p->colour;cmd[14][2]=p->textured;
    /* 0 forward quad, 1 clipped model, 2 road quad, 3 clipped road triangle. */
    cmd[14][3]=p->trackTile ? (p->clipped ? 3 : 2) : p->clipped;
    cmd[15][0]=imageX;cmd[15][1]=imageY;cmd[15][2]=imageW;cmd[15][3]=imageH;
    cmd[16][0]=state->pageX;cmd[16][1]=state->pageY;
    cmd[16][2]=(state->windowMaskX&255)|((state->windowMaskY&255)<<8);
    cmd[16][3]=(state->windowOffsetX&255)|((state->windowOffsetY&255)<<8);
    cmd[17][0]=edge;cmd[17][2]=p->vertexAttributes;cmd[17][3]=v[0].noDepth;
    left=(int)lroundf(left*scaleX);top=(int)lroundf(top*scaleY);
    right=(int)lroundf(right*scaleX);bottom=(int)lroundf(bottom*scaleY);
    if (!forward && (cmd[0][2] <= left || cmd[0][0] >= right ||
                     cmd[0][3] <= top || cmd[0][1] >= bottom)) return 1;
    int firstRow=rowCount;
    for(int row=0;row<=edge;row++) {
        /* Coarse geometry culling only. Very large projected quads can have
           thousands of joining lines outside the viewport; do not send those
           instances through the vertex shader. Keep a pixel of slack for edge
           rounding. Exact pixel coverage and all shading remain on the GPU. */
        float t=edge ? (float)row/edge : 0;
        float ax=q[0].x+(q[3].x-q[0].x)*t,bx=q[1].x+(q[2].x-q[1].x)*t;
        float ay=q[0].y+(q[3].y-q[0].y)*t,by=q[1].y+(q[2].y-q[1].y)*t;
        if(forward && (fmaxf(ax,bx)<left-1 || fminf(ax,bx)>=right+1 ||
                          fmaxf(ay,by)<top-1 || fminf(ay,by)>=bottom+1)) continue;
        rowData[rowCount][0]=commandCount;rowData[rowCount++][1]=p->clipped ? -1 : row;
    }
    if(firstRow==rowCount) return 1;
    commandCount++;quadDepth=depth;quadDepthWrite=write;
    return 1;
}

static void RenderSaturnFlushQuads(GLuint vram,int targetW,int targetH,float originX,float scaleX,float scaleY,int scale,
                            int left,int top,int right,int bottom)
{
    if(!rowCount) return;
    glUseProgram(quadProgram.program);
    /* Stream command buffers without updating a texture between draws. Texture
       uploads force render-pass submissions on tile GPUs, even for tiny batches. */
    glBindBuffer(GL_UNIFORM_BUFFER,quadCommands);
    glBufferData(GL_UNIFORM_BUFFER,sizeof(commandData),commandData,GL_STREAM_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER,0,quadCommands);
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,quadAtlas);glUniform1i(quadSourceUniform,2);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,vram);glUniform1i(quadProgram.uniformVram,0);
    glUniform2f(quadProgram.uniformTarget,(float)targetW,(float)targetH);
    glUniform2f(quadProgram.uniformOrigin,roundf(originX*scaleX),0);
    glUniform4i(quadClipUniform,(int)lroundf(left*scaleX),(int)lroundf(top*scaleY),(int)lroundf(right*scaleX),(int)lroundf(bottom*scaleY));
    glUniform1f(quadScaleUniform,(float)scale);
    glDisable(GL_DITHER);glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_ONE,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);
    if(quadDepth) glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);glDepthMask(quadDepthWrite ? GL_TRUE : GL_FALSE);
    glBindVertexArray(quadArray);glBindBuffer(GL_ARRAY_BUFFER,quadBuffer);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(rowCount*sizeof(rowData[0])),rowData,GL_STREAM_DRAW);
    glDrawArraysInstanced(GL_TRIANGLES,0,6,rowCount);
    glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);
    rowCount=commandCount=0;
}

typedef struct SaturnDraw {
    GpuDrawState source;
    int noDepth;
    const RenderNativeTarget *target;
} SaturnDraw;

static int SaturnSampleIndex(void *user, float u, float v)
{
    SaturnDraw *d = user;
    /* Stabilize exact texel boundaries across CPU division and shader arithmetic. */
    int x = (int)floorf(u+0.00001f) & 255, y = (int)floorf(v+0.00001f) & 255;
    x = (x & ~(d->source.windowMaskX*8)) | ((d->source.windowOffsetX & d->source.windowMaskX)*8);
    y = (y & ~(d->source.windowMaskY*8)) | ((d->source.windowOffsetY & d->source.windowMaskY)*8);
    int py = (d->source.pageY + y) & 1023;
    const uint16_t *row = py >= 512 ? d->target->vramHigh[py-512] : d->target->vramLow[py];
    return (row[(d->source.pageX + x/4) & 1023] >> ((x&3)*4)) & 15;
}

static void SaturnEmit(void *user, int x, int y, uint16_t rgb, int blend, float depth, uint8_t alpha)
{
    SaturnDraw *d = user;
    GpuDrawState state = {0};
    state.semi = blend == -2 ? 0 : blend; state.fade = alpha < 255;
    if (blend == -2) rgb = 0; /* RGB destination shadow: half the destination. */
    GpuVertex pixel = {0};
    pixel.x = x / d->target->rasterX; pixel.y = y / d->target->rasterY; pixel.w = depth;
    pixel.noDepth = d->noDepth; pixel.world = 1;
    pixel.r = (uint8_t)((rgb & 31)*8); pixel.g = (uint8_t)(((rgb>>5)&31)*8);
    pixel.b = (uint8_t)(((rgb>>10)&31)*8); pixel.a = alpha;
    unsigned shade = SHADE_AFFINE_COLOUR | SHADE_PS1_COLOUR;
    d->target->point(&state, &pixel, shade);
}

/* Aggregate ordered native writes as premultiplied colour/coverage. The final
   overlay blends over prior compatibility draws; flush before the next such draw.
   Optional depth uses the point path so it still shares the host depth buffer. */
static void SaturnComposite(void *user, int x, int y, uint16_t rgb, int blend, float depth, uint8_t alpha)
{
    (void)user; (void)depth;
    int px=x+SATURN_MARGIN;
    if (px<0 || px>=SATURN_WIDTH || y<0 || y>=VRAM_HEIGHT) return;
    uint8_t *out=saturnPixels[y][px];
    unsigned a=blend >= 0 || blend == -2 ? (alpha+1)/2 : alpha;
    if (a==255) {
        /* Most road/scenery writes are opaque: avoid reading and blending the
           previous pixel (including repeated writes along a distorted edge). */
        out[0]=(uint8_t)((rgb&31)*8);
        out[1]=(uint8_t)(((rgb>>5)&31)*8);
        out[2]=(uint8_t)(((rgb>>10)&31)*8);
        out[3]=255;
    } else {
        for(int c=0;c<3;c++) {
            unsigned src=blend == -2 ? 0 : ((rgb>>(5*c))&31)*8;
            out[c]=(uint8_t)((src*a+out[c]*(255-a)+127)/255);
        }
        out[3]=(uint8_t)(a+(out[3]*(255-a)+127)/255);
    }
    if(px<saturnLeft) saturnLeft=px;
    if(px>=saturnRight) saturnRight=px+1;
    if(y<saturnTop) saturnTop=y;
    if(y>=saturnBottom) saturnBottom=y+1;
}

static void SaturnFlushNative(const RenderNativeTarget *t)
{
    if (RenderSaturnQuadsPending()) {
        t->bind(1);
        RenderSaturnFlushQuads(t->vram, t->pixelWidth, t->pixelHeight, t->origin, t->scaleX, t->scaleY,
                               t->scale, t->left, t->top, t->right, t->bottom);
        glUseProgram(saturnProgram.program);
        t->changed(t->left, t->top, t->right - t->left, t->bottom - t->top);
    }
    if (saturnRight <= saturnLeft || saturnBottom <= saturnTop) return;
    t->bind(0);
    int left = saturnLeft - SATURN_MARGIN, right = saturnRight - SATURN_MARGIN;
    RenderSaturnPixels(&saturnPixels[0][0][0], SATURN_WIDTH, VRAM_HEIGHT, SATURN_MARGIN,
        left, saturnTop, right, saturnBottom, t->targetWidth * t->rasterX, t->targetHeight * t->rasterY,
        t->origin * t->rasterX, (float)t->scale, (float)t->scale);
    glUseProgram(saturnProgram.program);
    t->changed(left / t->rasterX, saturnTop / t->rasterY,
               (right - left) / t->rasterX, (saturnBottom - saturnTop) / t->rasterY);
    for (int y = saturnTop; y < saturnBottom; y++)
        memset(saturnPixels[y][saturnLeft], 0, (size_t)(saturnRight - saturnLeft) * 4);
    saturnLeft = SATURN_WIDTH; saturnTop = VRAM_HEIGHT;
    saturnRight = saturnBottom = 0;
}

static void SaturnBackground(const SaturnRenderSettings *settings, const RenderNativeTarget *t,
                             const GpuDrawState *state)
{
    if (saturnSkyDrawn && saturnSkyPass == t->pass) return;
    const uint16_t *palette;
    int width, height;
    const uint8_t *picture = SaturnNativeSkyData(&palette, &width, &height);
    if (!picture) return;
    saturnSkyDrawn = 1;
    saturnSkyPass = t->pass;
    t->begin(1);
    t->bind(0);
    RenderSaturnBackground(picture, palette, width, height, SaturnNativeSkyRevision(), state,
        !settings->cameraAlignedSky, t->left, t->top, t->right, t->bottom,
        t->x + t->width * .5f, (t->top + t->bottom) * .5f, t->targetWidth, t->targetHeight,
        t->origin, t->scaleX, t->scaleY, t->scale);
    glUseProgram(saturnProgram.program);
    t->changed(t->left, t->top, t->right - t->left, t->bottom - t->top);
    t->markDrawn(t->x, t->top, t->x + t->width, t->bottom);
}

static int SaturnSubmitNative(const void *profile, const RenderNativeTarget *t, const GpuDrawState *state,
                              const GpuVertex *vertices, int count)
{
    const SaturnRenderSettings *settings = profile;
    if (state->saturnSky) {
        SaturnBackground(settings, t, state);
        return 1;
    }
    if (!state->saturn || !settings->affineTextures) return 0;

    t->begin(0);
    SaturnNativePoly poly = *state->saturn;
    poly.alphaMesh = (poly.mode & 0x100) && !settings->mesh;
    if (poly.dynamicLight)
        SaturnShipLight(SaturnTrackLight(vertices[0].r, vertices[0].g, vertices[0].b), poly.gouraud);
    const char *reference = SDL_getenv("WIPEOUT_SATURN_REFERENCE");
    if (reference && SDL_atoi(reference) != 0) {
        SaturnDraw draw = {.source = *state, .noDepth = vertices[0].noDepth, .target = t};
        GpuVertex native[4];
        for (int k = 0; k < 4; k++) {
            native[k] = vertices[k < count ? k : 0];
            native[k].x *= t->rasterX;
            native[k].y *= t->rasterY;
        }
        SaturnVdp1Draw(&poly, native, (int)lroundf(t->left * t->rasterX), (int)lroundf(t->top * t->rasterY),
            (int)lroundf(t->right * t->rasterX), (int)lroundf(t->bottom * t->rasterY),
            settings->depth ? SaturnEmit : SaturnComposite, SaturnSampleIndex, &draw);
    } else {
        uint64_t revision = SaturnNativeMaterialRevision();
        if (!RenderSaturnQuad(&poly, vertices, state, revision, settings->depth, t->scaleX, t->scaleY,
                              t->left, t->top, t->right, t->bottom)) {
            SaturnFlushNative(t);
            if (!RenderSaturnQuad(&poly, vertices, state, revision, settings->depth, t->scaleX, t->scaleY,
                                  t->left, t->top, t->right, t->bottom)) abort();
        }
    }
    float left = vertices[0].x, right = left, top = vertices[0].y, bottom = top;
    for (int k = 1; k < count; k++) {
        left = fminf(left, vertices[k].x); right = fmaxf(right, vertices[k].x);
        top = fminf(top, vertices[k].y); bottom = fmaxf(bottom, vertices[k].y);
    }
    if (isfinite(left) && isfinite(right) && isfinite(top) && isfinite(bottom)) {
        t->markDrawn(SDL_clamp(floorf(left), t->x, t->x + t->width),
                     SDL_clamp(floorf(top), t->top, t->bottom),
                     SDL_clamp(ceilf(right), t->x, t->x + t->width),
                     SDL_clamp(ceilf(bottom), t->top, t->bottom));
    }
    return 1;
}

const RenderPipeline saturnPipeline = {
    .scene = SaturnScene, .present = SaturnPresent, .destroy = SaturnDestroy,
    .submitNative = SaturnSubmitNative, .flushNative = SaturnFlushNative,
};
