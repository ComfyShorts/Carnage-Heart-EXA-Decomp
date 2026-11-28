/* See oke_parts.h. Hand-written from CHardwareDataBase (FUN_089ed208..FUN_089edf40),
 * CBattleOkeEquipment's tables (FUN_08855f54..FUN_08856904), CEquipmentName and
 * CEquipmentSelectWnd (FUN_088d95f8..FUN_088d9df0), CHardDesignMngr's confirm
 * functions (FUN_088f4df4, FUN_088f5020, FUN_088f45c4) and CSoftDesignMngr's resize
 * (FUN_08a831a8). The tables themselves stay in the user's EBOOT. */
#include "oke_parts.h"

#include <string.h>

#include "gamedata.h"
#include "savedata.h"

/* splat addresses (Ghidra - 0x08804000) */
#define T_BODY_NAME 0x00480DACu  /* COkeName, two pointers a body, the name first */
#define T_CPU_NAME 0x004775C8u
#define T_ARMOR_NAME 0x00477568u
#define T_ADD_ARMOR_NAME 0x00477598u
#define T_WEAPON_NAME 0x0047744Cu
#define T_CATEGORY_NAME 0x004773FCu
#define T_OPTION_NAME 0x0047753Cu
#define T_HARD_STRING 0x0047AE48u /* 0xFF pointers */
#define T_BODY 0x00410D84u        /* 0x68 bytes a body */
#define T_WEAPON 0x003D1574u      /* 0x14 bytes a weapon: +0 type, +0x10 category */
#define T_WEAPON_WEIGHT 0x003D1A28u /* same rows: +0 weight, +4 per round */
#define T_SLOT_CAP 0x00411C88u    /* s16 [body 0xDC][type 10][slot 2] */
#define T_CPU_CLASS 0x003D1F68u
#define T_CPU_SPEED 0x003D1F88u
#define T_CPU_WIDTH 0x003D1FA8u   /* per class */
#define T_CPU_CLASS_WEIGHT 0x003D1FBCu
#define T_ARMOR_WEIGHT 0x003D1ED8u /* 0xC bytes a thickness, +0: % of the body's weight */
#define T_ADD_ARMOR_WEIGHT 0x003D2064u
#define T_OPTION_WEIGHT 0x003D2038u
#define T_BODY_ORDER 0x003E4A80u  /* the body list's order (0x08be8a80) */

int32_t hw_get(const OkeDataBase *card, int off)
{
    int32_t v;
    memcpy(&v, card->hardware + off, 4);
    return v;
}

void hw_set(OkeDataBase *card, int off, int32_t v)
{
    memcpy(card->hardware + off, &v, 4);
}

static int32_t word(uint32_t addr)
{
    return (int32_t)gamedata_word(addr, 0);
}

static const char *ptr_string(uint32_t table, int i)
{
    uint32_t p = gamedata_word(table + 4u * (uint32_t)i, 0);
    if (p >= 0x08804000u)
        p -= 0x08804000u;
    return p ? gamedata_string(p) : NULL;
}

static const char *named(uint32_t table, int i, int count)
{
    return i >= 0 && i < count ? ptr_string(table, i) : NULL;
}

const char *oke_body_name(int body) { return body >= 0 && body < OKE_BODIES ? ptr_string(T_BODY_NAME, body * 2) : NULL; }
const char *oke_cpu_name(int cpu) { return named(T_CPU_NAME, cpu, OKE_CPUS); }
const char *oke_armor_name(int a) { return named(T_ARMOR_NAME, a, OKE_ARMORS); }
const char *oke_add_armor_name(int a) { return named(T_ADD_ARMOR_NAME, a, OKE_ADD_ARMORS); }
const char *oke_weapon_name(int w) { return named(T_WEAPON_NAME, w, OKE_WEAPONS); }
const char *oke_category_name(int c) { return named(T_CATEGORY_NAME, c, OKE_CATEGORIES); }
const char *oke_option_name(int o) { return named(T_OPTION_NAME, o, OKE_OPTIONS); }

const char *hard_string(int id)
{
    return ptr_string(T_HARD_STRING, id >= 0 && id < 0xFF ? id : 1);
}

/* ---- tables ---- */

static int body_word(int body, int off)
{
    return body >= 0 && body < OKE_BODIES ? word(T_BODY + 0x68u * (uint32_t)body + (uint32_t)off) : 0;
}

