/* Checks the battle scene's files against the user's EBOOT and disc:
 *   - the sky of each map 0-24 (battle_sky_path, the game's table at 0x08c7a598): the
 *     maps without one (02, 04, 05, 08-14, 24) give none, 17 and 23 use map 16's, and
 *     every sky named is on the disc;
 *   - every OKE body loads with each of its paints (okemodel_load), including the six
 *     whose battle/ models are only in their file sets (abi, agr, gaz, gs, kei, nh), and
 *     the sets are closed again.
 *
 *   chexa-scene-files-test [--disc DIR] [--elf FILE] [-v]
 *
 * Exits 77 (CTest "skipped") when the game files or the EBOOT aren't there. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "battle.h"
#include "fileio.h"
#include "gamedata.h"
#include "heap.h"
#include "okemodel.h"
#include "psp_shim.h"

static int failures;
static int verbose;

#define CHECK(cond, ...)                      \
    do {                                      \
        if (!(cond)) {                        \
            failures++;                       \
            printf("FAIL  ");                 \
            printf(__VA_ARGS__);              \
            printf("\n");                     \
        } else if (verbose) {                 \
            printf("ok    ");                 \
            printf(__VA_ARGS__);              \
            printf("\n");                     \
        }                                     \
    } while (0)

static void drop_log(void *u, int c, SDL_LogPriority p, const char *m)
{
    (void)u; (void)c; (void)p; (void)m;
}

int main(int argc, char **argv)
{
    const char *disc = getenv("CHEXA_DISC"), *elf = "orig/EBOOT.elf";
    if (!disc)
        disc = "/mnt/c/psp/chexa";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--disc") && i + 1 < argc) disc = argv[++i];
        else if (!strcmp(argv[i], "--elf") && i + 1 < argc) elf = argv[++i];
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else {
            fprintf(stderr, "usage: %s [--disc DIR] [--elf FILE] [-v]\n", argv[0]);
            return 2;
        }
    }
    if (!verbose)
        SDL_SetLogOutputFunction(drop_log, NULL);
    if (!psp_io_mount_disc(disc)) {
        printf("SKIP: game files not found at %s\n", disc);
        return 77;
    }
    if (!gamedata_open(elf)) {
        printf("SKIP: no EBOOT at %s\n", elf);
        return 77;
    }
    static uint8_t heap_mem[24 << 20];
    heap_init(&g_default_heap, heap_mem, sizeof heap_mem, "default heap");
    fileio_init();
    if (!packindex_read("chexa.idx")) {
        printf("FAIL: can't read the pack index\n");
        return 1;
    }

    /* The skies. */
    static const unsigned char k_none[25] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1,
                                             1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    int skies = 0;
    for (int map = 0; map < 25; map++) {
        const char *sky = battle_sky_path(map);
        if (k_none[map]) {
            CHECK(!sky, "map %02d has no sky", map);
            continue;
        }
        char own[32];
        snprintf(own, sizeof own, "land/map%02d/map%02d_sky.gmo", map, map);
        const char *want = map == 17 || map == 23 ? "land/map16/map16_sky.gmo" : own;
        CHECK(sky && !strcmp(sky, want), "map %02d: sky %s (%s)", map, want, sky ? sky : "none");
        CHECK(sky && fileio_file_size(sky) > 0, "map %02d: %s is on the disc", map, sky ? sky : "?");
        skies += sky != NULL;
    }
    CHECK(skies == 14 && !battle_sky_path(-1) && !battle_sky_path(25), "14 maps with a sky, none out of range");

    /* The OKE bodies, every paint. */
    uint32_t free_before = heap_largest_free(&g_default_heap);
    int bodies = 0, loads = 0;
    for (int id = 0; id <= OKE_ID_MAX; id++) {
        const char *all = okemodel_set_name(id, 4);
        if (!all)
            continue;
        bodies++;
        for (int paint = 0; paint < 4; paint++) {
            const char *set = okemodel_set_name(id, paint);
            if (!set || !packindex_find_set(set))
                continue; /* not every body has four paints */
            OkeModel *m = okemodel_load(id, paint);
            CHECK(m && okemodel_pose_count(m) > 0, "OKE %d (%s) paint %d loads", id, all, paint);
            loads += m != NULL;
            okemodel_free(m);
        }
    }
    CHECK(bodies == OKE_ID_MAX + 1 && loads > bodies, "%d bodies, %d body + paint loads", bodies, loads);
    CHECK(heap_largest_free(&g_default_heap) == free_before, "the OKE file sets are closed after loading");

    printf("%s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
