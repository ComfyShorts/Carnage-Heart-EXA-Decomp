/* Checks the OKE Configuration hardware rules (src/game/oke_parts.c) against the
 * shipped preset designs:
 * on a new Battle Mode game's parts stock (savedata_battle_mode_new):
 *   - every preset's weapons and options are in the part lists the Hardware screen
 *     offers for their slots;
 *   - no preset is too heavy by the weight formula (FUN_089edebc / FUN_089edf40:
 *     under 130% of the payload; the arena designs go up to 129.96%);
 *   - resizing a preset's program to its own CPU leaves it unchanged;
 *   - a body change gives a loadout the new body's lists accept, and a CPU change
 *     resizes the program to the CPU's widths.
 *
 *   chexa-oke-parts-test [--disc DIR] [--elf FILE] [-v]
 *
 * Exits 77 (CTest "skipped") when the game files or the EBOOT aren't there. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gamedata.h"
#include "oke_parts.h"
#include "packfile.h"
#include "psp_shim.h"
#include "savedata.h"

static Pack g_pack;
static int g_verbose;

static int load(const char *path, OkeDataBase *out)
{
    const PackEntry *e = pack_find(&g_pack, path);
    return e && pack_read(&g_pack, e, out, sizeof *out) == (long)sizeof *out;
}

static int listed(const OkeDataBase *card, int item, int id)
{
    int ids[PART_LIST_MAX];
    int n = part_list(card, item, ids);
    for (int i = 0; i < n; i++)
        if (ids[i] == id)
            return 1;
    return 0;
}

static int check_loadout(const char *path, const OkeDataBase *card)
{
    int bad = 0, body = hw_get(card, HW_BODY);
    for (int s = 0; s < body_weapons(body) && s < OKE_WEAPON_SLOTS; s++) {
        int w = hw_get(card, HW_WEAPON + 4 * s);
        if (!listed(card, HWI_WEAPON1 + s, w)) {
            printf("FAIL: %s: weapon %d (%s) in slot %d isn't in the slot's list\n", path, w,
                   oke_weapon_name(w) ? oke_weapon_name(w) : "?", s + 1);
            bad++;
        }
    }
    for (int s = 0; s < body_options(body) && s < OKE_OPTION_SLOTS; s++)
        if (!listed(card, HWI_OPTION1 + s, hw_get(card, HW_OPTION + 4 * s))) {
            printf("FAIL: %s: option slot %d\n", path, s + 1);
            bad++;
        }
    return bad;
}

int main(int argc, char **argv)
{
    const char *disc = getenv("CHEXA_DISC"), *elf = "orig/EBOOT.elf";
    if (!disc)
        disc = "/mnt/c/psp/chexa";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--disc") && i + 1 < argc) disc = argv[++i];
        else if (!strcmp(argv[i], "--elf") && i + 1 < argc) elf = argv[++i];
        else if (!strcmp(argv[i], "-v")) g_verbose = 1;
        else {
            fprintf(stderr, "usage: %s [--disc DIR] [--elf FILE] [-v]\n", argv[0]);
            return 2;
        }
    }
    if (!psp_io_mount_disc(disc)) {
        printf("SKIP: game files not found at %s\n", disc);
        return 77;
    }
    if (!gamedata_open(elf)) {
        printf("SKIP: no EBOOT at %s\n", elf);
        return 77;
    }
    if (pack_open(&g_pack, "disc0:/PSP_GAME/USRDIR") < 0) {
        printf("FAIL: can't open the pack index\n");
        return 1;
    }
    savedata_battle_mode_new(); /* a new Battle Mode game's stock: the lists show owned parts */
    int checked = 0, failures = 0;
    for (int i = 1; i <= 100; i++) {
        char path[48];
        snprintf(path, sizeof path, "preset/arena/oke_b%02d_1.bin", i);
        static OkeDataBase card, copy;
        if (!load(path, &card))
            continue;
        checked++;
        int body = hw_get(&card, HW_BODY);
        failures += check_loadout(path, &card);
        int state = oke_weight_state(&card);
        if (g_verbose)
            printf("%s: %s, weight %d / %d\n", path, oke_body_name(body), oke_weight(&card), oke_payload(&card));
        if (state >= 3) {
            printf("FAIL: %s: weight %d too heavy for the payload %d\n", path, oke_weight(&card), oke_payload(&card));
            failures++;
        }
        copy = card;
        sw_resize(&copy);
        if (memcmp(&copy.software, &card.software, sizeof card.software)) {
            printf("FAIL: %s: resizing to its own CPU changed the program\n", path);
            failures++;
        }
        /* a body change gives a loadout the lists accept */
        copy = card;
        hw_equip(&copy, HWI_BODY, (body + 1) % 13);
        failures += check_loadout(path, &copy);
        if (hw_get(&copy, HW_CPU) != hw_get(&card, HW_CPU) || hw_get(&copy, HW_ARMOR) != hw_get(&card, HW_ARMOR)) {
            printf("FAIL: %s: the body change didn't keep the CPU and armour\n", path);
            failures++;
        }
        /* a CPU change resizes the program */
        copy = card;
        int cpu = hw_get(&card, HW_CPU) == 7 ? 1 : 7;
        hw_equip(&copy, HWI_CPU, cpu);
        if (copy.software.width[0] != cpu_program_width(cpu, 0) || copy.software.width[1] != cpu_program_width(cpu, 1)) {
            printf("FAIL: %s: CPU %d gave widths %d / %d\n", path, cpu, copy.software.width[0], copy.software.width[1]);
            failures++;
        }
    }
    pack_close(&g_pack);
    printf("%d preset designs checked, %d failure(s)\n", checked, failures);
    return checked && !failures ? 0 : 1;
}
