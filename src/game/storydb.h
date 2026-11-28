/* CScenarioInfoDataBase: Story Mode's step list, read from the EBOOT at run time
 * (0x08c2a344, 0x56 entries of 0x28 bytes). See docs/story.md ("Structure").
 *
 * The story is one linear list: chapter title cards, story scenes, lessons and
 * missions, one after another. Entry 0x55 is a debug step the game never reaches. */
#ifndef GAME_STORYDB_H
#define GAME_STORYDB_H

#include <stdbool.h>

#define STORY_STEPS 0x55     /* 0..0x54 */
#define STORY_STEP_LAST 0x54 /* the ending follows it (CTutorialFlow::Update) */

/* Accessors in the game's order; each takes a step 0..0x55 and returns NULL / 0 / -1
 * out of range or when the EBOOT isn't open. */
const char *storydb_script(int step);       /* FUN_08a69784: "adv/scenario01.chs", NULL on title steps */
const char *storydb_script_id(int step);    /* FUN_08a69804: "scenario01" */
bool storydb_lecture_part(int step);        /* FUN_08a698e4: the lesson half (steps 0-45) */
bool storydb_briefing(int step);            /* FUN_08a69964: IsHideScriptBeforeBriefing */
bool storydb_title(int step);               /* FUN_08a699e4: a chapter title card */
int storydb_story_index(int step);          /* FUN_08a69af4: lesson day / mission group, -1 */
const char *storydb_progress(int step, bool full); /* FUN_08a69b74: "Progress :Chapter 1" or "Chapter 1" */
const char *storydb_file_set(int step);     /* FUN_08a69d2c: "story01_adv" */
const char *storydb_chapter_picture(int step); /* FUN_08a69c0c: "adv/chpt_01.gim" */

#endif
