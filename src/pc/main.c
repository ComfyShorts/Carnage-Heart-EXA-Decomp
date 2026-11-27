/* PC entry point. For now this opens the window and runs an empty frame loop; decompiled
 * game code in src/game/ will be called from here once it exists.
 *
 * Options:
 *   --frames N        quit after N frames (for scripted runs)
 *   --scale N         window scale (default 2, i.e. 960x544)
 *   --call-all-stubs  call every PSP import stub once with no arguments, to check the
 *                     logging (debug only: real imports will take arguments)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "psp_imports.h"
#include "psp_shim.h"

#ifdef PCPORT_M32
/* The decompiled structs assume the PSP's ILP32 layout. */
_Static_assert(sizeof(void *) == 4, "32-bit build expected");
_Static_assert(sizeof(long) == 4, "32-bit build expected");
#endif

int main(int argc, char **argv)
{
    long frames = -1;
    int scale = 2;
    int call_all = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc)
            scale = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--call-all-stubs"))
            call_all = 1;
        else {
            fprintf(stderr, "usage: %s [--frames N] [--scale N] [--call-all-stubs]\n", argv[0]);
            return 2;
        }
    }

    SDL_Log("[pc] chexa-pc, %d-bit build, %d PSP imports stubbed", (int)(sizeof(void *) * 8),
            psp_import_count);
    if (!psp_platform_init("Carnage Heart EXA (PC port)", scale))
        return 1;

    if (call_all)
        for (int i = 0; i < psp_import_count; i++)
            psp_imports[i].fn();

    long n = 0;
    while (psp_platform_pump()) {
        psp_platform_present();
        if (frames >= 0 && ++n >= frames)
            break;
    }

    psp_stub_report();
    psp_platform_shutdown();
    return 0;
}
