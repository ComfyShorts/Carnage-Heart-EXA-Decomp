/* The scene player: CEventScriptWnd running one script of a .chs file in adv/, as
 * Story Mode's scenes, briefings and lessons do. See docs/story.md ("Scene scripts").
 *
 *   CEventScriptAnalysis  Load FUN_088deef0, header FUN_088df10c, Seek FUN_088df220,
 *                         the word readers FUN_088df1dc / FUN_088df1f0 / FUN_088df204.
 *   CEventScriptWnd       Initialize FUN_088e61d0, Start FUN_088e3404, update
 *                         FUN_088e661c with updateCommand FUN_088e6e30 and the input
 *                         FUN_088e4c3c (the page FUN_088e9134), draw FUN_088e5730
 *                         (FUN_088e5800, the window FUN_088eb690).
 *   CEventScriptPicture   FUN_088e09b4 .. FUN_088e1190: a background or portrait with
 *                         its fade, shake and tint (update FUN_088e0f28, draw
 *                         FUN_088e1064).
 *
 * Text, pictures and sound effects come from the user's disc at run time. */
#ifndef GAME_SCENE_H
#define GAME_SCENE_H

#include <stdbool.h>

/* Loads `path` (e.g. "adv/scenario01.chs") and starts the script named `id`. False if
 * the file or the script isn't there. */
bool scene_start(const char *path, const char *id);

/* One frame. Returns 1 while the script runs, 0 once it has ended. */
int scene_update(float dt);

/* Draws the pictures and the message window (call from a render node). */
void scene_draw(void);

/* Frees the script and the pictures. */
void scene_stop(void);

/* Runs the script to its end at once for its effects on the save (variables, the
 * mission, hidden scripts), drawing and playing nothing: --story-step uses it for the
 * scenes before the step it starts at. */
bool scene_apply(const char *path, const char *id);

/* The same for every script of a file, in the file's order (a lesson day's scripts:
 * the stand-in for a lesson until CLectureFlow is ported). Returns how many ran. */
int scene_apply_file(const char *path);

/* For tools and tests: run the loaded script's commands to the end without waiting,
 * counting them. Returns the number of commands, -1 on a bad command. */
int scene_dry_run(const char *path, const char *id);

/* --auto-scenes: scripts skip through on their own (pages and notices too). */
extern int g_scene_auto;

#endif
