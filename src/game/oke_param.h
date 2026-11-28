/* Software Setup's parameter sub-menus: CSoftDesignMngr modes 0x13 (the sub-menu),
 * 0x14 (a value window), 0x15 (the number after "Value" / "Specify Altitude"),
 * 0x17 (the range editor) and 0x18 / 0x19 (direction / elevation). They edit a
 * working copy of one chip; oke_soft.c puts it on the cell. See oke_param.c and
 * docs/screens.md ("Software Setup"). */
#ifndef GAME_OKE_PARAM_H
#define GAME_OKE_PARAM_H

#include <stdbool.h>

#include "okedb.h"
#include "pgchip.h"

typedef enum {
    PARAM_OPEN,    /* still editing */
    PARAM_CONFIRM, /* Confirm: on to the exit step (mode 0x1A) */
    PARAM_CANCEL,  /* cross: back where it came from, the chip kept as edited */
} ParamResult;

/* A chip type with parameters (a non-empty list in the table at 0x08C2DCFC). */
bool param_has(int type);
/* Start on a copy of `chip`. `sw` names the counters, `card` gives the weapon and
 * option counts and the CPU's class. */
void param_open(const PgChipCommand *chip, const SoftwareDataBase *sw, const OkeDataBase *card);
ParamResult param_update(float dt);
const PgChipCommand *param_chip(void);
void param_draw(void);
/* The help line, or NULL for the chip's own. */
const char *param_help(void);

#endif
