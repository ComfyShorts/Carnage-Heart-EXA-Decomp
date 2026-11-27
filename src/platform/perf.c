/* --perf: where a frame's time goes. See psp_shim.h and docs/graphics.md
 * ("Performance").
 *
 * The game thread runs the frame: the screens' update (logic, with the chip
 * interpreter inside it in a battle), the render list's drawing into a GU display list
 * (build), the wait for the vertical blank, then the GE running that list (commands
 * and vertex transform, and the rasterizer inside it). The main thread presents the
 * displayed frame buffer to the window. Each thread sums its own parts and logs them
 * every PERF_WINDOW frames, as milliseconds per frame. */
#include "psp_shim.h"
#include "ge.h"

#include <SDL3/SDL.h>

#include <time.h>

#define PERF_WINDOW 120

bool g_psp_perf;

static const char *const k_names[PERF_COUNT] = {
    [PERF_LOGIC] = "logic", [PERF_CHIPS] = "chips",   [PERF_BUILD] = "build",
    [PERF_GE] = "GE",       [PERF_RASTER] = "raster", [PERF_WAIT] = "vblank wait",
};

static struct {
    uint64_t acc[PERF_COUNT];
    uint64_t last, sum, max;
    int frames, late;
    uint32_t prims0, pixels0;
    uint64_t busy_last, busy_max; /* work in the frame: logic + build + GE */
    uint64_t raster_last;
    uint32_t prims_last, pixels_last;
    struct { uint64_t busy, raster, cpu; uint32_t prims, pixels, frame; } worst;
    uint32_t frame_no;
    uint64_t cpu_last;
} g;

static struct {
    uint64_t work, wait, last, sum, max;
    int frames;
} g_main;

uint64_t psp_perf_now(void)
{
    return SDL_GetTicksNS();
}

void psp_perf_enable(bool on)
{
    g_psp_perf = on;
}

void psp_perf_end(int part, uint64_t t0)
{
    if (t0 && part >= 0 && part < PERF_COUNT)
        g.acc[part] += SDL_GetTicksNS() - t0;
}

/* The game thread's own CPU time: wall time minus the time it wasn't running. */
static uint64_t thread_cpu_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

static double ms(uint64_t ns, int frames)
{
    return frames ? (double)ns / 1e6 / frames : 0.0;
}

void psp_perf_frame(int vblanks, int interval)
{
    if (!g_psp_perf)
        return;
    uint64_t now = SDL_GetTicksNS();
    if (g.last) {
        uint64_t d = now - g.last;
        g.sum += d;
        if (d > g.max)
            g.max = d;
        g.frames++;
        g.late += vblanks > interval;
    }
    g.last = now;
    uint64_t busy_now = g.acc[PERF_LOGIC] + g.acc[PERF_BUILD] + g.acc[PERF_GE];
    uint32_t prims_now = psp_ge_stats_prims(), pixels_now = psp_ge_stats_pixels();
    uint64_t cpu_now = thread_cpu_ns();
    g.frame_no++;
    if (busy_now - g.busy_last > g.busy_max) {
        g.busy_max = busy_now - g.busy_last;
        g.worst.busy = g.busy_max;
        g.worst.raster = g.acc[PERF_RASTER] - g.raster_last;
        g.worst.prims = prims_now - g.prims_last;
        g.worst.pixels = pixels_now - g.pixels_last;
        g.worst.frame = g.frame_no;
        g.worst.cpu = g.cpu_last ? cpu_now - g.cpu_last : 0;
    }
    g.cpu_last = cpu_now;
    g.busy_last = busy_now;
    g.raster_last = g.acc[PERF_RASTER];
    g.prims_last = prims_now;
    g.pixels_last = pixels_now;
    if (g.frames < PERF_WINDOW)
        return;
    int n = g.frames;
    uint32_t prims = psp_ge_stats_prims(), pixels = psp_ge_stats_pixels();
    uint64_t busy = g.acc[PERF_LOGIC] + g.acc[PERF_BUILD] + g.acc[PERF_GE];
    SDL_Log("[perf] game: %.1f fps, frame %.2f ms avg / %.2f max, %d late | busy %.2f ms (max %.2f): logic %.2f "
            "(chips %.2f), build %.2f, GE %.2f (commands %.2f, raster %.2f) | %s %.2f | %u prims, %uk pixels",
            n * 1e9 / (double)g.sum, ms(g.sum, n), (double)g.max / 1e6, g.late, ms(busy, n), (double)g.busy_max / 1e6,
            ms(g.acc[PERF_LOGIC], n), ms(g.acc[PERF_CHIPS], n), ms(g.acc[PERF_BUILD], n),
            ms(g.acc[PERF_GE], n), ms(g.acc[PERF_GE] - g.acc[PERF_RASTER], n), ms(g.acc[PERF_RASTER], n),
            k_names[PERF_WAIT], ms(g.acc[PERF_WAIT], n), (prims - g.prims0) / (unsigned)n,
            (pixels - g.pixels0) / (unsigned)n / 1000);
    if (g.worst.busy > 14000000)
        SDL_Log("[perf] slowest frame (%u since --perf started): busy %.2f ms, raster %.2f, %u prims, %uk pixels; "
                "the game thread ran %.2f ms of the frame",
                (unsigned)g.worst.frame, (double)g.worst.busy / 1e6, (double)g.worst.raster / 1e6,
                (unsigned)g.worst.prims, (unsigned)g.worst.pixels / 1000, (double)g.worst.cpu / 1e6);
    uint32_t frame_no = g.frame_no;
    uint64_t cpu_last = g.cpu_last;
    SDL_memset(&g, 0, sizeof g);
    g.cpu_last = cpu_last;
    g.last = now;
    g.prims0 = g.prims_last = prims;
    g.pixels0 = g.pixels_last = pixels;
    g.frame_no = frame_no;
}

void psp_perf_present(uint64_t work_ns, uint64_t wait_ns)
{
    if (!g_psp_perf)
        return;
    uint64_t now = SDL_GetTicksNS();
    if (g_main.last) {
        uint64_t d = now - g_main.last;
        g_main.sum += d;
        if (d > g_main.max)
            g_main.max = d;
        g_main.frames++;
        g_main.work += work_ns;
        g_main.wait += wait_ns;
    }
    g_main.last = now;
    if (g_main.frames < PERF_WINDOW)
        return;
    int n = g_main.frames;
    SDL_Log("[perf] window: %.1f presents/s, %.2f ms avg / %.2f max | present %.2f ms, swap wait %.2f ms",
            n * 1e9 / (double)g_main.sum, ms(g_main.sum, n), (double)g_main.max / 1e6, ms(g_main.work, n),
            ms(g_main.wait, n));
    SDL_memset(&g_main, 0, sizeof g_main);
    g_main.last = now;
}