int body_durability(int body) { return body_word(body, 0x00); }
int body_weight(int body) { return body_word(body, 0x04); }
int body_payload(int body) { return body_word(body, 0x08); }
int body_energy(int body) { return body_word(body, 0x0C); }
int body_heat(int body) { return body_word(body, 0x10); }
int body_options(int body) { return body_word(body, 0x14); }
int body_weapons(int body) { return body_word(body, 0x18); }

int weapon_type(int w)
{
    return w >= 0 && w < OKE_WEAPONS ? word(T_WEAPON + 0x14u * (uint32_t)w) : 0;
}

int weapon_ability(int w)
{
    return w >= 0 && w < OKE_WEAPONS ? word(T_WEAPON + 0x14u * (uint32_t)w + 4) : 0;
}

int weapon_category(int w)
{
    return w >= 0 && w < OKE_WEAPONS ? word(T_WEAPON + 0x14u * (uint32_t)w + 0x10) : 0;
}

int slot_capacity(const OkeDataBase *card, int slot, int type)
{
    int body = hw_get(card, HW_BODY);
    if (body < 0 || body >= OKE_BODIES || slot < 0 || slot >= OKE_WEAPON_SLOTS || type < 0 || type >= OKE_WEAPON_TYPES)
        return -1;
    uint32_t a = T_SLOT_CAP + 0xDCu * (uint32_t)body + 10u * (uint32_t)type + 2u * (uint32_t)slot;
    uint32_t w = gamedata_word(a & ~3u, 0xFFFFFFFFu);
    return (int16_t)(w >> ((a & 2) * 8));
}

/* FUN_089edb24: the ammo a newly fitted weapon of `type` gets. */
static int default_ammo(const OkeDataBase *card, int slot, int type)
{
    int cap = slot_capacity(card, slot, type);
    if (cap > 0) {
        if (type >= 1 && type <= 8)
            return 100;
        if (type == 9)
            return 30;
    }
    return cap;
}

/* ---- weights ---- */

/* FUN_089ed6b0: the energy carried, the body's capacity (+0xC) x on-board %. */
int oke_energy_amount(const OkeDataBase *card)
{
    int e = body_energy(hw_get(card, HW_BODY)), pct = hw_get(card, HW_ENERGY);
    return pct > 0 ? e * pct / 100 : e;
}

/* FUN_089edba4 / FUN_089edbd4: what that energy weighs, 200 per 1000. */
int oke_energy_weight(const OkeDataBase *card)
{
    return oke_energy_amount(card) * 200 / 1000;
}

/* FUN_089edfdc: the heat resistance, 200 - on-board %. */
int oke_heat_resist(const OkeDataBase *card)
{
    return 200 - hw_get(card, HW_ENERGY);
}

/* FUN_089ed528: energy use, the output %, past 100 growing with the square. */
int oke_consumption(const OkeDataBase *card)
{
    int o = hw_get(card, HW_OUTPUT);
    return o > 100 ? o + (o - 100) * (o - 100) / 10 : o;
}

int oke_payload(const OkeDataBase *card)
{
    return body_payload(hw_get(card, HW_BODY)) * hw_get(card, HW_OUTPUT) / 100;
}

int weapon_weight(int w, int ammo)
{
    if (w < 0 || w >= OKE_WEAPONS)
        return 0;
    return word(T_WEAPON_WEIGHT + 0x14u * (uint32_t)w) + word(T_WEAPON_WEIGHT + 0x14u * (uint32_t)w + 4) * ammo;
}

static int cpu_class(int cpu)
{
    return cpu >= 0 && cpu < OKE_CPUS ? word(T_CPU_CLASS + 4u * (uint32_t)cpu) : 0;
}

int cpu_weight(int cpu)
{
    if (cpu < 0 || cpu >= OKE_CPUS)
        return 0;
    int top = word(T_CPU_SPEED + 4u * 7);
    return word(T_CPU_CLASS_WEIGHT + 4u * (uint32_t)cpu_class(cpu)) +
           (top ? word(T_CPU_SPEED + 4u * (uint32_t)cpu) * 100 / top : 0);
}

int armor_weight(const OkeDataBase *card, int armor)
{
    if (armor < 0 || armor >= OKE_ARMORS)
        return 0;
    return word(T_ARMOR_WEIGHT + 0xCu * (uint32_t)armor) * body_weight(hw_get(card, HW_BODY)) / 100;
}

int add_armor_weight(const OkeDataBase *card, int a)
{
    if (a < 0 || a >= OKE_ADD_ARMORS)
        return 0;
    return word(T_ADD_ARMOR_WEIGHT + 4u * (uint32_t)a) * body_weight(hw_get(card, HW_BODY)) / 100;
}

