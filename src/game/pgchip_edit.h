/* The chip editor's rules, without any drawing: PGCHIP::CCommand's editing helpers,
 * CPgChipArea and the parts of CPgChipWnd that change a program. oke_soft.c (Edit
 * Chips) draws and drives them; tests/chip_edit_test.c checks them on the shipped
 * designs. See docs/program-format.md ("Editing") for the rules in prose.
 *
 * A program is a square of width w (main: 6, 10 or 15 by CPU; the two sub-programs 7
 * or none), stored compact, y * w + x. Its start is a column of the top row: the
 * program enters that cell from above. */
#ifndef PGCHIP_EDIT_H
#define PGCHIP_EDIT_H

#include <stdbool.h>

#include "okedb.h"
#include "pgchip.h"

#define PGCHIP_DIRS 8

/* The step for each direction, 0 up then clockwise (CPgChipArea's table, 0x08C1C3F8). */
extern const signed char pgchip_dir_step[PGCHIP_DIRS][2];

/* FUN_089fd528: 0 system, 1 judge (two exits), 2 action, 3 the rest; -1 none or
 * out of range. Read from the EBOOT's table at 0x08C1CBE4. */
int pgchip_kind(int type);
/* FUN_089fe0b8: a chip type at all (1..0x35). */
static inline bool pgchip_valid_type(int type) { return type > 0 && type < PGCHIP_TYPE_END; }
/* A real chip: a valid type that isn't Return. */
bool pgchip_is_chip(const PgChipCommand *c);
bool pgchip_is_return(const PgChipCommand *c);
/* FUN_089fe01c / FUN_089fe054: the default and branch exits, -1 when there is none. */
int pgchip_exit_dir(const PgChipCommand *c);
int pgchip_branch_dir(const PgChipCommand *c);
/* FUN_089fdfe4: the chip's processing mode, which picks the arrow's look (0 or 1). */
int pgchip_attribute(const PgChipCommand *c);
/* FUN_089fcd54: a fresh chip of a type with the game's default parameters (exit 0,
 * branch 4, both status bits clear). */
void pgchip_default(PgChipCommand *c, int type);
/* FUN_089fce18 (mode 0): copy a chip onto a cell. A Return copies as a bare,
 * active Return. */
void pgchip_copy(PgChipCommand *dst, const PgChipCommand *src);

/* One program of a software block. */
typedef struct PgChipArea {
    PgChipCommand *chips;
    int width;      /* 0: the CPU has no such area */
    int32_t *start; /* the column the program starts in */
} PgChipArea;

PgChipArea pgchip_area(SoftwareDataBase *sw, int program);
bool pgchip_in_area(const PgChipArea *a, int x, int y);
PgChipCommand *pgchip_at(const PgChipArea *a, int x, int y);

/* FUN_089f77f8 with the start: how many of the 8 neighbours point into (x, y), the
 * start's "from above" counting as one. -1 off the area. */
int pgchip_incoming(const PgChipArea *a, int x, int y);

/* CPgChipWnd::GetCursorPgChipErrorType (FUN_08a1e710): -1 no error (or no chip),
 * 0 not connected (0x28D), 2 a Call Sub in the main area of a CPU with no
 * sub-areas (0x28F). `subs` says whether both sub-areas exist. */
enum { PGCHIP_ERR_NONE = -1, PGCHIP_ERR_UNCONNECTED = 0, PGCHIP_ERR_SUB_AREA = 1, PGCHIP_ERR_NO_SUB = 2 };
int pgchip_error(const PgChipArea *a, int program, bool subs, int x, int y);

/* The three per-cell updates (FUN_08a1dff4, FUN_08a1e2ac, FUN_08a1e578): a cell
 * something points into gets a Return when empty and a Return nothing points into
 * goes; bit 6 is set on chips with an incoming arrow; bit 7 on cells without an
 * error beyond "not connected". Each returns true when it changed the cell. */
bool pgchip_update_return(const PgChipArea *a, int x, int y);
bool pgchip_update_connected(const PgChipArea *a, int x, int y);
bool pgchip_update_enabled(const PgChipArea *a, int program, bool subs, int x, int y);
/* After an edit at (x, y): Returns and bit 6 for the cell and its 8 neighbours, bit 7
 * for the cell (FUN_08a1e230, FUN_08a1e4fc, FUN_08a1e578). */
void pgchip_update_around(const PgChipArea *a, int program, bool subs, int x, int y);

/* CPgChipWnd::NormalizeArea (FUN_08a190dc) on every program of the block: a start
 * off the area goes to 0, then every cell gets the three updates. */
bool pgchip_has_subs(const SoftwareDataBase *sw);
void pgchip_normalize(SoftwareDataBase *sw);

/* FUN_08a194c4: put a chip at (x, y). An exit that isn't a direction becomes the
 * first free one from it; the neighbours are updated. False if it can't go there
 * (a Return, or no chip). */
bool pgchip_place(SoftwareDataBase *sw, int program, int x, int y, const PgChipCommand *chip);
/* Empty a cell (the chip's exits stop counting; a Return comes back if something
 * still points in). */
void pgchip_remove(SoftwareDataBase *sw, int program, int x, int y);
/* R / L in Edit Chips (FUN_08a197b8 / FUN_08a1a008), and the d-pad in the exit step
 * (FUN_08a19ce0 / FUN_08a1a530): turn the default or branch exit of the chip at
 * (x, y) to the next direction (step +1 / -1) or to `dir`, never onto the other
 * exit. False when nothing changed. */
bool pgchip_turn_exit(SoftwareDataBase *sw, int program, int x, int y, bool branch, int step);
bool pgchip_set_exit(SoftwareDataBase *sw, int program, int x, int y, bool branch, int dir);
/* ClearArea (FUN_08a18ce4): every cell blank, which leaves a Return at each start. */
void pgchip_clear(SoftwareDataBase *sw);
/* CheckMoveStartCursorLeft / Right and MoveStartCursor: move a program's start one
 * column; false at the edge. */
bool pgchip_move_start(SoftwareDataBase *sw, int program, int step);

/* A rectangle of chips picked up by Select Area (Move / Copy) and pasted elsewhere. */
#define PGCHIP_CLIP_MAX (PGCHIP_MAIN_CAP * PGCHIP_MAIN_CAP)
typedef struct PgChipClip {
    int w, h;
    PgChipCommand chips[PGCHIP_CLIP_MAX];
} PgChipClip;

void pgchip_clip_take(PgChipClip *clip, const PgChipArea *a, int x0, int y0, int x1, int y1);
/* Blank a rectangle (Delete, and the source of a Move). */
void pgchip_erase_rect(SoftwareDataBase *sw, int program, int x0, int y0, int x1, int y1);
/* Paste with the clip's top-left at (x, y); cells off the area are dropped and blank
 * cells of the clip leave the target as it was. */
void pgchip_clip_paste(SoftwareDataBase *sw, int program, const PgChipClip *clip, int x, int y);

/* Copy from Card: copy another design's software, cutting each program to this
 * CPU's widths (0x183 warns when something is lost). Returns true when chips were
 * dropped. */
bool pgchip_copy_software(SoftwareDataBase *dst, const SoftwareDataBase *src);

#endif
