/* PC entry point. Opens the window and either runs an empty frame loop or, with
 * --startup, the game's own start-up code (src/game/startup.c) on a PSP thread while
 * this thread handles window events.
 *
 * Options:
 *   --disc DIR        the game's files: the disc root (holding PSP_GAME/) or its
 *                     PSP_GAME/USRDIR folder. Served as disc0:. Default: $CHEXA_DISC
 *   --startup         run the game's start-up path (needs --disc); quits when it stops
 *   --trace           log every call of an implemented import, not just the first
 *   --ms DIR          folder to serve as the memory stick (ms0:), writable. Optional
 *   --frames N        quit after N frames (for scripted runs). With --startup, N frames
 *                     shown by the game: then the HOME menu's exit is requested
 *   --gu-trace N      log the game's sceGu calls during its first N frames
 *   --ge-trace        log every GE command executed (very verbose)
 *   --screenshot [FILE]  save the first frame the game draws as a PNG
 *                     (default orig/first_frame.png)
 *   --shot N FILE     save displayed frame N as a PNG (repeatable, up to 8)
 *   --press N BUTTON  press a PSP button for 4 frames from displayed frame N
 *                     (circle, cross, square, triangle, up, down, left, right,
 *                     start, select, l, r; repeatable)
 *   --gmo FILE        with --startup: show a GMO model from the pack instead of the
 *                     title (--gmo-tex FILE: take its textures from another model,
 *                     --gmo-angle DEG: starting turn)
 *   --oke ID          with --startup: show an assembled OKE body (--oke-pose N)
 *   --map N           with --startup: show the generated terrain of map N
 *   --elf FILE        the decrypted EBOOT, for interface text (default orig/EBOOT.elf)
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
#include "ge.h"
#include "startup.h"
#include "modelview.h"

#ifdef PCPORT_M32
/* The decompiled structs assume the PSP's ILP32 layout. */
_Static_assert(sizeof(void *) == 4, "32-bit build expected");
_Static_assert(sizeof(long) == 4, "32-bit build expected");
#endif

/* Imports with real implementations take arguments, so --call-all-stubs must not call
 * them blind: whole libraries, then single functions. */
static const char *const k_implemented_libs[] = {"IoFileMgrForUser", "sceUmdUser"};
static const char *const k_implemented_funcs[] = {
    "sceKernelCreateThread", "sceKernelStartThread", "sceKernelExitThread", "sceKernelExitDeleteThread",
    "sceKernelDeleteThread", "sceKernelWaitThreadEnd", "sceKernelGetThreadId",
    "sceKernelGetThreadExitStatus", "sceKernelGetThreadCurrentPriority", "sceKernelChangeThreadPriority",
    "sceKernelChangeCurrentThreadAttr", "sceKernelDelayThread", "sceKernelDelayThreadCB",
    "sceKernelCreateCallback", "sceKernelDeleteCallback", "sceKernelCheckCallback", "sceKernelCreateMutex",
    "sceKernelDeleteMutex", "sceKernelLockMutexCB", "sceKernelUnlockMutex", "sceKernelCpuSuspendIntr",
    "sceKernelCpuResumeIntr", "sceKernelCpuResumeIntrWithSync", "sceKernelGetSystemTimeWide",
    "sceKernelGetSystemTimeLow", "sceKernelAllocPartitionMemory", "sceKernelFreePartitionMemory",
    "sceKernelGetBlockHeadAddr", "sceKernelPrintf", "sceKernelSetCompiledSdkVersion606",
    "sceKernelSetCompilerVersion", "sceKernelGetModuleId", "sceKernelGetModuleIdByAddress",
    "sceKernelExitGame", "sceKernelRegisterExitCallback", "sceKernelDcacheWritebackAll",
    "sceKernelDcacheWritebackInvalidateAll", "sceKernelDcacheWritebackRange", "sceKernelStdin",
    "sceKernelStdout", "sceKernelStderr", "sceUtilityGetSystemParamInt", "sceUtilityLoadModule",
    "sceUtilityUnloadModule", "sceImposeSetLanguageMode", "scePowerRegisterCallback",
    "scePowerUnregisterCallback", "sceRtcGetCurrentClock", "sceGeEdramGetAddr", "sceGeEdramGetSize", "sceGeListEnQueue", "sceGeListEnQueueHead",
    "sceGeListUpdateStallAddr", "sceGeListSync", "sceGeDrawSync", "sceGeSetCallback", "sceGeUnsetCallback",
    "sceGeBreak", "sceGeContinue", "sceDisplaySetMode", "sceDisplaySetFrameBuf", "sceDisplayGetFrameBuf",
    "sceDisplayGetVcount", "sceDisplayWaitVblankStartCB", "sceDisplayGetFramePerSec", "sceDmacMemcpy",
    "sceKernelCreateEventFlag", "sceKernelDeleteEventFlag", "sceKernelSetEventFlag", "sceKernelClearEventFlag",
    "sceKernelWaitEventFlag", "sceKernelWaitEventFlagCB",
    "sceCtrlSetSamplingMode",
};

