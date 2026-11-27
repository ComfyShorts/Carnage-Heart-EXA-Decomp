/* Fights shipped preset designs against each other with the battle simulation
 * (src/game/battle_sim.c), headless and faster than real time, and checks that the
 * interpreter stays sane and the battles end.
 *
 *   chexa-battle-test [--disc DIR] [--elf FILE] [--mine PATH] [--theirs PATH] [--all] [-v]
 *
 * --all    your side: the test designs and tutorial answers; opponents: arena 01-10
 * --arena  every arena design against every other
 * --teams  three-OKE arena teams (opponents 51-75 against 76-100), on their spots
 * Exits 77 (CTest "skipped") when the game files aren't there. Without the EBOOT
 * (--elf, default orig/EBOOT.elf) the chip speed and range scale use rough defaults. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "battle_sim.h"
#include "gamedata.h"
#include "packfile.h"
#include "psp_shim.h"

#define DT (1.0f / 60.0f)

static Pack g_pack;
static int g_trace; /* chips of side 0 to print */

static int load(const char *path, OkeDataBase *out)
{
    const PackEntry *e = pack_find(&g_pack, path);
    return e && pack_read(&g_pack, e, out, sizeof *out) == (long)sizeof *out;
}

static int fight(const char *mine, const char *theirs, int verbose, int *decided)
{
    static Battle b;
    memset(&b, 0, sizeof b);
    if (!load(mine, &b.oke[0].rec) || !load(theirs, &b.oke[1].rec))
        return 0; /* not shipped in this version: skip */
    battle_sim_start(&b, 1);
    int steps = 0;
    while (b.result < 0 && steps < 60 * 200) {
        static PgChipHistory hist;
        int before = hist.count;
        if (g_trace) {
            /* The same step, with a history attached to side 0 for printing. */
            b.history[0] = &hist;
        }
        battle_sim_step(&b, DT);
        steps++;
        for (int k = before; g_trace > 0 && k < hist.count; k++, g_trace--) {
            const PgChipHistoryEntry *e = &hist.entries[k];
            const PgChipCommand *c = &oke_program_chips(&b.oke[0].rec, e->program)[e->position];
            printf("  %6.2f s  prog %d pos %3d  %-26s %02x %02x %02x %02x %02x %02x -> %d\n", b.time, e->program,
                   e->position, pgchip_type_name(pgchip_type(c)), c->b[2], c->b[3], c->b[4], c->b[5], c->b[6],
                   c->b[7], e->result);
        }
        for (int i = 0; i < 2; i++) {
            const PgChipVm *vm = &b.oke[i].vm;
            if (vm->depth < 0 || vm->depth > PGCHIP_STACK_MAX || vm->current < 0 || vm->current > 2) {
                printf("FAIL %s vs %s: interpreter state broken at %.2f s\n", mine, theirs, b.time);
                return -1;
            }
        }
    }
    if (b.result < 0) {
        printf("FAIL %s vs %s: no result after %d steps\n", mine, theirs, steps);
        return -1;
    }
    static const char *k_res[3] = {"win", "lose", "draw"};
    if (b.oke[0].hp == 0 || b.oke[1].hp == 0)
        (*decided)++;
    if (verbose)
        printf("%-34s vs %-28s %5.1f s  %-4s  HP %3d/%3d  shots %3d/%3d  hits %3d/%3d\n", mine, theirs, b.time,
               k_res[b.result], oke_hp_percent(&b.oke[0]), oke_hp_percent(&b.oke[1]), b.oke[0].shots_fired, b.oke[1].shots_fired,
               b.oke[0].hits, b.oke[1].hits);
    return 1;
}

/* An arena opponent's team (CBattleSettingInfo in the EBOOT: count +4, records at
 * +0x14 + 0x40 n, spots at +0xD4 + 4 n) as one side. */
#define ARENA_RECORDS 0x0044DC10u
#define ARENA_STRIDE 0x3C0u

static int load_team(Battle *b, int side, int opponent)
{
    uint32_t rec = ARENA_RECORDS + (uint32_t)opponent * ARENA_STRIDE;
    int n = (int)gamedata_word(rec + 4, 1);
    for (int m = 0; m < n && m < TEAM_SIZE; m++) {
        const char *path = gamedata_string(rec + 0x14u + 0x40u * (uint32_t)m);
        Oke *o = &b->oke[b->count];
        if (!path || !load(path, &o->rec))
            return 0;
        o->side = side;
        o->member = m;
        o->spot = (int)gamedata_word(rec + 0xD4u + 4u * (uint32_t)m, (uint32_t)-1);
        b->count++;
    }
    return n;
}

