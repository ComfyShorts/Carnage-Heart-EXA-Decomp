/* Checks the chip editor's rules (src/game/pgchip_edit.c) and that an edited program
 * survives a save:
 *   - on every shipped design, the game's NormalizeArea rules (Returns where something
 *     points into an empty cell, bit 6 on chips with an incoming arrow, bit 7 on cells
 *     without an error) change nothing but bit 7 on blank cells and Returns, which a
 *     few designs were shipped without; a second pass changes nothing;
 *   - on every chip of every shipped design, each parameter its type's sub-menus list
 *     can be read, and writing back what was read changes nothing (direction and
 *     elevation too); the range editor never leaves the game's bounds and changing a
 *     range's shape gives a valid range;
 *   - a new game holds each type's default chip, and placing a chip remembers it;
 *   - a design is loaded into a card, edited (chips placed, exits turned, a chip
 *     removed, the start moved, a block copied, parameters and a range set), saved as Battle Data, the saves
 *     cleared, reloaded, and its software is byte for byte the edited one;
 *   - a second card is cleared (Delete All) and given a two-chip program by hand, saved
 *     and reloaded with the first, and fought in the battle simulation: the
 *     interpreter runs exactly those two chips.
 *
 *   chexa-chip-edit-test [--disc DIR] [--elf FILE] [--saves DIR] [-v]
 *
 * The saves (which hold the game's ICON0.PNG / PARAM.SFO and shipped designs) go to
 * --saves DIR, emptied first, or else to a fresh folder in $TMPDIR (or /tmp) that is
 * removed afterwards, so a run from the source tree leaves nothing in it.
 *
 * Exits 77 (CTest "skipped") when the game files or the EBOOT aren't there. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    int params, param_bad, angles, angle_bad;
} Tally;

/* Each listed parameter read and written back. */
static void check_params(Tally *t, const char *path, const PgChipCommand *c)
{
    int subs[PGCHIP_SUB_ITEMS], n = pgchip_sub_menus(pgchip_type(c), subs);
    for (int i = 0; i < n; i++) {
        int sub = subs[i];
        if (sub == PGCHIP_SUB_CONFIRM || sub == PGCHIP_SUB_POLAR || sub == PGCHIP_SUB_CARTESIAN)
            continue;
        if (sub == PGCHIP_SUB_DIRECTION || sub == PGCHIP_SUB_ELEVATION) {
            PgChipCommand d = *c;
            int a = 0;
            bool ok = pgchip_angle_get(&d, sub == PGCHIP_SUB_ELEVATION, &a) &&
                      pgchip_angle_set(&d, sub == PGCHIP_SUB_ELEVATION, a) && !memcmp(&d, c, sizeof d);
            t->angles++;
            if (!ok && ++t->angle_bad <= 3)
                printf("      %s type 0x%02X: angle %d doesn't round-trip\n", path, pgchip_type(c), sub);
            continue;
        }
        int32_t v;
        PgChipCommand d = *c;
        bool ok = pgchip_param_get(&d, sub, &v);
        if (ok && sub != PGCHIP_SUB_RANGE) /* the shape only the range editor writes */
            ok = pgchip_param_set(&d, sub, v) && !memcmp(&d, c, sizeof d);
        t->params++;
        if (!ok && ++t->param_bad <= 5)
            printf("      %s type 0x%02X sub 0x%02X: %02X %02X %02X %02X %02X %02X\n", path, pgchip_type(c), sub,
                   c->b[2], c->b[3], c->b[4], c->b[5], c->b[6], c->b[7]);
    }
}

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
            if (pgchip_is_chip(a))
                check_params(t, path, a);
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

/* ---- the range editor ---- */

static bool range_valid(const uint8_t r[4])
{
    if (r[0] & 1)
        return (r[0] >> 1) <= 0x6E && r[1] <= 0x6E && abs((int8_t)r[2]) <= 0x6E && abs((int8_t)r[3]) <= 0x6E;
    return (r[0] >> 1) <= r[2] && r[1] <= 0xB4 && r[2] <= 0x6E && r[3] < 0xB4;
}

