/* The game's startup path, written from the Ghidra export and the disassembly.
 * Addresses are splat addresses (Ghidra = splat + 0x08804000).
 *
 *   module_start      0x0033630C   SDK version calls, starts "user_main"
 *   user_main (crt0)  0x0033642C   heap, argv, newlib state, atexit/init, main, exit
 *   sbrk              0x0033695C   "UserSbrk" partition, 18 MiB
 *   main              0x0013BE0C   static constructors, CApplication::Init, then the
 *                                  application object (0x00001C64) and its run loop
 *                                  (0x000024B0, the per-frame main loop)
 *   CApplication::Init 0x00001714  everything below up to the graphics start-up
 *
 * CApplication::Init runs up to the GU initialisation (CGraphics, src/game/render.c);
 * the sound, the game's managers and the "gamecommon" file set after it are skipped.
 * main then runs the graphics half of CApplication::Start (display set-up, the first
 * screen's clear colour) and a frame loop that does what the real main loop does for
 * the screen each frame (CRender's frame end), until the HOME menu's exit callback
 * calls sceKernelExitGame. The screens and the scene code aren't ported, so nothing
 * but the clear is drawn. */
#include "startup.h"

#include <stdint.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "fileio.h"
#include "gu.h"
#include "heap.h"
#include "psp_imports.h"
#include "render.h"

const char *g_startup_stop_reason = "user_main has not run";
int g_startup_status = -1;
int g_startup_gu_trace_frames;

/* ---- newlib heap: sbrk and memalign ---- */

/* D_0044D8D0: the user heap size in KiB, a link-time constant. */
#define USER_HEAP_KB 18432

static uint8_t *g_sbrk_cur, *g_sbrk_base, *g_sbrk_end;
static SceUID g_sbrk_block;

/* sbrk (0x0033695C). The first call allocates the whole user heap as one partition
 * block; later calls move the break inside it. */
static void *game_sbrk(int32_t incr)
{
    if (!g_sbrk_base) {
        uint32_t size = USER_HEAP_KB * 1024;
        g_sbrk_block = sceKernelAllocPartitionMemory(2, "UserSbrk", 3, size, (void *)0x1000);
        if (g_sbrk_block > 0) {
            g_sbrk_cur = g_sbrk_base = sceKernelGetBlockHeadAddr(g_sbrk_block);
            g_sbrk_end = g_sbrk_base + size;
        }
    }
    if (!g_sbrk_base)
        return (void *)-1;
    uint8_t *next = g_sbrk_cur + incr;
    if (next < g_sbrk_base || next > g_sbrk_end)
        return (void *)-1;
    uint8_t *old = g_sbrk_cur;
    g_sbrk_cur = next;
    return old;
}

/* newlib memalign (0x0033CC14), used once at startup for the default heap. newlib's
 * allocator isn't reproduced: this carves the block straight from the break. Only
 * the game's own heaps (heap.c) hand out memory after that. */
static void *game_memalign(uint32_t align, uint32_t size)
{
    uint8_t *cur = game_sbrk(0);
    if (cur == (void *)-1)
        return NULL;
    uint32_t pad = (uint32_t)(-(uintptr_t)cur & (align - 1));
    uint8_t *p = game_sbrk((int32_t)(pad + size));
    return p == (void *)-1 ? NULL : p + pad;
}

/* ---- random numbers: Mersenne Twister (state at DAT_08ca4f4c) ---- */

typedef struct GameRng {
    uint32_t index;
    uint32_t mt[624];
} GameRng;

static GameRng g_rng;

/* FUN_08b469c0. Returns the tempered current word, then regenerates it, so each word
 * is replaced right after it is used. */
static uint32_t rng_next(GameRng *r)
{
    uint32_t i = r->index;
    uint32_t cur = r->mt[i];
    uint32_t nxt = i < 623 ? r->mt[i + 1] : r->mt[0];
    r->index = i < 623 ? i + 1 : 0;
    uint32_t y = cur ^ cur >> 11;
    y ^= (y & 0x013A58ADu) << 7;
    y ^= (y & 0x0001DF8Cu) << 15;
    uint32_t mix = ((cur & 0x80000000u) | (nxt & 0x7FFFFFFFu)) >> 1;
    if (nxt & 1)
        mix ^= 0x9908B0DFu;
    r->mt[i] = mix ^ r->mt[i < 227 ? i + 397 : i - 227];
    return y ^ y >> 18;
}

