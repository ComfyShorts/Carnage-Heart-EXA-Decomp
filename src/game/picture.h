/* Textures and 2D drawing.
 *
 * The game keeps its images as GIM files (Sony's picture format) and draws them with
 * its CPicture helpers, which set up the texture and emit a sprite through two
 * libgu calls (sceGuSpriteSize / sceGuSprite). This file has:
 *   - a GIM reader (the game uses Sony's GIM runtime, e.g. FUN_08b581c4 to bind),
 *   - the texture manager's load-by-name cache (FUN_08b1fd84: find FUN_08b1fcf8,
 *     create FUN_08b1fa24), loading through ReadFile from the open file sets,
 *   - CPicture::Draw (FUN_08b08c78) and its two shorthands, and SetBlend (FUN_08b08da4). */
#ifndef GAME_PICTURE_H
#define GAME_PICTURE_H

#include <stdint.h>

typedef struct Picture Picture;

/* Loads "name" (e.g. "title/logo_a.gim") from the open file sets, or returns the
 * already-loaded copy with its reference count raised. NULL if missing or unreadable. */
Picture *picture_load(const char *name);
void picture_release(Picture *p);

int picture_width(const Picture *p);  /* FUN_08b57b9c(level 0) */
int picture_height(const Picture *p); /* FUN_08b57bec(level 0) */
int picture_frames(const Picture *p);

/* FUN_08b57950: which frame (animation/sub-image) binding uses. */
void picture_set_frame(Picture *p, int frame);

/* FUN_08b088b4 -> FUN_08b58d14: texture mode, image and palette into the list. */
void picture_bind(const Picture *p);

/* CPicture::SetBlend (FUN_08b08da4): 0 replace, 1 alpha (the default), 2 additive,
 * 3 subtractive, 4 min, 5 max, 6 difference. */
enum { BLEND_REPLACE, BLEND_ALPHA, BLEND_ADD, BLEND_SUB, BLEND_MIN, BLEND_MAX, BLEND_DIFF };
void picture_set_blend(int mode);

/* CPicture::Draw (FUN_08b08c78): the source rectangle (u, v, srcw, srch) to the
 * destination (x, y, dstw, dsth) at depth z, tinted by color (ABGR). wrapu/wrapv set
 * clamping for the draw; flip bit 0/1 mirror; corner as in sceGuSprite. */
void picture_draw_ex(const Picture *p, int x, int y, int z, int dstw, int dsth, int u, int v, int srcw,
                     int srch, int blend, uint32_t color, int wrapu, int wrapv, int flip, int corner);

/* FUN_08b08afc: the whole picture at (x, y); corner 1 and 3 turn it a quarter. */
void picture_draw(const Picture *p, int x, int y, int z, int blend, uint32_t color, int flip, int corner);

/* FUN_08b08c14: part of the picture, unscaled. */
void picture_draw_rect(const Picture *p, int x, int y, int z, int u, int v, int w, int h, int blend,
                       uint32_t color, int flip, int corner);

#endif
