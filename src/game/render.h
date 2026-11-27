/* CRender / CGraphics (singleton at DAT_08c8d338, 0x520 bytes): builds one GU packet
 * per frame and shows the frames on the display. Only what the start-up path and the
 * main loop's frame end use; the render list (objects drawn into the packet) is kept
 * but nothing registers with it yet. */
#ifndef GAME_RENDER_H
#define GAME_RENDER_H

#include <stdint.h>

/* CClear (+0x4F8): what the GU clears at the start of each packet. */
typedef struct RenderClear {
    uint32_t color;    /* ABGR */
    int flags;         /* sceGuClear flags */
    uint32_t depth;
    uint32_t stencil;
} RenderClear;

typedef struct Render {
    void *kick_list;       /* +0x4D0: 64-byte direct list that calls the packet */
    void *packet[2];       /* +0x4D4: double-buffered 512 KiB packet buffers */
    int packet_size;       /* +0x4F4 */
    int frame;             /* +0x4DC: which packet is being built, 0/1 */
    int vblanks;           /* +0x4E0: vertical blanks the last frame took */
    int step;              /* +0x4E4: the same, capped at max_step */
    int interval;          /* +0x4E8: minimum vertical blanks per frame */
    int max_step;          /* +0x4EC */
    int last_vcount;       /* +0x4F0 */
    RenderClear clear;     /* +0x4F8 */
    uint32_t frame_count;  /* +0x508 */
    int paused;            /* +0x50C: suspends frame output (utility dialogs) */
    int display_delay;     /* +0x510: frames until sceGuDisplay(1) */
    void *matrix_stack;    /* +0xD0: 16 matrices for libgum (inside the object in the game) */
} Render;

/* CGraphics::GetInstance (FUN_08b205c0): creates the object on first use. */
Render *render_instance(void);

/* FUN_08b1fe38: GU initialisation (sceGuInit). */
void render_gu_init(Render *r);

/* FUN_08b1fe70, from CApplication::Start: frame buffers, depth buffer, matrix stack,
 * and the display switched on when on != 0. */
void render_setup_display(Render *r, int on);

/* FUN_08b2027c: the clear colour; black also stops colour clears. */
void render_set_clear_color(Render *r, uint32_t color);

/* FUN_08b200ec, once per main-loop iteration: build and send this frame's packet,
 * wait for the vertical blank and swap buffers. */
void render_end_frame(Render *r);

/* FUN_08b20568: vertical blanks per frame (the main loop's time step). */
int render_interval(const Render *r);

#endif