int option_weight(int o)
{
    return o >= 0 && o < OKE_OPTIONS ? word(T_OPTION_WEIGHT + 4u * (uint32_t)o) : 0;
}

int oke_weight(const OkeDataBase *card)
{
    int body = hw_get(card, HW_BODY);
    int w = body_weight(body) + oke_energy_weight(card) + armor_weight(card, hw_get(card, HW_ARMOR)) +
            add_armor_weight(card, hw_get(card, HW_ADD_ARMOR));
    for (int s = 0; s < body_weapons(body) && s < OKE_WEAPON_SLOTS; s++)
        w += weapon_weight(hw_get(card, HW_WEAPON + 4 * s), hw_get(card, HW_AMMO + 4 * s));
    for (int s = 0; s < body_options(body) && s < OKE_OPTION_SLOTS; s++)
        w += option_weight(hw_get(card, HW_OPTION + 4 * s));
    return w + cpu_weight(hw_get(card, HW_CPU));
}

int oke_weight_state(const OkeDataBase *card)
{
    int payload = oke_payload(card);
    if (payload <= 0)
        return 3;
    float pct = (float)oke_weight(card) / (float)payload * 100.0f;
    if (pct < 70.0f)
        return 0;
    if (pct <= 100.0f)
        return 1;
    return pct < 130.0f ? 2 : 3;
}

int cpu_program_width(int cpu, int program)
{
    int cls = cpu_class(cpu);
    if (program == 0)
        return cls >= 0 && cls < 4 ? word(T_CPU_WIDTH + 4u * (uint32_t)cls) : 0;
    return cls == 3 ? 7 : 0;
}

/* ---- the parts lists ---- */

/* The lists offer only the parts the player owns: stock 2 in the save (savedata.h;
 * a new Battle Mode game owns all but bodies 14 and 33, the Shop sells the rest). */
static bool owned(int group, int id)
{
    return savedata_stock(group, id) == 2;
}

bool hw_item_shown(const OkeDataBase *card, int item)
{
    int body = hw_get(card, HW_BODY);
    if (item >= HWI_WEAPON1 && item < HWI_WEAPON1 + OKE_WEAPON_SLOTS)
        return item - HWI_WEAPON1 < body_weapons(body);
    if (item >= HWI_OPTION1 && item < HWI_OPTION1 + OKE_OPTION_SLOTS)
        return item - HWI_OPTION1 < body_options(body);
    return item >= 0 && item < HWI_COUNT;
}

int hw_item_part(const OkeDataBase *card, int item)
{
    switch (item) {
    case HWI_BODY: return hw_get(card, HW_BODY);
    case HWI_CPU: return hw_get(card, HW_CPU);
    case HWI_ARMOR: return hw_get(card, HW_ARMOR);
    case HWI_ADD_ARMOR: return hw_get(card, HW_ADD_ARMOR);
    case HWI_ENERGY: return hw_get(card, HW_ENERGY);
    case HWI_OUTPUT: return hw_get(card, HW_OUTPUT);
    default:
        if (item >= HWI_WEAPON1 && item < HWI_WEAPON1 + OKE_WEAPON_SLOTS)
            return hw_get(card, HW_WEAPON + 4 * (item - HWI_WEAPON1));
        if (item >= HWI_OPTION1 && item < HWI_OPTION1 + OKE_OPTION_SLOTS)
            return hw_get(card, HW_OPTION + 4 * (item - HWI_OPTION1));
        return 0;
    }
}

int part_list(const OkeDataBase *card, int item, int *ids)
{
    int n = 0;
    switch (item) {
    case HWI_BODY: /* FUN_088d95f8 */
        for (int i = 0; i < OKE_BODIES; i++) {
            int b = (int)gamedata_word(T_BODY_ORDER + 4u * (uint32_t)i, (uint32_t)i);
            if (b >= 0 && b < OKE_BODIES && owned(STOCK_BODY, b))
                ids[n++] = b;
        }
        return n;
    case HWI_CPU: /* FUN_088d976c */
        for (int i = 0; i < OKE_CPUS; i++)
            if (owned(STOCK_CPU, i))
                ids[n++] = i;
        return n;
    case HWI_ARMOR: /* FUN_088d98cc */
        for (int i = 0; i < OKE_ARMORS; i++)
            if (owned(STOCK_ARMOR, i))
                ids[n++] = i;
        return n;
    case HWI_ADD_ARMOR: /* FUN_088d9a2c */
        for (int i = 0; i < OKE_ADD_ARMORS; i++)
            if (owned(STOCK_ADD_ARMOR, i))
                ids[n++] = i;
        return n;
    default:
        break;
    }
    if (item >= HWI_WEAPON1 && item < HWI_WEAPON1 + OKE_WEAPON_SLOTS) { /* FUN_088d9b8c */
        int slot = item - HWI_WEAPON1;
        if (slot_capacity(card, slot, 0) >= 0)
            ids[n++] = 0; /* "No Weapon" */
        for (int cat = 0; cat < OKE_CATEGORIES; cat++)
            for (int w = 0; w < OKE_WEAPONS && n < PART_LIST_MAX; w++)
                if (weapon_category(w) == cat && owned(STOCK_WEAPON, w) &&
                    slot_capacity(card, slot, weapon_type(w)) > 0)
                    ids[n++] = w;
        return n;
    }
    if (item >= HWI_OPTION1 && item < HWI_OPTION1 + OKE_OPTION_SLOTS) { /* FUN_088d9df0 */
        for (int i = 0; i < OKE_OPTIONS; i++)
            if (owned(STOCK_OPTION, i))
                ids[n++] = i;
        return n;
    }
    return 0;
}

