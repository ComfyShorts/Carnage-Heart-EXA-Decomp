/* Checks the chip editor's rules (src/game/pgchip_edit.c) and that an edited program
 * survives a save:
 *   - on every shipped design, the game's NormalizeArea rules (Returns where something
 *     points into an empty cell, bit 6 on chips with an incoming arrow, bit 7 on cells
 *     without an error) change nothing but bit 7 on blank cells and Returns, which a
 *     few designs were shipped without; a second pass changes nothing;
 *   - a new game holds each type's default chip, and placing a chip remembers it;
 *   - a design is loaded into a card, edited (chips placed, exits turned, a chip
 *     removed, the start moved, a block copied), saved as Battle Data, the saves
 *     cleared, reloaded, and its software is byte for byte the edited one;
 *   - a second card is cleared (Delete All) and given a two-chip program by hand, saved
 *     and reloaded with the first, and fought in the battle simulation: the
 *     interpreter runs exactly those two chips.
 *
 *   chexa-chip-edit-test [--disc DIR] [--elf FILE] [--saves DIR] [-v]
 *
 * Exits 77 (CTest "skipped") when the game files or the EBOOT aren't there. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "battle_sim.h"
#include "gamedata.h"
#include "packfile.h"
#include "pgchip.h"
#include "pgchip_edit.h"
#include "psp_shim.h"
#include "savedata.h"

static Pack g_pack;
static int g_verbose, g_failures;

#define CHECK(cond, ...)                         \
    do {                                         \
        int ok_ = (cond);                        \
        if (!ok_)                                \
            g_failures++;                        \
        if (!ok_ || g_verbose) {                 \
            printf(ok_ ? "ok    " : "FAIL  ");   \
            printf(__VA_ARGS__);                 \
            printf("\n");                        \
        }                                        \
    } while (0)

static int load(const char *path, OkeDataBase *out)
{
    const PackEntry *e = pack_find(&g_pack, path);
    return e && pack_read(&g_pack, e, out, sizeof *out) == (long)sizeof *out;
}

/* ---- the rules on the shipped designs ---- */

typedef struct Tally {
    int designs, programs, unchanged, bit7_only, other;
} Tally;

static int check_design(void *user, const char *path, const PackEntry *e)
{
    Tally *t = user;
    if (strncmp(path, "preset/", 7) || e->size != OKE_RECORD_SIZE)
        return 0;
    static OkeDataBase card;
    if (pack_read(&g_pack, e, &card, sizeof card) != (long)sizeof card)
        return 0;
    t->designs++;
    SoftwareDataBase sw = card.software;
    pgchip_normalize(&sw);
    for (int p = 0; p < PGCHIP_PROGRAM_MAX; p++) {
        PgChipArea before = pgchip_area(&card.software, p), after = pgchip_area(&sw, p);
        if (!before.width)
            continue;
        t->programs++;
        int bit7 = 0, other = 0;
        for (int i = 0; i < before.width * before.width; i++) {
            const PgChipCommand *a = &before.chips[i], *b = &after.chips[i];
            if (!memcmp(a, b, sizeof *a))
                continue;
            PgChipCommand fixed = *a;
            fixed.b[0] |= 0x80;
            if (!pgchip_is_chip(a) && !memcmp(&fixed, b, sizeof fixed))
                bit7++;
            else {
                other++;
                if (other <= 3)
                    printf("      %s program %d cell %d: %02X %02X -> %02X %02X\n", path, p, i, a->b[0], a->b[1],
                           b->b[0], b->b[1]);
            }
        }
        if (other)
            t->other++;
        else if (bit7)
            t->bit7_only++;
        else
            t->unchanged++;
    }
    SoftwareDataBase again = sw;
    pgchip_normalize(&again);
    CHECK(!memcmp(&again, &sw, sizeof sw), "%s: normalizing twice changes nothing", path);
    return 0;
}

/* ---- an edit, saved and loaded ---- */

static int find_free(const PgChipArea *a, int *x, int *y)
{
    for (int yy = 1; yy < a->width; yy++)
        for (int xx = 0; xx < a->width; xx++)
            if (!pgchip_valid_type(pgchip_type(pgchip_at(a, xx, yy)))) {
                *x = xx;
                *y = yy;
                return 1;
            }
    return 0;
}

static int count_chips(const PgChipArea *a)
{
    int n = 0;
    for (int i = 0; i < a->width * a->width; i++)
        n += pgchip_is_chip(&a->chips[i]);
    return n;
}