/* FUN_08b52254: the standard MT19937 seeding, then 624 draws to warm up. */
static void rng_seed(GameRng *r, uint32_t seed)
{
    r->mt[0] = seed;
    for (uint32_t i = 1; i < 624; i++)
        r->mt[i] = (r->mt[i - 1] ^ r->mt[i - 1] >> 30) * 0x6C078965u + i;
    r->index = 0;
    for (int i = 0; i < 624; i++)
        rng_next(r);
}

/* FUN_08b04698: a seed from the local date and time. */
static uint32_t clock_seed(void)
{
    int tz = 0;
    ScePspDateTime t;
    memset(&t, 0, sizeof t);
    sceUtilityGetSystemParamInt(6 /* time zone */, &tz);
    sceRtcGetCurrentClock(&t, tz);
    /* The game also adds DAT_08c8bf70, which is 0 and never written. */
    return (uint32_t)t.year + t.month + t.day + t.hour + t.minute + t.second + t.microsecond;
}

/* ---- callbacks ---- */

/* FUN_0880568c: the HOME menu's "quit game". */
static int exit_callback(int count, int arg2, void *arg)
{
    sceKernelExitGame();
    return 0;
}

/* 0x000016AC: power events. Sets *state to 2 when resume completes (0x40000) and to
 * 1 while suspending (0x10000). */
static int power_callback(int count, int flags, void *arg)
{
    int *state = arg;
    if (flags & 0x40000)
        *state = 2;
    if (flags & 0x10000)
        *state = 1;
    return 0;
}

static int g_power_state[2]; /* DAT_08ccd820, DAT_08ccd824 */

/* FUN_088063a8. */
static void register_power_callback(SceKernelCallbackFunction fn, int *state)
{
    SceUID cb = sceKernelCreateCallback("PowerCB", fn, state);
    if (cb < 1)
        SDL_Log("[startup] CApplication::Initialize: sceKernelCreateCallback failed");
    else if (scePowerRegisterCallback(-1, cb) < 0)
        SDL_Log("[startup] CApplication::Initialize: scePowerRegisterCallback failed");
    *state = 0;
}

/* ---- CLoadModules (singleton at DAT_08c81be8) ---- */

typedef struct LoadModules {
    int loaded;
    void *jis2ucs; /* 128 KiB Shift-JIS -> UCS-2 table */
    void *ucs2jis; /* 128 KiB UCS-2 -> Shift-JIS table */
} LoadModules;

static LoadModules *g_load_modules;

/* Codec modules, loaded in this order and unloaded in reverse. */
static const int k_av_modules[] = {
    0x305, /* PSP_MODULE_AV_VAAC */
    0x301, /* PSP_MODULE_AV_SASCORE */
    0x300, /* PSP_MODULE_AV_AVCODEC */
    0x302, /* PSP_MODULE_AV_ATRAC3PLUS */
    0x304, /* PSP_MODULE_AV_MP3 */
    0x303, /* PSP_MODULE_AV_MPEGBASE */
};

/* The Shift-JIS conversion tables used by the text code (FUN_08b5219c). */
static const void *g_sjis_to_ucs, *g_ucs_to_sjis;

/* CLoadModules::GetInstance (FUN_0893fb4c) with its constructor (0x0013BD78). */
static LoadModules *load_modules_instance(void)
{
    if (!g_load_modules) {
        LoadModules *m = game_new(sizeof *m);
        if (m) {
            m->loaded = 0;
            m->jis2ucs = heap_alloc(&g_default_heap, 0x20000, 16, "CLoadModules", 0);
            m->ucs2jis = heap_alloc(&g_default_heap, 0x20000, 16, "CLoadModules", 0);
        }
        g_load_modules = m;
        if (!m)
            SDL_Log("[startup] static CLoadModules* CLoadModules::GetInstance(): allocation failed");
    }
    return g_load_modules;
}

/* 0x0013BD0C. */
static void load_modules_unload(LoadModules *m)
{
    for (int i = (int)SDL_arraysize(k_av_modules) - 1; i >= 0; i--)
        sceUtilityUnloadModule(k_av_modules[i]);
    m->loaded = 0;
}

/* CLoadModules::Load (FUN_0893fbd4). */
static int load_modules_load(LoadModules *m)
{
    if (m->loaded)
        return 1;
    for (size_t i = 0; i < SDL_arraysize(k_av_modules); i++)
        if (sceUtilityLoadModule(k_av_modules[i]) < 0) {
            load_modules_unload(m);
            return 0;
        }
    if (fileio_read_file("common/jis2ucs.bin", m->jis2ucs, 0x20000) != 0x20000 ||
        fileio_read_file("common/ucs2jis.bin", m->ucs2jis, 0x20000) != 0x20000) {
        load_modules_unload(m);
        return 0;
    }
    g_sjis_to_ucs = m->jis2ucs;
    g_ucs_to_sjis = m->ucs2jis;
    m->loaded = 1;
    return 1;
}

