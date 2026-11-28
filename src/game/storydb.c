/* See storydb.h. Hand-written from the CScenarioInfoDataBase accessors
 * FUN_08a69784 .. FUN_08a69d2c (each checks 0 <= step <= 0x55 and reads one field). */
#include "storydb.h"

#include <stddef.h>

#include "gamedata.h"

#define STEP_TABLE 0x00426344u /* 0x08c2a344 */
#define STEP_SIZE 0x28u
#define STEP_ENTRIES 0x56

enum {
    F_SCRIPT = 0x00,
    F_SCRIPT_ID = 0x04,
    F_LECTURE = 0x08,
    F_BRIEFING = 0x0C,
    F_TITLE = 0x10,
    F_INDEX = 0x14,
    F_PROGRESS = 0x18,
    F_FILE_SET = 0x1C,
    F_CHAPTER = 0x20,
};

static bool in_range(int step) { return step >= 0 && step < STEP_ENTRIES; }

static uint32_t field(int step, uint32_t off, uint32_t fallback)
{
    return in_range(step) ? gamedata_word(STEP_TABLE + (uint32_t)step * STEP_SIZE + off, fallback) : fallback;
}

static const char *field_string(int step, uint32_t off)
{
    /* The EBOOT is relocatable: its pointers are offsets from the image start, or
     * PSP addresses once relocated. */
    uint32_t p = field(step, off, 0);
    if (p >= 0x08804000u)
        p -= 0x08804000u;
    return p ? gamedata_string(p) : NULL;
}

const char *storydb_script(int step) { return field_string(step, F_SCRIPT); }
const char *storydb_script_id(int step) { return field_string(step, F_SCRIPT_ID); }
bool storydb_lecture_part(int step) { return field(step, F_LECTURE, 0) != 0; }
bool storydb_briefing(int step) { return field(step, F_BRIEFING, 0) != 0; }
bool storydb_title(int step) { return field(step, F_TITLE, 0) != 0; }
int storydb_story_index(int step) { return (int)field(step, F_INDEX, 0xFFFFFFFFu); }
const char *storydb_file_set(int step) { return field_string(step, F_FILE_SET); }
const char *storydb_chapter_picture(int step) { return field_string(step, F_CHAPTER); }

const char *storydb_progress(int step, bool full)
{
    /* The short form skips "Progress :" (10 bytes). */
    const char *s = field_string(step, F_PROGRESS);
    if (!s || full)
        return s;
    for (int i = 0; i < 10; i++)
        if (!s[i])
            return s + i;
    return s + 10;
}
