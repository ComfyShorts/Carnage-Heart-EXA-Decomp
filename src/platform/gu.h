/* libgu: the GU drawing library the game links statically (Sony's SDK version, in the
 * game at 0x00371248-0x0037A8xx). This is a hand-written replacement with the same
 * calls and argument order as the game's copy, building display lists for the GE in
 * ge.c. The game's own copy can't be used as-is: it masks every address to a 28-bit
 * PSP physical address, which breaks host pointers.
 *
 * Constants and names follow the public PSPSDK where the meaning is the same. */
#ifndef PSP_GU_H
#define PSP_GU_H

#include <stdbool.h>
#include <stdint.h>

/* sceGuEnable / sceGuDisable states. */
enum {
    GU_ALPHA_TEST, GU_DEPTH_TEST, GU_SCISSOR_TEST, GU_STENCIL_TEST, GU_BLEND, GU_CULL_FACE,
    GU_DITHER, GU_FOG, GU_CLIP_PLANES, GU_TEXTURE_2D, GU_LIGHTING, GU_LIGHT0, GU_LIGHT1,
    GU_LIGHT2, GU_LIGHT3, GU_LINE_SMOOTH, GU_PATCH_CULL_FACE, GU_COLOR_TEST, GU_COLOR_LOGIC_OP,
    GU_FACE_NORMAL_REVERSE, GU_PATCH_FACE, GU_FRAGMENT_2X, GU_STATE_COUNT
};

/* List kinds for sceGuStart. 0 runs as it is built; 1 and 3 build lists that are
 * called from another list (they end in RET); 2 and 4 build complete lists to send. */
enum { GU_DIRECT = 0, GU_CALL = 1, GU_SEND = 2, GU_CALL2 = 3, GU_SEND2 = 4 };

/* Pixel formats. */
enum { GU_PSM_5650, GU_PSM_5551, GU_PSM_4444, GU_PSM_8888, GU_PSM_T4, GU_PSM_T8 };

/* Primitives. */
enum { GU_POINTS, GU_LINES, GU_LINE_STRIP, GU_TRIANGLES, GU_TRIANGLE_STRIP, GU_TRIANGLE_FAN, GU_SPRITES };

/* sceGuClear flags. */
#define GU_COLOR_BUFFER_BIT 1
#define GU_STENCIL_BUFFER_BIT 2
#define GU_DEPTH_BUFFER_BIT 4
#define GU_FAST_CLEAR_BIT 16

/* Vertex type bits (sceGuDrawArray). */
#define GU_TEXTURE_8BIT (1 << 0)
#define GU_TEXTURE_16BIT (2 << 0)
#define GU_TEXTURE_32BITF (3 << 0)
#define GU_COLOR_5650 (4 << 2)
#define GU_COLOR_5551 (5 << 2)
#define GU_COLOR_4444 (6 << 2)
#define GU_COLOR_8888 (7 << 2)
#define GU_VERTEX_8BIT (1 << 7)
#define GU_VERTEX_16BIT (2 << 7)
#define GU_VERTEX_32BITF (3 << 7)
#define GU_INDEX_8BIT (1 << 11)
#define GU_INDEX_16BIT (2 << 11)
#define GU_TRANSFORM_2D (1 << 23)

/* sceGumMatrixMode / sceGuSetMatrix. */
enum { GU_PROJECTION, GU_VIEW, GU_MODEL, GU_TEXTURE };

typedef struct ScePspFVector3 {
    float x, y, z;
} ScePspFVector3;

typedef struct ScePspFMatrix4 {
    float m[16]; /* column-major: m[0..3] is the x column */
} ScePspFMatrix4;

/* Log every sceGu call (used for the first frames with --gu-trace). */
void psp_gu_set_trace(bool on);

int sceGuInit(void);
void sceGuTerm(void);
int sceGuStart(int cid, void *list, int size);
int sceGuFinish(void);
int sceGuFinishId(unsigned int id);
int sceGuSync(int mode, int what);
void *sceGuSwapBuffers(void);
int sceGuDisplay(int on);
void sceGuCallList(const void *list);
void *sceGuGetMemory(int size);
void sceGuSendCommandi(int cmd, int argument);

