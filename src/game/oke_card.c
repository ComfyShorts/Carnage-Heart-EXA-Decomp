/* See oke_card.h. Hand-written from COkeCard (0x089e4000 .. 0x089e7000):
 *   Initialize (FUN_089e5dcc) loads parts/bf/oc_wbg0..4, oc_stbg0/1, oc_hdbg,
 *   oc_hdbg1, oc_bso_0, bf_cover0 and the equipment icons bf_icon0;
 *   Draw (FUN_089e4b50) stacks drawWeightLine (FUN_089e4cf0), drawType (FUN_089e4f48)
 *   and drawHardware (FUN_089e6324), each band as high as its picture.
 *
 * The weight band is oc_wbg0..3 by the weight state (FUN_089edf40: under 70 %,
 * to 100 %, under 130 %, over), oc_wbg1 for an empty card and oc_wbg4 selected. The
 * name band is oc_stbg0, or oc_stbg1 for card states 2 and 3, with the name at
 * x + 7. The hardware band (oc_hdbg, oc_hdbg1 selected) has the OKE's picture
 * (CreatePicture FUN_089e431c: 48 x 48 index8 at hardware + 0x2B0, its 256 colours at
 * + 0xBB0) at (4, 2), the emblem (FUN_089e447c: 32 x 32 index4 at hardware + 0, its
 * 16 colours at + 0x200) at (0x7A, 0x13), both over a half-dark square, oc_bso_0 at
 * (0x35, 5), then CEquipmentIcon (FUN_088d88a4) 16 x 16 cells of bf_icon0: the
 * weapons 0xE apart from (0x3F, 3), the armour at (0x3F, 0x13), the extra armour at
 * (0x4D, 0x13), the options 0xE apart from (0x3F, 0x23).
 * Not ported: the chip program marks of the weight band (CHardString-like 0xFD /
 * 0xFE at x + 0x76, FUN_089e4608 / FUN_089ee26c check the chips). */
#include "oke_card.h"

#include <stdio.h>

#include "font.h"
#include "oke_parts.h"
#include "picture.h"
#include "savedata.h"
#include "ui.h"

#define HW_PICTURE 0x2B0
#define HW_PICTURE_PALETTE (HW_PICTURE + 0x900)
#define HW_EMBLEM 0x000
#define HW_EMBLEM_PALETTE 0x200

enum { P_WBG0, P_WBG1, P_WBG2, P_WBG3, P_WBG4, P_STBG0, P_STBG1, P_HDBG, P_HDBG1, P_BSO, P_ICON, P_COUNT };
static const char *const k_names[P_COUNT] = {
    "parts/bf/oc_wbg0.gim",  "parts/bf/oc_wbg1.gim",  "parts/bf/oc_wbg2.gim", "parts/bf/oc_wbg3.gim",
    "parts/bf/oc_wbg4.gim",  "parts/bf/oc_stbg0.gim", "parts/bf/oc_stbg1.gim", "parts/bf/oc_hdbg.gim",
    "parts/bf/oc_hdbg1.gim", "parts/bf/oc_bso_0.gim", "parts/bf/bf_icon0.gim",
};
static Picture *g_parts[P_COUNT];
static Picture *g_picture, *g_emblem; /* refilled from the card on every draw */

static Picture *part(int i)
{
    if (!g_parts[i])
        g_parts[i] = picture_load(k_names[i]);
    return g_parts[i];
}

static int height(Picture *p)
{
    return p ? picture_height(p) : 0;
}

static void blit(Picture *p, int x, int y)
{
    if (p)
        picture_draw(p, x, y, 0, BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
}

/* FUN_088d8770: a weapon's icon from its ability and category; -1 for none. */
static int weapon_icon(int w)
{
    int a = weapon_ability(w), c = weapon_category(w);
    if ((a >= 1 && a <= 10) || (a >= 0xD && a <= 0x12))
        return a;
    if (a == 0xB)
        return c == 0xB ? 0x15 : c == 0xC ? 0x14 : c == 0xD ? 0x13 : -1;
    if (a == 0xC)
        return c == 0xE ? 0x18 : c == 0xF ? 0x17 : c == 0x10 ? 0x16 : -1;
    return -1;
}

/* CEquipmentIcon::Draw: cell n - 1 of the sheet, 8 to a row, from row `base`. */
static void icon(int n, int base, int x, int y)
{
    Picture *p = part(P_ICON);
    if (!p || n <= 0)
        return;
    picture_draw_rect(p, x, y, 0, ((n - 1) & 7) * 16, base + ((n - 1) >> 3) * 16, 16, 16, BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
}

static Picture *card_picture(Picture **slot, const char *name, int format, int size, const uint8_t *pix,
                             const uint8_t *pal)
{
    if (!*slot)
        *slot = picture_from_pixels(name, format, size, size, pix, pal);
    else
        picture_set_pixels(*slot, pix, pal);
    return *slot;
}

int oke_card_draw(int number, int x, int y, bool selected)
{
    const OkeDataBase *card = savedata_card(number);
    int state = card ? (int)card->header[0] : 0, top = y;
    /* the weight band */
    Picture *wbg = part(!state ? P_WBG1 : selected ? P_WBG4 : P_WBG0 + oke_weight_state(card));
    blit(wbg, x, y);
    y += height(wbg);
    /* the name band */
    Picture *st = part(state >= 2 && state <= 3 ? P_STBG1 : P_STBG0);
    blit(st, x, y);
    char name[32] = "";
    if (state)
        savedata_card_name(number, name, sizeof name);
    /* the text at +0x3C (FUN_089e5dcc: font 0, 6, 12 x 12, spacing 4) */
    TextStyle ts = text_style(0, 6, 12, 12, 0, 4, 0xFFFFFFFF, 0); /* colour 0 */
    ui_text(name, &ts, x + 7, y + (height(st) - 12) / 2, 1.0f);
    y += height(st);
    /* the hardware band */
    Picture *hd = part(selected ? P_HDBG1 : P_HDBG);
    blit(hd, x, y);
    if (state) {
        ui_fill(x + 4, y + 2, 0x30, 0x30, 0x7F000000);
        ui_fill(x + 0x7A, y + 0x13, 0x20, 0x20, 0x7F000000);
        blit(card_picture(&g_picture, "card picture", 5, 48, card->hardware + HW_PICTURE,
                          card->hardware + HW_PICTURE_PALETTE), x + 4, y + 2);
        blit(card_picture(&g_emblem, "card emblem", 4, 32, card->hardware + HW_EMBLEM,
                          card->hardware + HW_EMBLEM_PALETTE), x + 0x7A, y + 0x13);
        blit(part(P_BSO), x + 0x35, y + 5);
        int body = hw_get(card, HW_BODY), ix = 0x3F;
        for (int s = 0; s < body_weapons(body) && s < OKE_WEAPON_SLOTS; s++) {
            int w = hw_get(card, HW_WEAPON + 4 * s);
            if (w) {
                icon(weapon_icon(w), 0, x + ix, y + 3);
                ix += 0xE;
            }
        }
        icon(hw_get(card, HW_ARMOR) + 1, 0x30, x + 0x3F, y + 0x13);
        icon(hw_get(card, HW_ADD_ARMOR), 0x50, x + 0x4D, y + 0x13);
        ix = 0x3F;
        for (int s = 0; s < body_options(body) && s < OKE_OPTION_SLOTS; s++) {
            int o = hw_get(card, HW_OPTION + 4 * s);
            if (o) {
                icon(o, 0x60, x + ix, y + 0x23);
                ix += 0xE;
            }
        }
    }
    y += height(hd);
    return y - top;
}
