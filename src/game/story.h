/* Story Mode, as the game runs it:
 *
 *   CTutorialFlow (ctor FUN_08ae0df0, Update FUN_08ae1938): walks the step list
 *     (storydb.h) one child at a time: an adventure part for chapter cards and story
 *     scenes, the briefing hub for lessons and missions, the ending after the last.
 *   CStoryFlow / CStoryMngr (FUN_08ab16b4, Initialize FUN_08ab1dcc, update
 *     FUN_08ab2278, draw FUN_08ab29b8): a chapter title card (asking "Save your
 *     progress?" first when it follows another step) or a scene script.
 *   CScenarioClearSaveFlow (FUN_08a68dc8): after the ending, "Save cleared data?".
 *
 * Text and pictures are read from the user's EBOOT and disc at run time. What is
 * still a stand-in is listed in docs/story.md ("Port status"). */
#ifndef GAME_STORY_H
#define GAME_STORY_H

#include <stdbool.h>

/* The scene manager's FUN_088d3d80: mode 0 (a new game: the save's blocks are reset
 * and CTutorialFlow type 1 starts) or mode 3 (`loaded`: Story Data loaded through
 * Continue, type 2). */
void story_start(bool loaded);

/* One step. Returns 0 once Story Mode has ended (back to the title). */
int story_update(float dt);

/* For tools and tests: start at `step` (0..0x54) of a new game. --story-step N sets
 * g_story_step, and the main loop then starts there instead of on the title. */
void story_start_at(int step);
extern int g_story_step;

#endif