const char *part_name(int item, int id)
{
    switch (item) {
    case HWI_BODY: return oke_body_name(id);
    case HWI_CPU: return oke_cpu_name(id);
    case HWI_ARMOR: return oke_armor_name(id);
    case HWI_ADD_ARMOR: return oke_add_armor_name(id);
    default:
        if (item >= HWI_WEAPON1 && item < HWI_WEAPON1 + OKE_WEAPON_SLOTS)
            return oke_weapon_name(id);
        if (item >= HWI_OPTION1 && item < HWI_OPTION1 + OKE_OPTION_SLOTS)
            return oke_option_name(id);
        return NULL;
    }
}

/* The descriptions follow each list's order in CHardString: bodies from 0x0B, CPUs
 * from 0x30, weapons from 0x4D (FUN_088f45c4 adds 0x4D), options from 0x8A,
 * armour 0x95, additional armour from 0x96. */
const char *part_help(int item, int id)
{
    switch (item) {
    case HWI_BODY: return hard_string(0x0B + id);
    case HWI_CPU: return hard_string(0x30 + id);
    case HWI_ARMOR: return hard_string(0x95);
    case HWI_ADD_ARMOR: return hard_string(0x96 + id);
    default:
        if (item >= HWI_WEAPON1 && item < HWI_WEAPON1 + OKE_WEAPON_SLOTS)
            return hard_string(0x4D + id);
        if (item >= HWI_OPTION1 && item < HWI_OPTION1 + OKE_OPTION_SLOTS)
            return hard_string(0x8A + id);
        return NULL;
    }
}

/* ---- changing parts ---- */

/* FUN_089f3a14: a body's defaults. Each weapon slot gets the first weapon type it
 * takes (type 0, none, when it may stay empty) and that type's first weapon. */
static void body_defaults(OkeDataBase *card, int body)
{
    hw_set(card, HW_BODY, body);
    hw_set(card, HW_PAINT, 0);
    hw_set(card, 0x248, (int32_t)0xFF464646);
    hw_set(card, 0x24C, (int32_t)0xFF646464);
    hw_set(card, 0x250, 0);
    hw_set(card, HW_ENERGY, 100);
    hw_set(card, HW_OUTPUT, 100);
    hw_set(card, HW_ARMOR, 0);
    hw_set(card, HW_ADD_ARMOR, 0);
    hw_set(card, HW_CPU, 0);
    for (int s = 0; s < OKE_WEAPON_SLOTS; s++) {
        int type = 0, ammo = 0, weapon = 0;
        if (s < body_weapons(body)) {
            for (type = 0; type < OKE_WEAPON_TYPES; type++)
                if ((ammo = default_ammo(card, s, type)) >= 0)
                    break;
            if (type == OKE_WEAPON_TYPES)
                type = 0;
            for (weapon = 0; weapon < OKE_WEAPONS && weapon_type(weapon) != type; weapon++) {
            }
            if (weapon == OKE_WEAPONS)
                weapon = 0;
        }
        hw_set(card, HW_WEAPON + 4 * s, weapon);
        hw_set(card, HW_AMMO + 4 * s, ammo > 0 ? ammo : 0);
    }
    for (int s = 0; s < OKE_OPTION_SLOTS; s++)
        hw_set(card, HW_OPTION + 4 * s, 0);
}