static void edit_program(OkeDataBase *card)
{
    SoftwareDataBase *sw = &card->software;
    pgchip_normalize(sw);
    PgChipArea main = pgchip_area(sw, 0);
    int before = count_chips(&main), x = 0, y = 0;

    /* A judge chip from the defaults, its parameters changed as the sub-menu would. */
    PgChipCommand judge = *savedata_chip_default(0x0B);
    CHECK(pgchip_kind(0x0B) == 1, "type 0x0B is a judge");
    judge.b[4] = 0x40;
    CHECK(find_free(&main, &x, &y), "a free cell in the main area");
    CHECK(pgchip_place(sw, 0, x, y, &judge), "placed a judge at (%d, %d)", x, y);
    savedata_set_chip_default(&judge);
    CHECK(pgchip_is_chip(pgchip_at(&main, x, y)) && count_chips(&main) == before + 1, "it's there");
    CHECK(pgchip_exit_dir(pgchip_at(&main, x, y)) == 0 && pgchip_branch_dir(pgchip_at(&main, x, y)) == 4,
          "with the default exits up / down");
    CHECK(pgchip_turn_exit(sw, 0, x, y, false, 1), "R turned its exit");
    CHECK(pgchip_exit_dir(pgchip_at(&main, x, y)) == 1, "to up-right");
    CHECK(pgchip_set_exit(sw, 0, x, y, true, 6), "set its branch to the left");
    CHECK(!pgchip_set_exit(sw, 0, x, y, true, 1), "the branch can't take the exit's direction");
    /* Whatever it points into that was empty now holds a Return. */
    int tx = x + pgchip_dir_step[6][0], ty = y + pgchip_dir_step[6][1];
    if (pgchip_in_area(&main, tx, ty))
        CHECK(pgchip_valid_type(pgchip_type(pgchip_at(&main, tx, ty))), "the branch target isn't blank");

    /* An action chip, then removed again. */
    int ax, ay;
    if (find_free(&main, &ax, &ay)) {
        CHECK(pgchip_place(sw, 0, ax, ay, savedata_chip_default(0x1E)), "placed a move chip");
        pgchip_remove(sw, 0, ax, ay);
        CHECK(!pgchip_is_chip(pgchip_at(&main, ax, ay)), "and removed it");
    }

    /* The start one column over and back is a no-op for the cells it leaves. */
    if (pgchip_move_start(sw, 0, 1) || pgchip_move_start(sw, 0, -1))
        CHECK(1, "moved the start");

    /* A 2 x 2 block copied to the bottom-right corner. */
    PgChipClip clip;
    pgchip_clip_take(&clip, &main, 0, 0, 1, 1);
    pgchip_clip_paste(sw, 0, &clip, main.width - 2, main.width - 2);
    for (int i = 0; i < 4; i++) {
        const PgChipCommand *s = &clip.chips[i];
        const PgChipCommand *d = pgchip_at(&main, main.width - 2 + i % 2, main.width - 2 + i / 2);
        if (pgchip_is_chip(s))
            CHECK(pgchip_type(d) == pgchip_type(s) && d->b[1] == s->b[1] && !memcmp(d->b + 2, s->b + 2, 6),
                  "pasted chip %d matches", i);
    }
    SoftwareDataBase again = *sw;
    pgchip_normalize(&again);
    CHECK(!memcmp(&again, sw, sizeof again), "the edited program is normalized");
}

/* Delete All, the start moved to column 0, a Move chip on it turned to the right
 * into a NOP that leaves the area upwards (which restarts the program). */
static void write_small_program(OkeDataBase *card)
{
    SoftwareDataBase *sw = &card->software;
    pgchip_clear(sw);
    while (pgchip_move_start(sw, 0, -1))
        ;
    CHECK(sw->start[0] == 0, "start moved to column 0");
    CHECK(pgchip_place(sw, 0, 0, 0, savedata_chip_default(0x1E)), "a Move chip on the start");
    CHECK(pgchip_set_exit(sw, 0, 0, 0, false, 2), "turned to the right");
    CHECK(pgchip_place(sw, 0, 1, 0, savedata_chip_default(0x01)), "a NOP beside it");
    PgChipArea a = pgchip_area(sw, 0);
    CHECK(pgchip_error(&a, 0, pgchip_has_subs(sw), 1, 0) == PGCHIP_ERR_NONE, "the NOP is connected");
    int chips = 0;
    for (int p = 0; p < PGCHIP_PROGRAM_MAX; p++) {
        PgChipArea q = pgchip_area(sw, p);
        for (int i = 0; i < q.width * q.width; i++)
            chips += pgchip_is_chip(&q.chips[i]);
    }
    CHECK(chips == 2, "two chips in the whole block (%d)", chips);
}

