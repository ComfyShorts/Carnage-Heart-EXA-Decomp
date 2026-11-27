/* PC entry point. For now this opens the window and runs an empty frame loop; decompiled
 * game code in src/game/ will be called from here once it exists.
 *
 * Options:
 *   --disc DIR        the game's files: the disc root (holding PSP_GAME/) or its
 *                     PSP_GAME/USRDIR folder. Served as disc0:. Default: $CHEXA_DISC
 *   --ms DIR          folder to serve as the memory stick (ms0:), writable. Optional
 *   --frames N        quit after N frames (for scripted runs)
 *   --scale N         window scale (default 2, i.e. 960x544)
 *   --call-all-stubs  call every still-stubbed PSP import once with no arguments, to
 *                     check the logging (debug only)
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

/* Libraries with real implementations: their functions take arguments, so
 * --call-all-stubs must not call them blind. */
static const char *const k_implemented_libs[] = {"IoFileMgrForUser", "sceUmdUser"};

static int is_implemented(const PspImport *imp)
{
    for (size_t i = 0; i < sizeof k_implemented_libs / sizeof k_implemented_libs[0]; i++)
        if (!strcmp(imp->library, k_implemented_libs[i]))
            return 1;
    return 0;
}

int main(int argc, char **argv)
{
    long frames = -1;
    int scale = 2;
    int call_all = 0;
    const char *disc = getenv("CHEXA_DISC");
    const char *ms = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--disc") && i + 1 < argc)
            disc = argv[++i];
        else if (!strcmp(argv[i], "--ms") && i + 1 < argc)
            ms = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc)
            scale = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--call-all-stubs"))
            call_all = 1;
        else {
            fprintf(stderr, "usage: %s [--disc DIR] [--ms DIR] [--frames N] [--scale N] [--call-all-stubs]\n",
                    argv[0]);
            return 2;
        }
    }

    SDL_Log("[pc] chexa-pc, %d-bit build, %d PSP imports stubbed", (int)(sizeof(void *) * 8),
            psp_import_count);
    if (disc) {
        if (!psp_io_mount_disc(disc))
            return 1;
        SceIoStat st;
        if (sceIoGetstat("disc0:/PSP_GAME/USRDIR/chexa.idx", &st) == 0)
            SDL_Log("[pc] game index found: disc0:/PSP_GAME/USRDIR/chexa.idx, %lld bytes",
                    (long long)st.st_size);
        else
            SDL_Log("[pc] warning: disc0:/PSP_GAME/USRDIR/chexa.idx not found");
    } else {
        SDL_Log("[pc] no game files mounted (pass --disc DIR or set CHEXA_DISC)");
    }
    if (ms && !psp_io_mount("ms0", ms, true))
        return 1;

    if (!psp_platform_init("Carnage Heart EXA (PC port)", scale))
        return 1;

    if (call_all)
        for (int i = 0; i < psp_import_count; i++)
            if (!is_implemented(&psp_imports[i]))
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