/* ---- controller (FUN_08b1ecb8) ---- */

/* Pad state globals around DAT_08cd2f64 / DAT_08cb78c0. The three masks are probably
 * the buttons that auto-repeat: everything but SELECT/START, the d-pad with L/R, and
 * the d-pad alone. */
static struct {
    uint32_t state[8];
    uint32_t mask_buttons;   /* 0xF3F0 */
    uint32_t mask_dpad_lr;   /* 0x03F0 */
    uint32_t mask_dpad;      /* 0x00F0 */
} g_pad;

static void pad_init(void)
{
    sceCtrlSetSamplingMode(1 /* analog */);
    memset(&g_pad, 0, sizeof g_pad);
    g_pad.mask_buttons = 0xF3F0;
    g_pad.mask_dpad_lr = 0x03F0;
    g_pad.mask_dpad = 0x00F0;
}

/* ---- CApplication::Init (0x00001714) ---- */

/* Returns 0 when it ran as far as the port goes. */
static int application_init(void)
{
    /* 1. FUN_08b3a26c: an empty hook. */

    /* 2. The exit callback, then two power callbacks (one per state flag). */
    SceUID cb = sceKernelCreateCallback("exit", exit_callback, NULL);
    sceKernelRegisterExitCallback(cb);
    register_power_callback(power_callback, &g_power_state[0]);
    register_power_callback(power_callback, &g_power_state[1]);

    /* 3. This thread uses the VFPU. */
    sceKernelChangeCurrentThreadAttr(0, 0x4000);

    /* 4. English text, cross confirms. */
    if (sceImposeSetLanguageMode(1, 1) != 0)
        SDL_Log("[startup] assertion failed: sceImposeSetLanguageMode(SCE_UTILITY_LANG_ENGLISH, ...) == 0");

    /* 5. Wait for the disc and mount it. */
    if (sceUmdCheckMedium() == 0)
        sceUmdWaitDriveStat(PSP_UMD_PRESENT);
    sceUmdActivate(1, "disc0:");
    sceUmdWaitDriveStat(PSP_UMD_READY);

    /* 6. The default heap: all of the user heap but 32 KiB, left for newlib. */
    uint32_t heap_size = USER_HEAP_KB * 1024 - 0x8000;
    void *heap_mem = game_memalign(16, heap_size);
    if (!heap_mem) {
        SDL_Log("[startup] assertion failed: pHeep (no memory for the default heap)");
        return -1;
    }
    heap_init(&g_default_heap, heap_mem, heap_size, "default heap");

    /* 7. A 2.5 MiB buffer kept in DAT_08ccd818 (new[]; first use not traced yet). */
    void *big_buffer = game_new_array(0x280000);
    (void)big_buffer;

    /* 8. The fast heap: video memory above the first 0xCC000 bytes (FUN_08b0f538). */
    uint32_t edram_size = sceGeEdramGetSize();
    uint8_t *edram = sceGeEdramGetAddr();
    heap_init(&g_fast_heap, edram + 0xCC000, edram_size - 0xCC000, "fast heap");

    /* 9. Seed the random numbers from the clock. */
    rng_seed(&g_rng, clock_seed());

    /* 10. File I/O, then the pack index (retried until it loads, like the game). */
    fileio_init();
    for (int tries = 0; !packindex_read("chexa.idx"); tries++)
        if (tries >= 3) {
            SDL_Log("[startup] PackFile::ReadIndex(\"chexa.idx\") keeps failing");
            return -1;
        }

    /* 11. The "boot" file set: fonts, Shift-JIS tables, map parameters, sound tables. */
    int boot_set = fileset_open("boot", &g_default_heap, 1, 0);
    if (boot_set < 0)
        SDL_Log("[startup] OpenFileSet(\"boot\") failed");

    /* 12. Codec modules and the Shift-JIS tables. */
    LoadModules *lm = load_modules_instance();
    if (!lm || !load_modules_load(lm))
        SDL_Log("[startup] CLoadModules::Load failed");

    /* 13. Controller. */
    pad_init();

    /* 14. CGraphics::GetInstance() and GU start-up. */
    Render *gfx = render_instance();
    if (!gfx) {
        SDL_Log("[startup] CGraphics::GetInstance: out of memory");
        return -1;
    }
    psp_gu_set_trace(g_startup_gu_trace_frames > 0);
    render_gu_init(gfx);

    /* 15-21, not ported yet: CSound (0x121A0 bytes) and its start-up; the timers,
     * utility, pad and file managers (FUN_08b1f904 .. FUN_089f2ecc); the save-data
     * manager; the "gamecommon" file set into the default heap; the sound bank; and
     * AssignSpecialBufferToUtilitySave, which hands the step-7 buffer to the save
     * dialogs. */
    SDL_Log("[startup] CApplication::Init: skipping CSound, the managers and the \"gamecommon\" file set "
            "(not ported)");
    SDL_Log("[startup] state: default heap %u KiB used of %u, fast heap %u KiB, boot set slot %d, "
            "SJIS tables %s, first random number 0x%08X",
            (unsigned)(g_default_heap.used / 1024), (unsigned)(g_default_heap.size / 1024),
            (unsigned)(g_fast_heap.size / 1024), boot_set,
            g_sjis_to_ucs && g_ucs_to_sjis ? "loaded" : "missing", (unsigned)rng_next(&g_rng));
    return 0;
}

