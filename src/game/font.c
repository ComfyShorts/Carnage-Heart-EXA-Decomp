/* See font.h.
 *
 * gothic.tbl: "FTTH", then at +0x18 the offset of the single-byte table and at +0x1C
 * that of the double-byte table. Each table: +0x0C columns per sheet row, +0x18 rows
 * per sheet, +0x1C cell width (u8), +0x1D cell height (u8), +0x20 the code-to-glyph
 * map (u8 entries in the single-byte table, u16 in the double-byte one). Glyph g sits
 * at column g % columns of row g / columns, and rows run on from one sheet (GIM
 * frame) to the next. */
#include "font.h"

#include <string.h>

#include <SDL3/SDL.h>

#include "fileio.h"
#include "gu.h"
#include "heap.h"
#include "picture.h"

typedef struct GlyphTable {
    int columns, rows_per_sheet, cell_w, cell_h;
    const uint8_t *map;
    int map_entries;
} GlyphTable;

static struct {
    uint8_t *tbl;
    GlyphTable single, wide;
    Picture *sheet8, *sheet16;
    int ready;
} g_font;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int read_table(GlyphTable *t, uint32_t off, int32_t size, int entry_size)
{
    if (off + 0x20 > (uint32_t)size)
        return 0;
    const uint8_t *p = g_font.tbl + off;
    t->columns = (int)rd32(p + 0x0C);
    t->rows_per_sheet = (int)rd32(p + 0x18);
    t->cell_w = p[0x1C];
    t->cell_h = p[0x1D];
    t->map = p + 0x20;
    t->map_entries = (int)(((uint32_t)size - off - 0x20) / (uint32_t)entry_size);
    return t->columns > 0 && t->rows_per_sheet > 0;
}

TextStyle text_style(int font, int half_adv, int full_adv, int height, int space_x, int space_y,
                     uint32_t color, int mode)
{
    return (TextStyle){font, half_adv, full_adv, height, space_x, space_y, color, mode};
}

int font_init(void)
{
    if (g_font.ready)
        return 1;
    int32_t size = fileio_file_size("font/gothic.tbl");
    if (size < 0x20) {
        SDL_Log("[font] font/gothic.tbl is not in an open file set");
        return 0;
    }
    g_font.tbl = heap_alloc(&g_default_heap, (uint32_t)size, 16, "font", 0);
    if (!g_font.tbl || fileio_read_file("font/gothic.tbl", g_font.tbl, (uint32_t)size) != size ||
        memcmp(g_font.tbl, "FTTH", 4) != 0 ||
        !read_table(&g_font.single, rd32(g_font.tbl + 0x18), size, 1) ||
        !read_table(&g_font.wide, rd32(g_font.tbl + 0x1C), size, 2)) {
        SDL_Log("[font] can't read font/gothic.tbl");
        return 0;
    }
    g_font.sheet8 = picture_load("font/gothic8.gim");
    if (!g_font.sheet8)
        return 0;
    g_font.sheet16 = picture_load("font/gothic16.gim"); /* FUN_08b15ae8(0, 1) */
    if (!g_font.sheet16)
        SDL_Log("[font] font/gothic16.gim is missing: two-byte characters won't be drawn");
    g_font.ready = 1;
    SDL_Log("[font] single-byte font: %dx%d cells, %d columns, %d rows per sheet", g_font.single.cell_w,
            g_font.single.cell_h, g_font.single.columns, g_font.single.rows_per_sheet);
    return 1;
}

/* FUN_08b17054 with FUN_08b0aa6c: 0 single-byte, 1 Shift-JIS lead byte, 3 CR, 4 LF,
 * 5 end; anything else (2) is skipped. */
static int char_class(uint8_t c)
{
    if ((c >= 0x20 && c <= 0x7F) || (c >= 0xA1 && c <= 0xDF))
        return 0;
    if ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC))
        return 1;
    if (c == '\r')
        return 3;
    if (c == '\n')
        return 4;
    if (c == 0)
        return 5;
    return 2;
}

/* CFontMngr::DrawChar for the single-byte sheet. Codes run from the space: ASCII
 * 0x20-0x7F become 0-0x5F and half-width katakana 0xA1-0xDF become 0x60-0x9E. */