static void check_range_editor(void)
{
    uint8_t r[4] = {0, 45, 20, 0}; /* a sector: inner 0, +/-90 degrees, out to step 20, ahead */
    int bad = 0, changed = 0;
    srand(7);
    for (int i = 0; i < 20000; i++) {
        if (i % 2500 == 0)
            pgchip_range_set_shape(r, (i / 2500) & 1);
        int dx = rand() % 3 - 1, dy = rand() % 3 - 1;
        changed += pgchip_range_edit(r, dx, dy, rand() & 1, rand() % 8 == 0);
        bad += !range_valid(r);
    }
    CHECK(bad == 0 && changed > 1000, "20000 range edits stayed in bounds (%d changed, %d out)", changed, bad);
    uint8_t s[4] = {0, 45, 20, 0};
    pgchip_range_set_shape(s, 1);
    CHECK((s[0] & 1) && range_valid(s) && (int8_t)s[3] > 0 && s[2] == 0,
          "a sector ahead becomes a rectangle ahead (%02X %02X %02X %02X)", s[0], s[1], s[2], s[3]);
    pgchip_range_set_shape(s, 0);
    CHECK(!(s[0] & 1) && range_valid(s) && s[3] == 0, "and back to a sector ahead (%02X %02X %02X %02X)", s[0], s[1],
          s[2], s[3]);
    CHECK(pgchip_range_distance(0x6E) > pgchip_range_distance(20), "the farthest step is the farthest");
    CHECK(pgchip_angle_edit(0, false, 200, -1, true, false) == 35800, "direction wraps below 0");
    CHECK(pgchip_angle_edit(400, false, 200, 1, true, true) == 9000, "L goes on to the quarter");
    CHECK(pgchip_angle_edit(9000, true, 200, 1, false, false) == 9000, "a held elevation stops at 90");
    CHECK(pgchip_angle_edit(9000, true, 200, 1, true, false) == -9000, "a fresh press goes round");
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
    {
        PgChipCommand *j = pgchip_at(&main, x, y);
        pgchip_range_set_shape(j->b + 2, 1);
        CHECK(pgchip_range_edit(j->b + 2, 1, 1, false, false), "moved the judge's range");
        uint8_t keep[4];
        memcpy(keep, j->b + 2, 4);
        int32_t v = -1;
        CHECK(pgchip_param_get(j, PGCHIP_SUB_RANGE, &v) && v == 1, "its range is a rectangle now");
        memcpy(j->b + 2, keep, 4);
    }
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
        PgChipCommand move = *savedata_chip_default(0x1E);
        int32_t v = -1;
        CHECK(pgchip_param_set(&move, 0x3A, 0) && pgchip_param_get(&move, PGCHIP_SUB_PROCESSING, &v) && v == 1,
              "an endless move passes through");
        CHECK(pgchip_place(sw, 0, ax, ay, &move), "placed a move chip");
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
    const char *disc = getenv("CHEXA_DISC"), *elf = "orig/EBOOT.elf", *saves = NULL;
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
    printf("%d parameters and %d angles on the shipped chips\n", t.params, t.angles);
    CHECK(t.params > 1000 && t.param_bad == 0, "every listed parameter reads and writes back (%d bad)", t.param_bad);
    CHECK(t.angles > 0 && t.angle_bad == 0, "every direction / elevation writes back (%d bad)", t.angle_bad);
    check_range_editor();

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

    char cmd[600], tmp[512];
    bool temp_saves = !saves;
    if (temp_saves) {
        const char *dir = getenv("TMPDIR");
        snprintf(tmp, sizeof tmp, "%s/chexa-chip-edit-XXXXXX", dir && *dir ? dir : "/tmp");
        if (!mkdtemp(tmp)) {
            printf("FAIL: can't make a temporary folder for the saves\n");
            return 1;
        }
        saves = tmp;
    } else {
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", saves);
        if (system(cmd) != 0)
            return 1;
    }
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
    if (temp_saves) {
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", saves);
        if (system(cmd) != 0)
            printf("note: couldn't remove %s\n", saves);
    }
    printf(g_failures ? "%d failure(s)\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
