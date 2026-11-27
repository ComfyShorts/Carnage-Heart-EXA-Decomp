/* Logging for unimplemented PSP imports. */
#include "psp_shim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

static PspStubSite *g_sites;
static SDL_SpinLock g_lock;
static int g_log_all = -1;

void psp_stub_hit(PspStubSite *site)
{
    if (g_log_all < 0) {
        const char *env = getenv("PSP_SHIM_LOG");
        g_log_all = env && strcmp(env, "all") == 0;
    }
    SDL_LockSpinlock(&g_lock);
    uint32_t n = ++site->calls;
    if (n == 1) {
        site->next = g_sites;
        g_sites = site;
    }
    SDL_UnlockSpinlock(&g_lock);
    if (n == 1 || g_log_all)
        SDL_Log("[shim] stub %s::%s (NID 0x%08X) call #%u", site->library, site->name,
                (unsigned)site->nid, (unsigned)n);
}

void psp_stub_report(void)
{
    int count = 0;
    SDL_LockSpinlock(&g_lock);
    for (PspStubSite *s = g_sites; s; s = s->next)
        count++;
    SDL_Log("[shim] %d distinct stubs called", count);
    for (PspStubSite *s = g_sites; s; s = s->next)
        SDL_Log("[shim]   %-22s %-40s %u", s->library, s->name, (unsigned)s->calls);
    SDL_UnlockSpinlock(&g_lock);
}
