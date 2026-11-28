/* See osk.h. */
#include "osk.h"

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "pad.h"
#include "psp_shim.h"
#include "sound.h"
#include "ui.h"

#define COLS 13

static const char *const k_rows[] = {
    "ABCDEFGHIJKLM", "NOPQRSTUVWXYZ", "abcdefghijklm", "nopqrstuvwxyz", "0123456789-_.", "!?&'()+,/:#@*",
};
#define ROWS ((int)(sizeof k_rows / sizeof k_rows[0]))

static struct {
    bool active;
    char title[64];
    char text[OSK_TEXT_MAX + 1];
    int max_len, row, col;
} g;

void osk_open(const char *title, const char *text, int max_len)
{
    memset(&g, 0, sizeof g);
    g.active = true;
    g.max_len = max_len < 1 ? 1 : max_len > OSK_TEXT_MAX ? OSK_TEXT_MAX : max_len;
    snprintf(g.title, sizeof g.title, "%s", title ? title : "");
    snprintf(g.text, sizeof g.text, "%s", text ? text : "");
    g.text[g.max_len] = '\0';
    SDL_Log("[osk] \"%s\": \"%s\" (up to %d characters)", g.title, g.text, g.max_len);
}

bool osk_active(void)
{
    return g.active;
}

const char *osk_text(void)
{
    return g.text;
}

static void put(char c)
{
    size_t n = strlen(g.text);
    if ((int)n < g.max_len) {
        g.text[n] = c;
        g.text[n + 1] = '\0';
        sound_se(SE_DECIDE, 1.0f);
    } else {
        sound_se(SE_ERROR, 1.0f);
    }
}

int osk_update(void)
{
    if (!g.active)
        return OSK_CANCEL;
    int r = g.row, c = g.col;
    if (pad_repeat(PSP_CTRL_UP))
        g.row = (g.row + ROWS - 1) % ROWS;
    else if (pad_repeat(PSP_CTRL_DOWN))
        g.row = (g.row + 1) % ROWS;
    else if (pad_repeat(PSP_CTRL_LEFT))
        g.col = (g.col + COLS - 1) % COLS;
    else if (pad_repeat(PSP_CTRL_RIGHT))
        g.col = (g.col + 1) % COLS;
    if (r != g.row || c != g.col)
        sound_se(SE_CURSOR, 1.0f);
    if (pad_repeat(PSP_CTRL_CIRCLE)) {
        put(k_rows[g.row][g.col]);
    } else if (pad_repeat(PSP_CTRL_TRIANGLE)) {
        put(' ');
    } else if (pad_repeat(PSP_CTRL_SQUARE)) {
        /* the last character, both bytes of a Shift-JIS one (the game's own texts are) */
        size_t n = strlen(g.text), last = 0;
        for (size_t i = 0; i < n;) {
            unsigned char ch = (unsigned char)g.text[i];
            last = i;
            i += ((ch >= 0x81 && ch <= 0x9F) || (ch >= 0xE0 && ch <= 0xFC)) && i + 1 < n ? 2 : 1;
        }
        if (n) {
            g.text[last] = '\0';
            sound_se(SE_CANCEL, 1.0f);
        }
    } else if (pad_pressed(PSP_CTRL_START)) {
        g.active = false;
        sound_se(SE_OK, 1.0f);
        SDL_Log("[osk] OK: \"%s\"", g.text);
        return OSK_OK;
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        g.active = false;
        sound_se(SE_CANCEL, 1.0f);
        SDL_Log("[osk] cancelled");
        return OSK_CANCEL;
    }
    return OSK_RUNNING;
}

void osk_draw(void)
{
    if (!g.active)
        return;
    const int cell = 22, w = COLS * cell + 24, h = 60 + ROWS * cell + 22;
    int x = (480 - w) / 2, y = (272 - h) / 2;
    ui_fill(0, 0, 480, 272, 0x80000000);
    ui_frame(ui_part(PART_BF_SWND4), x, y, w, h, 8, 1.0f);
    TextStyle dark = text_style(0, 6, 12, 12, 0, 4, 0xFF303030, 0);
    TextStyle key = text_style(0, 6, 12, 12, 0, 0, 0xFF303030, 0);
    TextStyle hint = text_style(0, 5, 10, 10, 0, 4, 0xFF606060, 0);
    ui_text(g.title, &dark, x + 12, y + 8, 1.0f);
    /* the field: the text, a cursor bar, and how many characters are left */
    ui_fill(x + 12, y + 26, w - 24, 18, 0xFFF0F0F0);
    int tw = ui_text(g.text, &dark, x + 16, y + 29, 1.0f);
    if ((SDL_GetTicks() / 400) & 1)
        ui_fill(x + 17 + tw, y + 28, 2, 14, 0xFF303030);
    char left[16];
    snprintf(left, sizeof left, "%d/%d", (int)strlen(g.text), g.max_len);
    ui_text(left, &hint, x + w - 12 - ui_text_width(left, &hint), y + 10, 1.0f);
    int gx = x + 12, gy = y + 52;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            int cx = gx + c * cell, cy = gy + r * cell;
            if (r == g.row && c == g.col)
                ui_frame(ui_part(PART_BF_CUR0), cx - 1, cy - 1, cell + 1, cell + 1, 6, ui_pulse(SDL_GetTicks() / 1000.0f));
            char s[2] = {k_rows[r][c], 0};
            ui_text(s, &key, cx + (cell - ui_text_width(s, &key)) / 2, cy + 5, 1.0f);
        }
    ui_text("O type   [] delete   /\\ space   START OK   X cancel", &hint, x + 12, y + h - 18, 1.0f);
}
