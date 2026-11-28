/* Checks the save-data path without the game's files: the sceUtilitySavedata shim
 * (src/platform/savedata.c) under the ported CSaveMngr sequences (src/game/savedata.c).
 *   chexa-savedata-test [DIR]   (default: a fresh folder under the build directory)
 * Start-up check on an empty folder creates the system data, the next one loads it;
 * a Battle Data save round-trips its cards; FILES lists it and tells broken files;
 * a Story Data save round-trips the Story Mode block in its own folder. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "psp_imports.h"
#include "psp_shim.h"
#include "savedata.h"

static int failures;

#define CHECK(cond, ...)                         \
    do {                                         \
        printf((cond) ? "ok    " : "FAIL  ");    \
        if (!(cond))                             \
            failures++;                          \
        printf(__VA_ARGS__);                     \
        printf("\n");                            \
    } while (0)

static long size_of(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

int main(int argc, char **argv)
{
    char root[512];
    snprintf(root, sizeof root, "%s", argc > 1 ? argv[1] : "savedata-test");
    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    if (system(cmd) != 0)
        return 1;
    psp_savedata_set_root(root);
    char path[700];

    /* Start-up check, first run: nothing there, so the system data is created. */
    savedata_check_start();
    CHECK(savedata_run(savedata_check_update) == 0, "start-up check on an empty folder");
    snprintf(path, sizeof path, "%s/ULJM05698/DATA.BIN", root);
    CHECK(size_of(path) == SYSTEM_DATA_SIZE, "%s is 0x7C bytes (%ld)", path, size_of(path));
    snprintf(path, sizeof path, "%s/ULJM05698/PARAM.SFO", root);
    CHECK(size_of(path) > 0, "PARAM.SFO written");

    /* Second run: loaded. Set the Story Mode flag (+0x74) in the file to see it come back. */
    snprintf(path, sizeof path, "%s/ULJM05698/DATA.BIN", root);
    FILE *sys = fopen(path, "r+b");
    if (sys) {
        fseek(sys, 0x74, SEEK_SET);
        fputc(1, sys);
        fclose(sys);
    }
    savedata_check_start(); /* resets the block to the defaults */
    CHECK(!savedata_story_completed(), "defaults before the check");
    CHECK(savedata_run(savedata_check_update) == 0, "start-up check with data");
    CHECK(savedata_story_completed(), "the saved block came back (Story Mode flag set)");

    /* Battle Data: two cards, saved, cleared, listed, loaded. */
    savedata_new_game();
    for (int i = 0; i < 2; i++) {
        OkeDataBase *c = &g_save.db.card[i * 5];
        c->header[0] = 1;
        snprintf((char *)c->header + 4, 0x1C, "Unit %d", i);
        c->software.width[0] = 7 + i;
    }
    memcpy(g_save.battle, "PLAYER", 6);
    /* A team card: team 3 with cards 6 and 1 on spots 4 and 0. */
    savedata_team_create(2, "Squad", "Owner");
    savedata_team_set_member(2, 0, 5, 4);
    savedata_team_set_member(2, 1, 0, 0);
    CHECK(savedata_team_status(2) == 1 && savedata_team_count(2) == 2, "team 3 made with two members");
    CHECK(savedata_team_member(2, 2) == -1 && savedata_team_spot(2, 2) == -1, "its third slot is empty");
    savedata_save_start(SAVE_BATTLE, "UNIT.CHE");
    CHECK(savedata_run(savedata_io_update) == 0, "save UNIT.CHE (new: MAKEDATA)");
    snprintf(path, sizeof path, "%s/ULJM05698B/UNIT.CHE", root);
    CHECK(size_of(path) == BATTLE_SAVE_SIZE, "UNIT.CHE is 0x%X bytes (%ld)", BATTLE_SAVE_SIZE, size_of(path));
    savedata_save_start(SAVE_BATTLE, "UNIT.CHE");
    CHECK(savedata_run(savedata_io_update) == 0, "save UNIT.CHE again (existing: WRITE)");

    /* A file of the wrong size and one that isn't game data. */
    snprintf(path, sizeof path, "%s/ULJM05698B/SHORT.CHE", root);
    FILE *f = fopen(path, "wb");
    if (f) {
        fputs("0.0.44", f);
        fclose(f);
    }
    snprintf(path, sizeof path, "%s/ULJM05698B/NOTES.TXT", root);
    f = fopen(path, "wb");
    if (f)
        fclose(f);

    savedata_list_start(SAVE_BATTLE);
    savedata_run(savedata_list_update);
    const SaveListEntry *list;
    int n = savedata_list(&list);
    CHECK(n == 2, "the list holds the two .CHE files (%d)", n);
    int good = -1, bad = -1;
    for (int i = 0; i < n; i++) {
        if (!strcmp(list[i].file, "UNIT.CHE"))
            good = list[i].broken ? -2 : i;
        if (!strcmp(list[i].file, "SHORT.CHE"))
            bad = list[i].broken ? i : -2;
    }
    CHECK(good >= 0, "UNIT.CHE listed as good");
    CHECK(bad >= 0, "SHORT.CHE listed as broken");

    savedata_new_game();
    CHECK(!savedata_card_used(0), "new game: card 1 blank");
    savedata_load_start(SAVE_BATTLE, "UNIT.CHE");
    CHECK(savedata_run(savedata_io_update) == 0, "load UNIT.CHE");
    char name[32];
    savedata_card_name(5, name, sizeof name);
    CHECK(savedata_card_used(0) && savedata_card_used(5) && !savedata_card_used(1), "cards 1 and 6 in use");
    CHECK(!strcmp(name, "Unit 1"), "card 6 is named \"%s\"", name);
    CHECK(savedata_card(5)->software.width[0] == 8, "card 6's program came back");
    CHECK(!strcmp(g_save.battle_file, "UNIT.CHE"), "the loaded file is remembered");
    char team[TEAM_NAME_MAX + 1];
    savedata_team_name(2, team, sizeof team);
    CHECK(!strcmp(team, "Squad") && savedata_team_member(2, 0) == 5 && savedata_team_spot(2, 0) == 4 &&
              savedata_team_member(2, 1) == 0 && savedata_team_spot(2, 1) == 0,
          "team 3 came back (\"%s\", cards %d/%d)", team, savedata_team_member(2, 0), savedata_team_member(2, 1));
    CHECK(savedata_team_status(0) == 0, "team 1 still unused");
    savedata_team_set_member(2, 0, -1, 4);
    CHECK(savedata_team_member(2, 0) == -1 && savedata_team_spot(2, 0) == -1 && savedata_team_count(2) == 1,
          "unregistering clears the card and the spot");
    savedata_team_delete(2);
    CHECK(savedata_team_status(2) == 0, "a deleted team is unused");

    savedata_load_start(SAVE_BATTLE, "SHORT.CHE");
    CHECK(savedata_run(savedata_io_update) != 0, "loading SHORT.CHE fails");
    savedata_load_start(SAVE_BATTLE, "NONE.CHE");
    CHECK(savedata_run(savedata_io_update) == SCE_UTILITY_SAVEDATA_ERROR_RW_FILE_NOT_FOUND,
          "loading a missing file gives RW_FILE_NOT_FOUND");

    /* Story Data: ULJM05698S, the shared block and the Story Mode block. */
    savedata_new_game();
    savedata_story_new();
    CHECK(savedata_story_step() == STORY_STEP_NONE, "new story: no step yet");
    savedata_set_story_step(0x30);
    savedata_set_story_battle(0x26);
    savedata_set_story_script("adv/mission01.chs");
    CHECK(savedata_story_field_hide(0xD, 0, "mission01") && savedata_story_field_hide(0xD, 3, "mission01b"),
          "two hidden scripts");
    savedata_story_field_clear(0xD, 0);
    savedata_save_start(SAVE_STORY, "STORY.CHE");
    CHECK(savedata_run(savedata_io_update) == 0, "save STORY.CHE");
    snprintf(path, sizeof path, "%s/%sS/STORY.CHE", root, SAVE_GAME_NAME);
    CHECK(size_of(path) == STORY_SAVE_SIZE, "STORY.CHE is 0x%X bytes (%ld)", STORY_SAVE_SIZE, size_of(path));
    savedata_new_game();
    CHECK(savedata_story_step() == STORY_STEP_NONE, "a new game clears the story");
    savedata_list_start(SAVE_STORY);
    savedata_run(savedata_list_update);
    n = savedata_list(&list);
    CHECK(n == 1 && !strcmp(list[0].file, "STORY.CHE") && !list[0].broken, "the Story Data list has STORY.CHE");
    savedata_load_start(SAVE_STORY, "STORY.CHE");
    CHECK(savedata_run(savedata_io_update) == 0, "load STORY.CHE");
    CHECK(savedata_story_step() == 0x30 && savedata_story_battle() == 0x26 &&
              !strcmp(savedata_story_script(), "adv/mission01.chs"),
          "the story came back (step %d, battle %d)", savedata_story_step(), savedata_story_battle());
    const char *hidden = savedata_story_field_seek(0xD, 3);
    CHECK(!savedata_story_field_exists(0xD, 0) && hidden && !strcmp(hidden, "mission01b"),
          "the hidden scripts came back");
    CHECK(!strcmp(g_save.story_file, "STORY.CHE"), "the loaded story file is remembered");
    savedata_load_start(SAVE_BATTLE, "STORY.CHE");
    CHECK(savedata_run(savedata_io_update) != 0, "STORY.CHE isn't Battle Data");

    printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