void sceGuDrawBuffer(int psm, uint32_t fbp, int fbw);
void sceGuDispBuffer(int width, int height, uint32_t dispbp, int dispbw);
void sceGuDepthBuffer(uint32_t zbp, int zbw);
void sceGuOffset(unsigned int x, unsigned int y);
void sceGuViewport(int cx, int cy, int width, int height);
void sceGuScissor(int x, int y, int width, int height);
void sceGuEnable(int state);
void sceGuDisable(int state);

void sceGuSetDither(const int *matrix16);
void sceGuPatchDivision(int s, int t);
void sceGuColorMaterial(int components);
void sceGuSpecular(float power);
void sceGuShadeModel(int smooth);
void sceGuFrontFace(int order);
void sceGuDepthOffset(int offset);
void sceGuDepthRange(int near, int far);
void sceGuDepthFunc(int func);
void sceGuDepthMask(int mask);
void sceGuAlphaFunc(int func, int ref, int mask);
void sceGuStencilFunc(int func, int ref, int mask);
void sceGuStencilOp(int fail, int zfail, int zpass);
void sceGuColorFunc(int func, uint32_t color, uint32_t mask);
void sceGuBlendFunc(int op, int src, int dst, uint32_t fixa, uint32_t fixb);
void sceGuLogicalOp(int op);
void sceGuPixelMask(uint32_t mask);
void sceGuFog(float near, float far, uint32_t color);
void sceGuLightMode(int mode);
void sceGuAmbient(uint32_t color);
void sceGuAmbientColor(uint32_t color);
void sceGuMaterial(int mode, uint32_t color);
void sceGuColor(uint32_t color);

void sceGuTexMode(int psm, int maxmips, int a2, int swizzle);
void sceGuTexImage(int mipmap, int width, int height, int tbw, const void *tbp);
void sceGuTexFilter(int min, int mag);
void sceGuTexFunc(int tfx, int tcc);
void sceGuTexWrap(int u, int v);
void sceGuTexScale(float u, float v);
void sceGuTexOffset(float u, float v);
void sceGuTexEnvColor(uint32_t color);
void sceGuTexFlush(void);
void sceGuTexSync(void);
void sceGuClutMode(int cpsm, int shift, int mask, int a3);
void sceGuClutLoad(int num_blocks, const void *cbp);

void sceGuClearColor(uint32_t color);
void sceGuClearDepth(unsigned int depth);
void sceGuClearStencil(unsigned int stencil);
void sceGuClear(int flags);

void sceGuDrawArray(int prim, int vtype, int count, const void *indices, const void *vertices);
void sceGuSetMatrix(int type, const ScePspFMatrix4 *m);

/* Two calls in the game's libgu that aren't in the public SDK: a textured sprite with
 * a stored size. 0x003727EC sets the source (texel) and destination (pixel) sizes;
 * 0x00372808 draws a through-mode sprite with its top-left at (x, y) and its first
 * texel at (u, v). flip bit 0 mirrors u, bit 1 mirrors v; corner 1-3 picks which
 * corner the first texel goes to (swapping y, both, or x). */
void sceGuSpriteSize(int srcw, int srch, int dstw, int dsth);
void sceGuSprite(int x, int y, int z, int u, int v, int flip, int corner);

/* libgum: a matrix stack in caller-provided memory, uploaded by sceGumUpdateMatrix. */
void sceGumSetMatrixStack(ScePspFMatrix4 *mem, int proj_depth, int view_depth, int model_depth,
                          int tex_depth);
void sceGumMatrixMode(int mode);
void sceGumLoadIdentity(void);
void sceGumLoadMatrix(const ScePspFMatrix4 *m);
void sceGumMultMatrix(const ScePspFMatrix4 *m);
void sceGumPushMatrix(void);
void sceGumPopMatrix(void);
void sceGumOrtho(float left, float right, float bottom, float top, float near, float far);
void sceGumPerspective(float fovy, float aspect, float near, float far);
void sceGumLookAt(const ScePspFVector3 *eye, const ScePspFVector3 *center, const ScePspFVector3 *up);
void sceGumTranslate(const ScePspFVector3 *v);
void sceGumScale(const ScePspFVector3 *v);
void sceGumRotateX(float angle);
void sceGumRotateY(float angle);
void sceGumRotateZ(float angle);
void sceGumUpdateMatrix(void);

#endif
