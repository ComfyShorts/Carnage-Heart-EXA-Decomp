/* Call log for PSP imports: unimplemented stubs and implemented imports alike.
 *
 * Every import's first call is logged and remembered in order, so the sequence of
 * imports a code path reaches can be printed afterwards (psp_import_report). With
 * tracing on (psp_trace_enable, or PSP_SHIM_LOG=all) every call is logged. */
#include "psp_shim.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#define MAX_SEEN 320

typedef struct Seen {
    const char *name;
    uint32_t calls;
    bool stub;
} Seen;

static Seen g_seen[MAX_SEEN];
static int g_seen_count;
static SDL_SpinLock g_lock;
static int g_trace = -1;

void psp_trace_enable(bool on)
{
    g_trace = on;
}

static bool tracing(void)
{
    if (g_trace < 0) {
        const char *env = getenv("PSP_SHIM_LOG");
        g_trace = env && strcmp(env, "all") == 0;
    }
    return g_trace;
}

/* Count a call; returns the call number (1 = first). */
static uint32_t note_call(const char *name, bool stub)
{
    uint32_t n = 0;
    SDL_LockSpinlock(&g_lock);
    for (int i = 0; i < g_seen_count; i++)
        if (g_seen[i].name == name || strcmp(g_seen[i].name, name) == 0) {
            n = ++g_seen[i].calls;
            break;
        }
    if (n == 0 && g_seen_count < MAX_SEEN) {
        g_seen[g_seen_count++] = (Seen){name, 1, stub};
        n = 1;
    }
    SDL_UnlockSpinlock(&g_lock);
    return n;
}

void psp_stub_hit(PspStubSite *site)
{
    uint32_t n = note_call(site->name, true);
    site->calls = n;
    if (n == 1 || tracing())
        SDL_Log("[import] %s() STUB (%s, NID 0x%08X) call #%u", site->name, site->library,
                (unsigned)site->nid, (unsigned)n);
}

static void vtrace(const char *name, const char *result, const char *fmt, va_list ap)
{
    uint32_t n = note_call(name, false);
    if (n != 1 && !tracing())
        return;
    char args[256] = "";
    if (fmt)
        vsnprintf(args, sizeof args, fmt, ap);
    SDL_Log("[import] %s(%s) -> %s", name, args, result);
}

void psp_trace(const char *name, int64_t result, const char *fmt, ...)
{
    char res[32];
    if (result < 0 && result >= INT32_MIN)
        snprintf(res, sizeof res, "0x%08X", (unsigned)(int32_t)result);
    else
        snprintf(res, sizeof res, "%lld", (long long)result);
    va_list ap;
    va_start(ap, fmt);
    vtrace(name, res, fmt, ap);
    va_end(ap);
}

void psp_trace_ptr(const char *name, const void *result, const char *fmt, ...)
{
    char res[32];
    snprintf(res, sizeof res, "%p", result);
    va_list ap;
    va_start(ap, fmt);
    vtrace(name, res, fmt, ap);
    va_end(ap);
}

void psp_import_report(void)
{
    SDL_LockSpinlock(&g_lock);
    int stubs = 0;
    for (int i = 0; i < g_seen_count; i++)
        stubs += g_seen[i].stub;
    SDL_Log("[import] %d distinct imports called (%d implemented, %d still stubs), in first-call order:",
            g_seen_count, g_seen_count - stubs, stubs);
    for (int i = 0; i < g_seen_count; i++)
        SDL_Log("[import] %3d. %-40s %6u call%s%s", i + 1, g_seen[i].name, (unsigned)g_seen[i].calls,
                g_seen[i].calls == 1 ? "" : "s", g_seen[i].stub ? "  (stub)" : "");
    SDL_UnlockSpinlock(&g_lock);
}
