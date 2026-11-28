/* OKE Customization (CDesignMngr modes 2-6): the sub-menu and its four windows,
 * Design (paint pattern), Main Color, Accent Color and Polish. See oke_custom.c. */
#ifndef GAME_OKE_CUSTOM_H
#define GAME_OKE_CUSTOM_H

#include <stdbool.h>
#include <stdint.h>

void custom_open(int card);
/* 1 while open, 0 once it's back to the top menu. */
int custom_update(float dt);
void custom_draw(void);
const char *custom_help(void);

/* CColorEditWnd: three rows R, G, B (or four with A), each 0-100. */
typedef struct ColorEdit {
    int v[4], keep[4], row, rows;
    bool restore; /* cross puts the opening values back (FUN_088a122c's last argument) */
} ColorEdit;

void color_edit_open(ColorEdit *c, uint32_t packed, int rows, bool restore);
uint32_t color_edit_value(const ColorEdit *c);
/* Each frame: -1 while open, 1 on circle, 0 on cross. */
int color_edit_update(ColorEdit *c);
void color_edit_draw(const ColorEdit *c, int x, int y);

#endif