void sw_resize(OkeDataBase *card)
{
    SoftwareDataBase *sw = &card->software;
    PgChipCommand *area[PGCHIP_PROGRAM_MAX] = {sw->main, sw->sub1, sw->sub2};
    const int cap[PGCHIP_PROGRAM_MAX] = {PGCHIP_MAIN_CAP, PGCHIP_SUB_CAP, PGCHIP_SUB_CAP};
    int cpu = hw_get(card, HW_CPU), same = 1;
    for (int p = 0; p < PGCHIP_PROGRAM_MAX; p++)
        same &= sw->width[p] == cpu_program_width(cpu, p);
    if (same) /* the shipped designs keep old chips past their area; leave them be */
        return;
    for (int p = 0; p < PGCHIP_PROGRAM_MAX; p++) {
        int ow = sw->width[p], nw = cpu_program_width(cpu, p);
        if (nw > cap[p])
            nw = cap[p];
        if (ow < 0 || ow > cap[p])
            ow = 0;
        PgChipCommand old[PGCHIP_MAIN_CAP * PGCHIP_MAIN_CAP];
        memcpy(old, area[p], sizeof(PgChipCommand) * (size_t)(cap[p] * cap[p]));
        memset(area[p], 0, sizeof(PgChipCommand) * (size_t)(cap[p] * cap[p]));
        for (int y = 0; y < nw; y++)
            for (int x = 0; x < nw; x++) {
                PgChipCommand *c = &area[p][y * nw + x];
                if (x < ow && y < ow)
                    *c = old[y * ow + x];
                else
                    c->b[1] = 0x40; /* FUN_089fcddc: a blank chip */
            }
        int start = sw->start[p], sx = ow ? start % ow : 0, sy = ow ? start / ow : 0;
        sw->start[p] = sx < nw && sy < nw ? sy * nw + sx : 0;
        sw->width[p] = nw;
    }
}

void hw_new_design(OkeDataBase *card, int body)
{
    body_defaults(card, body);
    sw_resize(card);
}

/* FUN_088da16c: a weapon of the same category keeps its ammo, up to what the slot
 * takes; another gets the default amount. */
static int weapon_ammo(const OkeDataBase *card, int slot, int weapon)
{
    if (weapon == 0)
        return 0;
    int old = hw_get(card, HW_WEAPON + 4 * slot), type = weapon_type(weapon);
    int ammo;
    if (weapon_category(old) == weapon_category(weapon)) {
        ammo = hw_get(card, HW_AMMO + 4 * slot);
        int cap = slot_capacity(card, slot, type);
        if (cap < ammo)
            ammo = cap;
    } else {
        ammo = default_ammo(card, slot, type);
    }
    return ammo > 0 ? ammo : 0;
}

void hw_equip(OkeDataBase *card, int item, int id)
{
    if (item == HWI_BODY) { /* FUN_088f4df4 */
        static const int keep[] = {0x250, HW_PAINT, HW_CPU, 0x248, 0x24C, HW_OUTPUT, HW_ENERGY,
                                   HW_ARMOR, HW_ADD_ARMOR, 0x254};
        int32_t saved[sizeof keep / sizeof keep[0]];
        for (unsigned i = 0; i < sizeof keep / sizeof keep[0]; i++)
            saved[i] = hw_get(card, keep[i]);
        body_defaults(card, id);
        for (unsigned i = 0; i < sizeof keep / sizeof keep[0]; i++)
            hw_set(card, keep[i], saved[i]);
        /* FUN_08a773a0 then resets the colour block's first words (FUN_08a77378). */
        memset(card->color_block, 0, sizeof(int32_t) * 6);
        card->color_block[6] = 1;
        return;
    }
    if (item == HWI_CPU) { /* FUN_088f5020 */
        hw_set(card, HW_CPU, id);
        sw_resize(card);
        return;
    }
    if (item == HWI_ARMOR) {
        hw_set(card, HW_ARMOR, id);
        return;
    }
    if (item == HWI_ADD_ARMOR) {
        hw_set(card, HW_ADD_ARMOR, id);
        return;
    }
    if (item >= HWI_WEAPON1 && item < HWI_WEAPON1 + OKE_WEAPON_SLOTS) { /* FUN_088f45c4 */
        int slot = item - HWI_WEAPON1, ammo = weapon_ammo(card, slot, id);
        hw_set(card, HW_WEAPON + 4 * slot, id);
        hw_set(card, HW_AMMO + 4 * slot, ammo);
        return;
    }
    if (item >= HWI_OPTION1 && item < HWI_OPTION1 + OKE_OPTION_SLOTS)
        hw_set(card, HW_OPTION + 4 * (item - HWI_OPTION1), id);
}
