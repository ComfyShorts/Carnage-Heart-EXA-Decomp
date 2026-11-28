/* Story Mode's lessons (docs/story.md, "Lessons"):
 *   - CLectureCredit (FUN_08934024 .. FUN_08934748): the 49 lesson cards of the table
 *     at 0x08c80f00 (lesson number, title, required bits, script file, first and last
 *     script, file set), their states in the story block (+0x88 / +0xBC), the scene
 *     player's lesson ops (op 0x10 flag 0x10000000, the op 0x12 test) and ReportEnd,
 *     which ends a lesson when its last script ends and the day when its cards are done;
 *   - CLectureMngr (FUN_08936d50 Initialize, FUN_0893834c input, FUN_08938d80 draw):
 *     the Lesson card screen, field menu 0x14, opened from the hub's Lesson item. */
#ifndef GAME_LESSON_H
#define GAME_LESSON_H

#include <stdbool.h>
#include <stddef.h>

#define LESSON_CARDS 49

/* ---- CLectureCredit ---- */

/* The card a lesson number (2..50) is stored at (the table at 0x08c7d190), -1 none. */
int lesson_card_of(int number);
const char *lesson_title(int card);
bool lesson_done(int card); /* (state & required) == required */
bool lesson_listed(int card);

/* FUN_088e435c: op 0x10 with flag 0x10000000 (op 5 sets, 6 ors; the value's high
 * nibble goes to the offered word, the low one to the state). */
void lesson_var(int op, int number, int value);
/* FUN_088e44bc: after that op, with value 1 and the card now done, the notice's text
 * ("Completed Lesson: %s"); false when there's none. */
bool lesson_completed_text(int number, int value, char *out, size_t n);
/* FUN_088e41e4: the op 0x12 test on slot number - 1 (1 equal, 2 not equal, 7 any bit
 * set). 1 taken, 2 not, 0 for a test it doesn't know. */
int lesson_test(int test, int number, int value);
/* FUN_08934480 (ReportEnd), on every script's end: a lesson ends with its last
 * script, and when all the day's cards are done the day is (FUN_089348f0). */
void lesson_report_end(const char *id);
/* The day was done since the last call (the step-done flag, FUN_0889e170). */
bool lesson_take_day_done(void);

/* ---- CLectureMngr: the Lesson card screen ---- */

void lesson_screen_open(void);
/* 1 while open, 0 when closed (cross). */
int lesson_screen_update(float dt);
void lesson_screen_draw(void);

#endif