/* The reloaded card against an arena design for 20 seconds. */
static void fight_small_program(const OkeDataBase *card)
{
    static Battle b;
    static PgChipHistory hist;
    memset(&b, 0, sizeof b);
    memset(&hist, 0, sizeof hist);
    b.oke[0].rec = *card;
    if (!load("preset/arena/oke_b02_1.bin", &b.oke[1].rec)) {
        CHECK(0, "loaded an opponent");
        return;
    }
    battle_sim_start(&b, 1);
    b.history[0] = &hist;
    for (int i = 0; i < 60 * 20 && b.result < 0; i++)
        battle_sim_step(&b, 1.0f / 60.0f);
    int moves = 0, nops = 0, other = 0;
    for (int k = 0; k < hist.count; k++) {
        const PgChipHistoryEntry *e = &hist.entries[k];
        if (e->program == 0 && e->position == 0)
            moves++;
        else if (e->program == 0 && e->position == 1)
            nops++;
        else
            other++;
    }
    printf("the edited program in battle: %d chip results, Move %d, NOP %d, other %d\n", hist.count, moves, nops,
           other);
    CHECK(moves > 0 && nops > 0 && other == 0, "the interpreter ran the two placed chips and nothing else");
}

int main(int argc, char **argv)
{
    const char *disc = getenv("CHEXA_DISC"), *elf = "orig/EBOOT.elf", *saves = "chip-edit-test-saves";
    if (!disc)
        disc = "/mnt/c/psp/chexa";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--disc") && i + 1 < argc) disc = argv[++i];
        else if (!strcmp(argv[i], "--elf") && i + 1 < argc) elf = argv[++i];
        else if (!strcmp(argv[i], "--saves") && i + 1 < argc) saves = argv[++i];
        else if (!strcmp(argv[i], "-v")) g_verbose = 1;
        else {
            fprintf(stderr, "usage: %s [--disc DIR] [--elf FILE] [--saves DIR] [-v]\n", argv[0]);
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

    Tally t = {0};
    pack_foreach(&g_pack, check_design, &t);
    printf("%d designs, %d programs: %d unchanged, %d only gain bit 7 on blank cells / Returns, %d other\n",
           t.designs, t.programs, t.unchanged, t.bit7_only, t.other);
    CHECK(t.programs > 0 && t.other == 0, "the normalize rules match every shipped program");

    /* Defaults in a new game. */
    savedata_new_game();
    int same = 1;
    for (int ty = 1; ty < PGCHIP_TYPE_END; ty++) {
        PgChipCommand c;
        pgchip_default(&c, ty);
        same &= !memcmp(&c, savedata_chip_default(ty), sizeof c);
    }
    CHECK(same, "a new game holds each type's default chip");
    CHECK(savedata_chip_page() == 4, "the chip list opens on page 4, every chip");

    /* Load a design with sub-programs into card 1 and edit it. */
    OkeDataBase *card = &g_save.db.card[0];
    const char *path = "preset/arena/oke_b01_1.bin";
    CHECK(load(path, card), "loaded %s", path);
    edit_program(card);
    static OkeDataBase edited, small;
    edited = *card;
    OkeDataBase *card2 = &g_save.db.card[1];
    CHECK(load("preset/arena/oke_b03_1.bin", card2), "loaded a second design");
    write_small_program(card2);
    small = *card2;
    PgChipCommand judge = *savedata_chip_default(0x0B);

    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", saves);
    if (system(cmd) != 0)
        return 1;
    psp_savedata_set_root(saves);
    savedata_save_start("CHIPEDIT.CHE");
    CHECK(savedata_run(savedata_io_update) == 0, "saved CHIPEDIT.CHE");
    savedata_new_game();
    CHECK(memcmp(&g_save.db.card[0], &edited, sizeof edited) != 0, "cleared before loading");
    savedata_load_start("CHIPEDIT.CHE");
    CHECK(savedata_run(savedata_io_update) == 0, "loaded CHIPEDIT.CHE");
    CHECK(!memcmp(&g_save.db.card[0].software, &edited.software, sizeof edited.software),
          "the edited software came back unchanged");
    CHECK(!memcmp(&g_save.db.card[0], &edited, sizeof edited), "so did the rest of the card");
    CHECK(!memcmp(savedata_chip_default(0x0B), &judge, sizeof judge), "and the remembered judge chip");
    CHECK(!memcmp(&g_save.db.card[1], &small, sizeof small), "the cleared and rewritten card came back unchanged");

    /* The interpreter takes it. */
    PgChipVm vm;
    pgchip_setup(&vm, &g_save.db.card[0], 4);
    CHECK(vm.prog[0].width == edited.software.width[0] && vm.prog[0].chips != NULL,
          "the interpreter set up the edited main program (width %d)", vm.prog[0].width);
    fight_small_program(&g_save.db.card[1]);

    pack_close(&g_pack);
    printf(g_failures ? "%d failure(s)\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
