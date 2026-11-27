/* Read-only game data taken from the user's own decrypted executable at run time.
 *
 * Some data the screens need lives in the executable rather than the pack files: the
 * menu strings (CDesignString's table) for example. The port doesn't carry copies;
 * it reads them from a decrypted EBOOT (ELF) the user supplies, by the same splat
 * addresses the rest of the code documents (Ghidra address - 0x08804000).
 * Without the file the lookups return NULL and the caller draws nothing. */
#ifndef GAME_GAMEDATA_H
#define GAME_GAMEDATA_H

#include <stdbool.h>
#include <stdint.h>

/* Loads the ELF. Returns false (and logs why) if it's missing or not the game's. */
bool gamedata_open(const char *path);
bool gamedata_available(void);

/* A 32-bit word at a splat address, or `fallback` without the ELF. */
uint32_t gamedata_word(uint32_t addr, uint32_t fallback);

/* The NUL-terminated string at a splat address, or NULL. */
const char *gamedata_string(uint32_t addr);

/* CDesignString::GetString (FUN_088c09bc): one of the 0x13B interface strings
 * ("Start Game", "Story Mode", ...). Out-of-range ids give string 1, as in the game
 * when `allow_fallback` is set; NULL without the ELF. */
const char *design_string(int id, int allow_fallback);

#endif
