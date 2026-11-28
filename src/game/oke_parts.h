/* An OKE card's hardware as the OKE Configuration screens see it: CHardwareDataBase
 * (COkeDataBase + 0x30), the equipment tables in the EBOOT and CEquipmentSelectWnd's part
 * lists. Every table is read from the user's EBOOT at run time (gamedata.h); without
 * it the names are NULL and the numbers 0.
 *
 * The hardware block (offsets from COkeDataBase + 0x30):
 *   +0x240 body (OKE type, 0-0x24)       +0x244 paint pattern
 *   +0x248 / +0x24C main / accent colour  +0x250 polish     +0x254 flags
 *   +0x258 five weapons (0-0x3B)          +0x26C their ammo
 *   +0x280 armour thickness (0-11)        +0x284 additional armour (0-7)
 *   +0x288 CPU (0-7)                      +0x28C four options (0-10)
 *   +0x29C on-board energy %              +0x2A0 engine output % */
#ifndef GAME_OKE_PARTS_H
#define GAME_OKE_PARTS_H

#include <stdbool.h>
#include <stdint.h>

#include "okedb.h"

enum {
    HW_BODY = 0x240,
    HW_PAINT = 0x244,  /* 0-3, CColorDesignWnd */
    HW_MAIN_COLOR = 0x248,   /* R | G << 8 | B << 16 | A << 24, each 0-100; -1 unset */
    HW_ACCENT_COLOR = 0x24C,
    HW_POLISH = 0x250, /* 0-4, CColorLusterWnd */
    HW_WEAPON = 0x258,
    HW_AMMO = 0x26C,
    HW_ARMOR = 0x280,
    HW_ADD_ARMOR = 0x284,
    HW_CPU = 0x288,
    HW_OPTION = 0x28C,
    HW_ENERGY = 0x29C,
    HW_OUTPUT = 0x2A0,
};

#define OKE_BODIES 0x25
#define OKE_CPUS 8
#define OKE_ARMORS 12
#define OKE_ADD_ARMORS 8
#define OKE_WEAPONS 0x3C
#define OKE_WEAPON_TYPES 0x16
#define OKE_CATEGORIES 0x14
#define OKE_OPTIONS 11
#define OKE_WEAPON_SLOTS 5
#define OKE_OPTION_SLOTS 4

/* The hardware menu's items (CHardDesignMngr, FUN_088f9694 / FUN_088f7b00). */
typedef enum {
    HWI_BODY = 0,
    HWI_CPU = 1,
    HWI_ARMOR = 2,
    HWI_ADD_ARMOR = 3,
    HWI_WEAPON1 = 4,  /* .. HWI_WEAPON1 + 4 */
    HWI_ENERGY = 9,
    HWI_OUTPUT = 10,
    HWI_OPTION1 = 11, /* .. HWI_OPTION1 + 3 */
    HWI_COUNT = 15,
} HwItem;

int32_t hw_get(const OkeDataBase *card, int off);
void hw_set(OkeDataBase *card, int off, int32_t v);

/* ---- names (CEquipmentName / COkeName) ---- */
const char *oke_body_name(int body);       /* FUN_089f354c, 0x08c84dac */
const char *oke_cpu_name(int cpu);         /* FUN_088d8d7c, 0x08c7b5c8 */
const char *oke_armor_name(int armor);     /* FUN_088d8cc4, 0x08c7b568 */
const char *oke_add_armor_name(int a);     /* FUN_088d8d20, 0x08c7b598 */
const char *oke_weapon_name(int weapon);   /* FUN_088d8c0c, 0x08c7b44c */
const char *oke_category_name(int cat);    /* FUN_088d8dd8, 0x08c7b3fc */
const char *oke_option_name(int option);   /* FUN_088d8c68, 0x08c7b53c */
const char *hard_string(int id);           /* CHardString (FUN_088ffb4c, 0x08c7ee48) */

/* ---- the body table (0x08c14d84, 0x68 bytes a body) ---- */
int body_durability(int body);  /* +0x00 */
int body_weight(int body);      /* +0x04 */
int body_payload(int body);     /* +0x08, before the engine output */
int body_energy(int body);      /* +0x0C */
int body_heat(int body);        /* +0x10 */
int body_options(int body);     /* +0x14: option slots (FUN_089ed7ac) */
int body_weapons(int body);     /* +0x18: weapon slots (FUN_089ed784) */

/* CBattleOkeEquipment: a weapon's type (0x08bd5574 +0) and category (+0x10). */
int weapon_type(int weapon);
int weapon_category(int weapon);
int weapon_ability(int weapon); /* +4 (FUN_08855fb8) */
/* CHardwareDataBase FUN_089eda88: how much of weapon type `type` slot `slot` of the
 * body takes, -1 when it can't. */
int slot_capacity(const OkeDataBase *card, int slot, int type);

/* ---- what the card adds up to ---- */
/* The tuning sliders' numbers (CHardDesignMngr modes 0x10 / 0x11, see oke_parts.c). */
int oke_energy_amount(const OkeDataBase *card);
int oke_energy_weight(const OkeDataBase *card);
int oke_heat_resist(const OkeDataBase *card);
int oke_consumption(const OkeDataBase *card);
int oke_payload(const OkeDataBase *card);       /* FUN_089ed624 */
int oke_weight(const OkeDataBase *card);        /* FUN_089edebc */
/* FUN_089edf40: 0 under 70% of the payload, 1 up to 100%, 2 up to 130%, 3 over. */
int oke_weight_state(const OkeDataBase *card);
int weapon_weight(int weapon, int ammo);        /* FUN_089edc3c */
int cpu_weight(int cpu);                        /* FUN_0885679c */
int armor_weight(const OkeDataBase *card, int armor);         /* FUN_089edd54 */
int add_armor_weight(const OkeDataBase *card, int add_armor); /* FUN_089eddcc */
int option_weight(int option);                  /* FUN_0885688c */
int cpu_program_width(int cpu, int program);    /* FUN_0885680c / FUN_08856824 */

/* ---- the parts lists (CEquipmentSelectWnd, FUN_088da8b8) ----
 * The ids of the parts the player can choose for item `item` on `card`, in the
 * game's order: bodies by the table at 0x08be8a80, weapons by category; parts not
 * in stock left out. Returns the count. */
#define PART_LIST_MAX 0x3D
int part_list(const OkeDataBase *card, int item, int *ids);
const char *part_name(int item, int id);
/* The CHardString text shown for a part in the list. */
const char *part_help(int item, int id);
/* Whether item `item` is on the hardware menu for this body (FUN_088f9694). */
bool hw_item_shown(const OkeDataBase *card, int item);
/* The part item `item` holds now. */
int hw_item_part(const OkeDataBase *card, int item);

/* Puts part `id` on item `item`, as the confirm functions do:
 *   body  FUN_088f4df4: the body's default loadout (FUN_089f3a14), keeping paint,
 *         colours, CPU, armour and the energy settings;
 *   CPU   FUN_088f5020: then resizes the program (FUN_089eeb44);
 *   weapon FUN_088f45c4 with the ammo of FUN_088da16c;
 *   the rest set the field. */
void hw_equip(OkeDataBase *card, int item, int id);

/* FUN_089f3a14 on a blank card, then FUN_089eeb44: a new design with `body`. */
void hw_new_design(OkeDataBase *card, int body);

/* CSoftDesignMngr FUN_08a831a8: the three programs resized to the CPU's widths,
 * chips inside the new size kept, new ones blank. */
void sw_resize(OkeDataBase *card);

#endif
