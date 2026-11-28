/* SDL3 window and frame loop backing the PSP display. */
#include "psp_shim.h"
#include "ge.h"

#include <stdio.h>

#include <SDL3/SDL.h>

static SDL_Window *g_window;
static char g_title[128];
static SDL_Renderer *g_renderer;
static SDL_Texture *g_screen;

bool psp_platform_init(const char *title, int scale)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_Log("[platform] SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    /* Gamepads: already connected ones arrive as SDL_EVENT_GAMEPAD_ADDED too. */
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
        SDL_Log("[platform] no gamepad support: %s", SDL_GetError());
    snprintf(g_title, sizeof g_title, "%s", title);
    psp_help_init();
    psp_controls_log();
    if (scale < 1)
        scale = 1;
    if (!SDL_CreateWindowAndRenderer(title, PSP_SCREEN_W * scale, PSP_SCREEN_H * scale,
                                     SDL_WINDOW_RESIZABLE, &g_window, &g_renderer)) {
        SDL_Log("[platform] window creation failed: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }
    /* Keep the PSP's 480x272 coordinate space whatever the window size. */
    SDL_SetRenderLogicalPresentation(g_renderer, PSP_SCREEN_W, PSP_SCREEN_H,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderVSync(g_renderer, 1);
    SDL_Log("[platform] SDL %d.%d.%d, video driver %s, renderer %s, window %dx%d",
            SDL_VERSIONNUM_MAJOR(SDL_GetVersion()), SDL_VERSIONNUM_MINOR(SDL_GetVersion()),
            SDL_VERSIONNUM_MICRO(SDL_GetVersion()), SDL_GetCurrentVideoDriver(),
            SDL_GetRendererName(g_renderer), PSP_SCREEN_W * scale, PSP_SCREEN_H * scale);
    return true;
}

bool psp_platform_pump(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_EVENT_QUIT)
            return false;
        if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE)
            return false;
        if ((ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_KEY_UP) && !ev.key.repeat)
            psp_ctrl_key_event((int)ev.key.scancode, ev.type == SDL_EVENT_KEY_DOWN);
        if (ev.type == SDL_EVENT_WINDOW_FOCUS_LOST)
            psp_ctrl_release_all(); /* no key stays held while another window has them */
        if (ev.type == SDL_EVENT_GAMEPAD_ADDED)
            psp_ctrl_pad_added(ev.gdevice.which);
        if (ev.type == SDL_EVENT_GAMEPAD_REMOVED)
            psp_ctrl_pad_removed(ev.gdevice.which);
    }
    psp_ctrl_pads_poll();
    /* The keys for the buttons the screen's help text names, in the title bar. */
    char hint[256], title[400];
    if (psp_help_hint(hint, sizeof hint) && g_window) {
        if (hint[0])
            snprintf(title, sizeof title, "%s  -  %s", g_title, hint);
        else
            snprintf(title, sizeof title, "%s", g_title);
        SDL_SetWindowTitle(g_window, title);
        if (hint[0])
            SDL_Log("[input] this screen's help names: %s", hint);
    }
    return true;
}

void psp_platform_present(void)
{
    /* The frame buffer the game is displaying, or a dark placeholder with the screen
     * outline while the display is off. */
    static uint32_t pixels[PSP_SCREEN_W * PSP_SCREEN_H];
    uint64_t t0 = PERF_BEGIN();
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    if (psp_display_snapshot(pixels, NULL)) {
        if (!g_screen) {
            g_screen = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING,
                                         PSP_SCREEN_W, PSP_SCREEN_H);
            /* The PSP's display ignores the frame buffer's alpha (it holds the stencil),
             * so copy the picture without blending: correct, and the renderer's
             * cheapest path. */
            if (g_screen) {
                SDL_SetTextureScaleMode(g_screen, SDL_SCALEMODE_NEAREST);
                SDL_SetTextureBlendMode(g_screen, SDL_BLENDMODE_NONE);
            }
        }
        if (g_screen) {
            SDL_UpdateTexture(g_screen, NULL, pixels, PSP_SCREEN_W * 4);
            SDL_RenderTexture(g_renderer, g_screen, NULL, NULL);
        }
    } else {
        SDL_SetRenderDrawColor(g_renderer, 24, 32, 48, 255);
        SDL_FRect screen = {0, 0, PSP_SCREEN_W, PSP_SCREEN_H};
        SDL_RenderFillRect(g_renderer, &screen);
        SDL_SetRenderDrawColor(g_renderer, 90, 120, 170, 255);
        SDL_RenderRect(g_renderer, &screen);
    }
    uint64_t t1 = PERF_BEGIN();
    SDL_RenderPresent(g_renderer);
    if (t0)
        psp_perf_present(t1 - t0, psp_perf_now() - t1);
}

void psp_platform_shutdown(void)
{
    psp_audio_shutdown();
    psp_ctrl_pads_close();
    if (g_screen)
        SDL_DestroyTexture(g_screen);
    g_screen = NULL;
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(g_window);
    g_renderer = NULL;
    g_window = NULL;
    SDL_Quit();
}