static int is_implemented(const PspImport *imp)
{
    for (size_t i = 0; i < SDL_arraysize(k_implemented_libs); i++)
        if (!strcmp(imp->library, k_implemented_libs[i]))
            return 1;
    for (size_t i = 0; i < SDL_arraysize(k_implemented_funcs); i++)
        if (!strcmp(imp->name, k_implemented_funcs[i]))
            return 1;
    return 0;
}

static uint32_t button_by_name(const char *n)
{
    static const struct { const char *name; uint32_t bit; } k[] = {
        {"circle", PSP_CTRL_CIRCLE}, {"cross", PSP_CTRL_CROSS}, {"square", PSP_CTRL_SQUARE},
        {"triangle", PSP_CTRL_TRIANGLE}, {"up", PSP_CTRL_UP}, {"down", PSP_CTRL_DOWN},
        {"left", PSP_CTRL_LEFT}, {"right", PSP_CTRL_RIGHT}, {"start", PSP_CTRL_START},
        {"select", PSP_CTRL_SELECT}, {"l", PSP_CTRL_LTRIGGER}, {"r", PSP_CTRL_RTRIGGER},
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        if (!SDL_strcasecmp(n, k[i].name))
            return k[i].bit;
    return 0;
}

int main(int argc, char **argv)
{
    long frames = -1;
    int scale = 2;
    int call_all = 0;
    int startup = 0;
    const char *disc = getenv("CHEXA_DISC");
    const char *ms = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--disc") && i + 1 < argc)
            disc = argv[++i];
        else if (!strcmp(argv[i], "--ms") && i + 1 < argc)
            ms = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--shot") && i + 2 < argc) {
            uint32_t n = (uint32_t)strtoul(argv[i + 1], NULL, 0);
            psp_display_request_screenshot_at(n, argv[i + 2]);
            i += 2;
        } else if (!strcmp(argv[i], "--press") && i + 2 < argc) {
            uint32_t b = button_by_name(argv[i + 2]);
            if (!b) {
                fprintf(stderr, "unknown button %s\n", argv[i + 2]);
                return 2;
            }
            psp_ctrl_script_press((uint32_t)strtoul(argv[i + 1], NULL, 0), b);
            i += 2;
        } else if (!strcmp(argv[i], "--gmo") && i + 1 < argc)
            g_modelview_path = argv[++i];
        else if (!strcmp(argv[i], "--gmo-tex") && i + 1 < argc)
            g_modelview_textures = argv[++i];
        else if (!strcmp(argv[i], "--oke") && i + 1 < argc)
            g_modelview_oke = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--map") && i + 1 < argc)
            g_modelview_map = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--oke-pose") && i + 1 < argc)
            g_modelview_pose = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--gmo-angle") && i + 1 < argc)
            g_modelview_angle = strtof(argv[++i], NULL);
        else if (!strcmp(argv[i], "--elf") && i + 1 < argc)
            g_game_elf_path = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc)
            scale = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--call-all-stubs"))
            call_all = 1;
        else if (!strcmp(argv[i], "--startup"))
            startup = 1;
        else if (!strcmp(argv[i], "--trace"))
            psp_trace_enable(true);
        else if (!strcmp(argv[i], "--gu-trace") && i + 1 < argc)
            g_startup_gu_trace_frames = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--ge-trace"))
            psp_ge_set_trace(true);
        else if (!strcmp(argv[i], "--screenshot"))
            psp_display_request_screenshot(i + 1 < argc && strncmp(argv[i + 1], "--", 2) ? argv[++i]
                                                                                       : "orig/first_frame.png");
        else {
            fprintf(stderr,
                    "usage: %s [--disc DIR] [--ms DIR] [--startup] [--trace] [--frames N] [--scale N] "
                    "[--gu-trace N] [--ge-trace] [--screenshot [FILE]] [--shot N FILE] [--press N BUTTON] "
                    "[--elf FILE] [--gmo FILE] [--call-all-stubs]\n",
                    argv[0]);
            return 2;
        }
    }

    SDL_Log("[pc] chexa-pc, %d-bit build, %d PSP imports stubbed", (int)(sizeof(void *) * 8),
            psp_import_count);
    if (disc) {
        if (!psp_io_mount_disc(disc))
            return startup ? 77 : 1; /* 77: "skipped" for the CTest run without game files */
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
    if (startup && !disc) {
        fprintf(stderr, "--startup needs the game files (--disc DIR or CHEXA_DISC)\n");
        return 2;
    }

    if (!psp_platform_init("Carnage Heart EXA (PC port)", scale))
        return 1;

    if (call_all)
        for (int i = 0; i < psp_import_count; i++)
            if (!is_implemented(&psp_imports[i]))
                psp_imports[i].fn();

    if (startup) {
        /* The argument block the PSP passes to module_start: the boot path. */
        static char boot_args[] = "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN";
        SDL_Log("[pc] running the game's start-up code");
        if (game_module_start(sizeof boot_args, boot_args) != 0) {
            SDL_Log("[pc] module_start failed");
            startup = 0;
        }
    }

    long n = 0;
    bool quit_sent = false;
    for (;;) {
        if (!psp_platform_pump()) {
            if (!startup || !psp_kernel_threads_alive())
                break;
            if (!quit_sent) { /* window closed: ask the game to quit, as the HOME menu would */
                psp_kernel_request_exit();
                quit_sent = true;
            }
        }
        psp_platform_present();
        if (startup) {
            /* Count the game's frames, then quit the way the HOME menu does. */
            if (frames >= 0 && !quit_sent && psp_display_frame_count() >= (uint32_t)frames) {
                SDL_Log("[pc] %ld frames shown: requesting exit", frames);
                psp_kernel_request_exit();
                quit_sent = true;
            }
        } else if (frames >= 0 && ++n >= frames) {
            break;
        }
        if (startup && !psp_kernel_threads_alive())
            break;
    }
    if (startup) {
        SDL_Log("[pc] start-up path ended: %s", g_startup_stop_reason);
        if (psp_kernel_exit_called())
            SDL_Log("[pc] the game called sceKernelExitGame");
    }

    psp_import_report();
    psp_platform_shutdown();
    if (startup) {
        SDL_Log("[pc] %u frames shown", (unsigned)psp_display_frame_count());
    }
    if (startup && (psp_kernel_threads_alive() || (g_startup_status != 0 && !psp_kernel_exit_called()))) {
        SDL_Log("[pc] start-up path did not finish cleanly (status %d)", g_startup_status);
        return 1;
    }
    return 0;
}
