/* Checks Story Mode against the user's EBOOT and disc:
 *   - the step list (storydb.c): 85 steps, 20 chapter cards whose pictures are on the
 *     disc, 41 scenes, 13 lessons and 11 missions;
 *   - every step's script is on the disc and reads to its end marker with the scene
 *     player's argument table (scene_dry_run);
 *   - a new story (savedata_story_new + loadPreset) has its preset cards and team;
 *   - the lessons (lesson.c): the cards' table, the state ops and tests, and the
 *     day ending when the last of its cards is done (ReportEnd);
 *   - a scene voice from voice.dat decodes (sceAtrac, as voice.c plays it) to mono
 *     samples that aren't silent (skipped without libavcodec).
 *
 *   chexa-story-test [--disc DIR] [--elf FILE] [-v]
 *
 * Exits 77 (CTest "skipped") when the game files or the EBOOT aren't there. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "fileio.h"
#include "gamedata.h"
#include "heap.h"
#include "lesson.h"
#include "psp_imports.h"
#include "psp_shim.h"
#include "savedata.h"
#include "scene.h"
#include "storydb.h"

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

    int titles = 0, scenes = 0, lessons = 0, missions = 0, commands = 0;
    for (int step = 0; step < STORY_STEPS; step++) {
        const char *progress = storydb_progress(step, true);
        CHECK(progress && !strncmp(progress, "Progress :", 10), "step %d has a progress title", step);
        if (storydb_title(step)) {
            titles++;
            const char *pic = storydb_chapter_picture(step);
            CHECK(pic && fileio_file_size(pic) > 0, "step %d: chapter picture %s on the disc", step,
                  pic ? pic : "(none)");
            continue;
        }
        const char *script = storydb_script(step), *id = storydb_script_id(step);
        if (storydb_briefing(step)) {
            if (storydb_lecture_part(step))
                lessons++;
            else
                missions++;
        } else {
            scenes++;
        }
        int n = script && id ? scene_dry_run(script, id) : -1;
        CHECK(n > 0, "step %d: %s / %s reads to its end (%d commands)", step, script ? script : "?",
              id ? id : "?", n);
        if (n > 0)
            commands += n;
    }
    CHECK(titles == 20 && scenes == 41 && lessons == 13 && missions == 11,
          "20 chapter cards, 41 scenes, 13 lessons, 11 missions (%d, %d, %d, %d)", titles, scenes, lessons,
          missions);
    CHECK(!storydb_title(-1) && storydb_script(0x56) == NULL, "out-of-range steps give nothing");

    savedata_new_game();
    savedata_story_new();
    CHECK(savedata_story_step() == STORY_STEP_NONE && savedata_story_battle() == -1, "a new story starts before step 0");
    CHECK(savedata_menu_limit(0) == 2 && savedata_menu_limit(6) == 0, "a new story's menu limits");
    CHECK(savedata_stock(STOCK_WEAPON, 1) == 0 && savedata_stock(STOCK_BODY, 0x0E) == 3, "a new story's stock");
    CHECK(savedata_story_preset(), "loadPreset's files are on the disc");
    CHECK(savedata_card_used(0) && savedata_card_used(1) && !savedata_card_used(2), "cards 1 and 2 hold the presets");
    CHECK(savedata_team_status(0) != 0, "team card 1 holds the preset team");

    /* Lessons: day one (tutorial_a) is cards 0-3, lessons 2-5. */
    CHECK(lesson_card_of(2) == 0 && lesson_card_of(10) == 8 && lesson_card_of(11) == 11 && lesson_card_of(1) == -1,
          "lesson numbers to cards (2 -> 0, 10 -> 8, 11 -> 11)");
    CHECK(!strcmp(lesson_title(0), "OKE Overview") && !strcmp(lesson_title(1), "OKE Controls"), "lesson titles");
    savedata_set_story_step(2);
    lesson_var(6, 2, 0x10); /* offered, as tutorial01a does */
    CHECK(lesson_listed(0) && !lesson_done(0) && lesson_test(1, 1, 0) == 1 && lesson_test(2, 1, 0) == 2 && lesson_test(7, 1, 0x10) == 2,
          "a lesson offered, not done, and the op 0x12 tests on it (slot n - 1, as FUN_088e41e4 reads)");
    char text[160];
    lesson_var(6, 2, 1);
    CHECK(lesson_done(0) && lesson_completed_text(2, 1, text, sizeof text) && strstr(text, "OKE Overview"),
          "a lesson done, and its notice (%s)", text);
    lesson_report_end("tutorial02b");
    CHECK(!lesson_take_day_done() && savedata_lesson_current() == -1, "one card done doesn't end the day");
    for (int n = 3; n <= 5; n++)
        lesson_var(6, n, 0x11);
    lesson_report_end("tutorial05_succeed");
    CHECK(lesson_take_day_done(), "the day's last card ends it (FUN_089348f0)");

    /* The first voice of scenario01. */
    const char *voice = "voice/til/til0023380.at3";
    int32_t vsize = fileio_file_size(voice);
    uint8_t *vbuf = vsize > 0 ? malloc((size_t)vsize) : NULL;
    CHECK(vbuf && fileio_read_file(voice, vbuf, (uint32_t)vsize) == vsize, "%s is in voice.dat", voice);
    if (vbuf) {
        int id = sceAtracSetDataAndGetID(vbuf, (uint32_t)vsize);
        if (id < 0) {
            printf("skip  the voice decode: no ATRAC3plus decoder (0x%08X)\n", (unsigned)id);
        } else {
            static int16_t frame[2 * 2048];
            int peak = 0, total = 0, clipped = 0, crossings = 0;
            double sq = 0.0;
            for (int f = 0; f < 40; f++) {
                int n = 0, finish = 0, remain = 0;
                if (sceAtracDecodeData(id, frame, &n, &finish, &remain) < 0)
                    break;
                for (int i = 0; i < n; i++) { /* mono: one sample each */
                    peak = SDL_max(peak, abs(frame[i]));
                    sq += (double)frame[i] * frame[i];
                    clipped += abs(frame[i]) >= 32767;
                    crossings += i > 0 && (frame[i] < 0) != (frame[i - 1] < 0);
                }
                total += n;
                if (finish)
                    break;
            }
            sceAtracReleaseAtracID(id);
            /* Speech: loud enough, rarely at full scale, and not noise (noise crosses
             * zero about every other sample). */
            int rms = total ? (int)SDL_sqrt(sq / total) : 0;
            CHECK(vbuf[0x16] == 1 && total > 44100 / 2 && rms > 300 && clipped < total / 100 &&
                      crossings < total / 4,
                  "the voice decodes: mono %d, %d samples, rms %d, peak %d, %d clipped, %d zero crossings",
                  vbuf[0x16] == 1, total, rms, peak, clipped, crossings);
        }
        free(vbuf);
    }

    printf("%d steps, %d script commands read; %s\n", STORY_STEPS, commands,
           failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
