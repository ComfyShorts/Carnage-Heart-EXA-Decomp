/* The game's startup path: the ELF entry point, the C runtime start-up, main(), and
 * CApplication::Init as far as it runs without graphics or sound. */
#ifndef GAME_STARTUP_H
#define GAME_STARTUP_H

#include "psp_types.h"

/* ELF entry (module_start, 0x0033630C; Ghidra FUN_08b3a30c). Creates and starts the
 * "user_main" thread, which runs the game. Returns 0 on success, 1 if the thread
 * couldn't be created. args/argp: the NUL-separated argument strings, argv[0] first. */
int game_module_start(SceSize args, void *argp);

/* Where the startup path stopped, once the user_main thread has ended, and main()'s
 * result: 0 when it ran as far as the port goes, -1 while it hasn't returned. */
extern const char *g_startup_stop_reason;
extern int g_startup_status;

#endif