/* ---- main loop ---- */

/* The graphics part of CApplication::Start (FUN_08806960) and of the main loop
 * (FUN_088064b0). Runs until the exit callback ends the thread in sceKernelExitGame. */
static void main_loop(void)
{
    Render *gfx = render_instance();
    render_setup_display(gfx, 1);
    /* The first screen's constructor (FUN_088f1d98, CSceneBoot or similar) sets a black
     * clear colour; its child screens and the net-play dialogs aren't ported. */
    render_set_clear_color(gfx, 0);
    g_startup_stop_reason = "main loop: frames run until sceKernelExitGame";
    SDL_Log("[startup] CApplication::Start: display on; entering the main loop (screens not ported, "
            "only the clear is drawn)");
    for (uint32_t frame = 0;; frame++) {
        if (g_startup_gu_trace_frames > 0 && frame < (uint32_t)g_startup_gu_trace_frames)
            SDL_Log("[gu] ---- frame %u ----", (unsigned)frame);
        /* Before this the real loop updates the scene, sound, timers and pad. */
        render_end_frame(gfx);
        if (frame + 1 == (uint32_t)g_startup_gu_trace_frames)
            psp_gu_set_trace(false);
    }
}

/* ---- main (0x0013BE0C) ---- */

static int game_main(int argc, char **argv)
{
    SDL_Log("[startup] main(argc=%d, argv[0]=\"%s\")", argc, argc > 0 ? argv[0] : "");
    /* FUN_08b46d74 -> FUN_08b46c48: runs the 26 static constructors in the table at
     * 0x0049BDF8, sorted by priority (all 0). They only set up font and text-style
     * globals (no imports) that nothing before the stop point reads; not written yet. */
    if (application_init() != 0)
        return 1;
    /* The CApplication object (0x00001C64), then its run loop (0x000024B0), which never
     * returns on the PSP. */
    main_loop();
    return 0;
}

/* ---- C runtime start-up ---- */

#define MAX_ARGS 20

/* user_main (0x0033642C), the SN Systems crt0 thread. */
static int user_main(SceSize args, void *argp)
{
    game_sbrk(0);

    /* argv: the NUL-separated strings of the argument block, at most 20. */
    char *argv[MAX_ARGS + 1];
    int argc = 0;
    char *p = argp;
    while (args > 0 && argc < MAX_ARGS && p < (char *)argp + args) {
        argv[argc++] = p;
        p += strlen(p) + 1;
    }
    argv[argc] = NULL;

    /* newlib's per-thread state (_REENT_INIT on the stack, pointed to by $k0+4), then
     * atexit(0x0034A5E8) and the empty _init (0x0034A5E0): the host C library already
     * has its own. */
    int status = game_main(argc, argv);
    g_startup_status = status;

    /* exit (0x0033CB68) would run the atexit handlers and unload the module; on the PSP
     * main never returns. */
    SDL_Log("[startup] main returned %d", status);
    return sceKernelExitDeleteThread(status);
}

int game_module_start(SceSize args, void *argp)
{
    sceKernelSetCompiledSdkVersion606(0x06060010);
    sceKernelSetCompilerVersion(0x00030306);
    SceUID th = sceKernelCreateThread("user_main", user_main, 0x20, 0x40000, 0x80000000, NULL);
    if (th >= 0)
        sceKernelStartThread(th, args, argp);
    return th < 0;
}
