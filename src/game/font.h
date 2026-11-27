/* CFontMngr (singleton, FUN_08b14f70) and FSTRING, the game's bitmap text.
 *
 * The fonts are in the "boot" file set: font/gothic.tbl maps character codes to cells
 * of two GIM sheets, font/gothic8.gim (single-byte characters, 8x16 cells) and
 * font/gothic16.gim (Shift-JIS double-byte, 16x16 cells, 8 sheets). Text is drawn
 * one sprite per glyph, each cell scaled to the style's advance and height. */
#ifndef GAME_FONT_H
#define GAME_FONT_H

#include <stdint.h>

/* stStyle, 8 words (CStyle::FromStruct, FUN_08b15ee0 fills one). */
typedef struct TextStyle {
    int font;      /* +0x00 */
    int half_adv;  /* +0x04 single-byte width */
    int full_adv;  /* +0x08 double-byte width */
    int height;    /* +0x0C */
    int space_x;   /* +0x10 extra space after each character */
    int space_y;   /* +0x14 extra space after each line */
    uint32_t color;/* +0x18 ABGR */
    int mode;      /* +0x1C layout mode; only 0 (left to right) is written */
} TextStyle;

/* FUN_08b15ee0 */
TextStyle text_style(int font, int half_adv, int full_adv, int height, int space_x, int space_y,
                     uint32_t color, int mode);

/* Loads the table and the single-byte sheet. Returns 0 if they're missing. */
int font_init(void);

/* FSTRING::GetDrawSize (FUN_08b15fb0). */
void text_measure(const char *s, const TextStyle *st, int *w, int *h);

/* FSTRING::Draw (FUN_08b161d0): top-left at (x, y). out_w/out_h may be NULL. */
void text_draw(const char *s, const TextStyle *st, int x, int y, int z, int *out_w, int *out_h);

#endif