static void draw_single(uint8_t c, const TextStyle *st, int x, int y, int z)
{
    const GlyphTable *t = &g_font.single;
    int code = c < 0x80 ? c - 0x20 : c - 0x41;
    if (code < 0 || code >= t->map_entries)
        return;
    int g = t->map[code];
    int row = g / t->columns, col = g % t->columns;
    picture_set_frame(g_font.sheet8, row / t->rows_per_sheet);
    row %= t->rows_per_sheet;
    picture_bind(g_font.sheet8);
    sceGuSpriteSize(t->cell_w, t->cell_h, st->half_adv, st->height);
    sceGuSprite(x, y, z, col * t->cell_w, row * t->cell_h, 0, 0);
}

/* FUN_08b0aa28 (FUN_08b0ab28): a Shift-JIS pair to JIS, worked in bytes; then
 * FUN_08b14f20: the JIS row and cell to the double-byte table's index, rows 0x21-0x28
 * and 0x30 on packed together, 0x5E cells a row. */
static int wide_code(uint8_t s1, uint8_t s2)
{
    uint8_t j1, j2;
    if (s1 > 0xDF)
        s1 = (uint8_t)(s1 - 0x40);
    if (s2 >= 0x80)
        s2 = (uint8_t)(s2 - 1);
    if (s2 < 0x9E) {
        j1 = (uint8_t)(s1 * 2 + 0x1F);
        j2 = (uint8_t)(s2 - 0x1F);
    } else {
        j1 = (uint8_t)(s1 * 2 + 0x20);
        j2 = (uint8_t)(s2 + 0x83);
    }
    uint8_t row = (uint8_t)(j1 < 0x29 ? j1 - 0x21 : j1 - 0x28);
    return row * 0x5E + (uint8_t)(j2 - 0x21);
}

/* CFontMngr::DrawChar for the double-byte sheet (FUN_08b15798): nothing when the map
 * entry is 0. */
static void draw_wide(uint8_t s1, uint8_t s2, const TextStyle *st, int x, int y, int z)
{
    const GlyphTable *t = &g_font.wide;
    int code = wide_code(s1, s2);
    if (!g_font.sheet16 || code < 0 || code >= t->map_entries)
        return;
    int g = t->map[2 * code] | t->map[2 * code + 1] << 8;
    if (!g)
        return;
    int row = g / t->columns, col = g % t->columns;
    picture_set_frame(g_font.sheet16, row / t->rows_per_sheet);
    row %= t->rows_per_sheet;
    picture_bind(g_font.sheet16);
    sceGuSpriteSize(t->cell_w, t->cell_h, st->full_adv, st->height);
    sceGuSprite(x, y, z, col * t->cell_w, row * t->cell_h, 0, 0);
}

/* Walks the string the way GetDrawSize and Draw both do (mode 0). */
static void layout(const char *s, const TextStyle *st, int draw, int x0, int y0, int z, int *w, int *h)
{
    int x = 0, y = 0, max_w = 0, lines_h = 0;
    const uint8_t *p = (const uint8_t *)s;
    for (;;) {
        int cls = char_class(*p);
        if (cls == 0) {
            if (draw)
                draw_single(*p, st, x0 + x, y0 + y, z);
            x += st->half_adv + st->space_x;
            p++;
        } else if (cls == 1) {
            if (draw && p[1])
                draw_wide(p[0], p[1], st, x0 + x, y0 + y, z);
            x += st->full_adv + st->space_x;
            p += p[1] ? 2 : 1;
        } else if (cls == 3 || cls == 4 || cls == 5) {
            if (x > max_w)
                max_w = x;
            if (cls == 5) {
                if (x != 0 || lines_h == 0)
                    lines_h += st->height + st->space_y;
                break;
            }
            lines_h += st->height + st->space_y;
            y += st->height + st->space_y;
            x = 0;
            p++;
        } else {
            p++;
        }
    }
    if (w)
        *w = max_w;
    if (h)
        *h = lines_h;
}

void text_measure(const char *s, const TextStyle *st, int *w, int *h)
{
    if (!s) {
        *w = *h = 0;
        return;
    }
    layout(s, st, 0, 0, 0, 0, w, h);
}

void text_draw(const char *s, const TextStyle *st, int x, int y, int z, int *out_w, int *out_h)
{
    if (!s || !font_init())
        return;
    /* CFontMngr::BeginDraw: textures on, the style's colour. */
    sceGuEnable(GU_TEXTURE_2D);
    sceGuColor(st->color);
    sceGuTexWrap(1, 1);
    layout(s, st, 1, x, y, z, out_w, out_h);
    sceGuTexWrap(0, 0);
}
