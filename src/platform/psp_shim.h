/* PSP shim core: stub logging and the SDL3 platform layer. */
#ifndef PSP_SHIM_H
#define PSP_SHIM_H

#include <stdbool.h>
#include <stddef.h>
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

/* Log a call to an implemented import: its arguments (printf-style) and result.
 * The first call of each import is always logged; every call when tracing is on. */
void psp_trace(const char *name, int64_t result, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
#define PSP_TRACE(result, ...) psp_trace(__func__, (int64_t)(result), __VA_ARGS__)
/* The same for imports that return a pointer. */
void psp_trace_ptr(const char *name, const void *result, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
#define PSP_TRACE_PTR(result, ...) psp_trace_ptr(__func__, (result), __VA_ARGS__)
void psp_trace_enable(bool on);

/* Prints every import called so far (implemented and stubs), in first-call order. */
void psp_import_report(void);

/* File system (files.c): serve PSP device `device` (without the colon, e.g. "disc0")
 * from a host directory. disc0: is the disc root, the folder that holds PSP_GAME/. */
bool psp_io_mount(const char *device, const char *host_dir, bool writable);
/* Mount disc0: from either the disc root or its PSP_GAME/USRDIR folder. */
bool psp_io_mount_disc(const char *dir);

/* Save data (savedata.c): the host folder that stands for the memory stick's
 * PSP/SAVEDATA, one directory per save. Default "saves". */
void psp_savedata_set_root(const char *dir);
const char *psp_savedata_root(void);

/* Audio (audio.c): --mute keeps the sound code running without a device. */
void psp_audio_set_mute(bool mute);
bool psp_audio_muted(void);
void psp_audio_shutdown(void); /* psp_platform_shutdown: before SDL goes */
/* --audio-driver: SDL's driver to use ("pulseaudio", "pipewire", "alsa", "dummy"...),
 * set before the device opens; NULL or "" lets SDL choose. */
void psp_audio_set_driver(const char *name);
const char *psp_audio_driver(void); /* the driver in use, NULL before the device opens */
void psp_audio_open(void);          /* open (and log) the device now rather than on first use */

/* Kernel (kernel.c). */
int psp_kernel_threads_alive(void);  /* PSP threads currently running */
void psp_kernel_request_exit(void);  /* the user quit: notify the game's exit callback */
bool psp_kernel_exit_called(void);   /* the game called sceKernelExitGame */
int psp_kernel_run_callbacks(void);  /* run the calling thread's notified callbacks (*CB waits) */

/* Platform layer (sdl_platform.c). The PSP screen is 480x272. */
/* Controller (input.c). Button bits as in sceCtrl. */
enum {
    PSP_CTRL_SELECT = 0x1, PSP_CTRL_START = 0x8, PSP_CTRL_UP = 0x10, PSP_CTRL_RIGHT = 0x20,
    PSP_CTRL_DOWN = 0x40, PSP_CTRL_LEFT = 0x80, PSP_CTRL_LTRIGGER = 0x100, PSP_CTRL_RTRIGGER = 0x200,
    PSP_CTRL_TRIANGLE = 0x1000, PSP_CTRL_CIRCLE = 0x2000, PSP_CTRL_CROSS = 0x4000, PSP_CTRL_SQUARE = 0x8000,
};
typedef struct SceCtrlData {
    uint32_t timestamp;
    uint32_t buttons;
    uint8_t lx, ly;
    uint8_t reserved[6];
} SceCtrlData;
void psp_ctrl_key_event(int keycode, bool down);         /* from the SDL event pump */
void psp_ctrl_script_press(uint32_t frame, uint32_t buttons); /* press on a displayed frame */
uint32_t psp_display_frame_count(void);

#define PSP_SCREEN_W 480
#define PSP_SCREEN_H 272

bool psp_platform_init(const char *title, int scale);
bool psp_platform_pump(void);   /* handle window events; false once the user quits */
void psp_platform_present(void);
void psp_platform_shutdown(void);

/* --perf (perf.c): per-frame timings, logged every 120 frames. PERF_BEGIN returns 0
 * when it is off, and psp_perf_end ignores a 0 start. */
enum { PERF_LOGIC, PERF_CHIPS, PERF_BUILD, PERF_GE, PERF_RASTER, PERF_WAIT, PERF_COUNT };
extern bool g_psp_perf;
uint64_t psp_perf_now(void);
#define PERF_BEGIN() (g_psp_perf ? psp_perf_now() : 0)
void psp_perf_enable(bool on);
void psp_perf_end(int part, uint64_t t0);
void psp_perf_frame(int vblanks, int interval); /* game thread, after each swap */
void psp_perf_present(uint64_t work_ns, uint64_t wait_ns); /* main thread, each present */

#endif
