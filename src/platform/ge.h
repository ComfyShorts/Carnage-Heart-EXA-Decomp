/* GE (graphics engine) emulation shared between ge.c, display.c, gu.c and the SDL layer.
 *
 * Display lists are executed synchronously by a software rasterizer as soon as they
 * are enqueued (and again each time the stall address moves). Video memory is a 2 MiB
 * host buffer.
 *
 * Host addressing: the PSP GE takes 28-bit physical addresses. This port runs in a
 * 32-bit process and its lists carry host pointers, so BASE (0x10), the TBWn/CBPH/
 * TRXSBW/TRXDBW high bytes and the list calls use 8 high address bits instead of 4.
 * Addresses below 2 MiB, or in 0x04000000-0x041FFFFF, mean video memory. Lists are
 * produced by this port's own libgu (gu.c), which follows that rule. */
#ifndef PSP_GE_H
#define PSP_GE_H

#include <stdbool.h>
#include <stdint.h>

#define PSP_EDRAM_SIZE 0x200000

/* sceGeSetCallback's argument. */
typedef struct PspGeCallbackData {
    void (*signal_func)(int id, void *arg);
    void *signal_arg;
    void (*finish_func)(int id, void *arg);
    void *finish_arg;
} PspGeCallbackData;

uint8_t *psp_ge_vram(void);
/* A GE address (see above) as a host pointer. */
uint8_t *psp_ge_ptr(uint32_t addr);

/* Primitives drawn so far into the frame buffer at VRAM offset fb_offset. */
uint32_t psp_ge_prims_into(uint32_t fb_offset);

/* Log every GE command executed (with --gu-trace). */
void psp_ge_set_trace(bool on);

/* Display (display.c). Converts the displayed frame to RGBA8888 (R in the low byte),
 * 480x272. Returns false while the display is off. */
bool psp_display_snapshot(uint32_t *rgba, uint32_t *frame_number);
uint32_t psp_display_frame_count(void);
/* Save the first frame the GE drew into, once it is shown, as PNG. */
void psp_display_request_screenshot(const char *path);

#endif