static int fight_teams(int mine, int theirs, int verbose, int *decided)
{
    static Battle b;
    memset(&b, 0, sizeof b);
    if (!gamedata_available() || !load_team(&b, 0, mine) || !load_team(&b, 1, theirs))
        return 0;
    battle_sim_start(&b, 1);
    int steps = 0;
    while (b.result < 0 && steps < 60 * 200) {
        battle_sim_step(&b, DT);
        steps++;
    }
    int a0 = battle_team_alive(&b, 0), a1 = battle_team_alive(&b, 1);
    if (b.result < 0 || (b.time < battle_time_limit(&b) - 0.5f && a0 > 0 && a1 > 0)) {
        printf("FAIL team %d vs %d: no proper end (result %d after %.1f s, %d/%d left)\n", mine + 1, theirs + 1,
               b.result, b.time, a0, a1);
        return -1;
    }
    if (a0 == 0 || a1 == 0)
        (*decided)++;
    static const char *k_res[3] = {"win", "lose", "draw"};
    if (verbose)
        printf("team %3d vs team %3d  %5.1f s  %-4s  left %d/%d  durability %3d/%3d\n", mine + 1, theirs + 1, b.time,
               k_res[b.result], a0, a1, battle_team_percent(&b, 0), battle_team_percent(&b, 1));
    return 1;
}

int main(int argc, char **argv)
{
    const char *disc = getenv("CHEXA_DISC"), *elf = "orig/EBOOT.elf";
    const char *mine = "preset/oke_t01_1.bin", *theirs = "preset/arena/oke_b01_1.bin";
    int all = 0, verbose = 0;
    if (!disc)
        disc = "/mnt/c/psp/chexa";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--disc") && i + 1 < argc) disc = argv[++i];
        else if (!strcmp(argv[i], "--elf") && i + 1 < argc) elf = argv[++i];
        else if (!strcmp(argv[i], "--mine") && i + 1 < argc) mine = argv[++i];
        else if (!strcmp(argv[i], "--theirs") && i + 1 < argc) theirs = argv[++i];
        else if (!strcmp(argv[i], "--all")) all = 1;
        else if (!strcmp(argv[i], "--arena")) all = 2;
        else if (!strcmp(argv[i], "--teams")) all = 3;
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc) g_trace = atoi(argv[++i]);
        else {
            fprintf(stderr, "usage: %s [--disc DIR] [--elf FILE] [--mine PATH] [--theirs PATH] [--all] [--arena] [--teams] [-v] [--trace N]\n",
                    argv[0]);
            return 2;
        }
    }
    if (!psp_io_mount_disc(disc)) {
        printf("SKIP: game files not found at %s\n", disc);
        return 77;
    }
    if (pack_open(&g_pack, "disc0:/PSP_GAME/USRDIR") < 0) {
        printf("FAIL: can't open the pack index\n");
        return 1;
    }
    gamedata_open(elf);
    int fought = 0, failures = 0, decided = 0;
    if (all == 3) {
        for (int i = 50; i < 75; i++) {
            int r = fight_teams(i, i + 25, verbose, &decided);
            fought += r > 0;
            failures += r < 0;
        }
    } else if (all == 2) {
        char a[48], t[48];
        for (int i = 1; i <= 50; i++)
            for (int j = 1; j <= 50; j++) {
                snprintf(a, sizeof a, "preset/arena/oke_b%02d_1.bin", i);
                snprintf(t, sizeof t, "preset/arena/oke_b%02d_1.bin", j);
                int r = i == j ? 0 : fight(a, t, verbose, &decided);
                fought += r > 0;
                failures += r < 0;
            }
    } else if (all) {
        char a[48], t[48];
        for (int i = -2; i < 30; i++) {
            if (i < 0)
                snprintf(a, sizeof a, "preset/oke_t01_%d.bin", i + 3);
            else
                snprintf(a, sizeof a, "preset/lecture/oke_l%02d_a.bin", i);
            for (int j = 1; j <= 10; j++) {
                snprintf(t, sizeof t, "preset/arena/oke_b%02d_1.bin", j);
                int r = fight(a, t, verbose, &decided);
                fought += r > 0;
                failures += r < 0;
            }
        }
    } else {
        int r = fight(mine, theirs, 1, &decided);
        fought += r > 0;
        failures += r < 0;
    }
    pack_close(&g_pack);
    printf("%d battles, %d ended by destruction, %d failures\n", fought, decided, failures);
    printf("%s\n", failures || !fought ? "FAILED" : "OK");
    return failures || !fought ? 1 : 0;
}
