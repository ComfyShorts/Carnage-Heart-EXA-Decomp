/* PSP shim core: stub logging and the SDL3 platform layer. */
#ifndef PSP_SHIM_H
#define PSP_SHIM_H

#include <stdbool.h>
#include <stdint.h>

/* Per-stub call record. Each PSP_STUB site owns one, registered on its first call. */
typedef struct PspStubSite {
    const char *library;
    const char *name;
    uint32_t nid;
    uint32_t calls;
    struct PspStubSite *next;
} PspStubSite;

void psp_stub_hit(PspStubSite *site);

/* Logs the first call of an unimplemented import (every call with PSP_SHIM_LOG=all). */
#define PSP_STUB(lib, nid_)                                                   \
    do {                                                                      \
        static PspStubSite psp_site_ = {(lib), __func__, (nid_), 0, 0};       \
        psp_stub_hit(&psp_site_);                                             \
    } while (0)

/* Prints how often each stub was called. */
void psp_stub_report(void);

/* Platform layer (sdl_platform.c). The PSP screen is 480x272. */
#define PSP_SCREEN_W 480
#define PSP_SCREEN_H 272

bool psp_platform_init(const char *title, int scale);
bool psp_platform_pump(void);   /* handle window events; false once the user quits */
void psp_platform_present(void);
void psp_platform_shutdown(void);

#endif
