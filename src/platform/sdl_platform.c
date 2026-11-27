/* SDL3 window and frame loop backing the PSP display. */
#include "psp_shim.h"

#include <SDL3/SDL.h>

static SDL_Window *g_window;
static SDL_Renderer *g_renderer;

bool psp_platform_init(const char *title, int scale)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_Log("[platform] SDL_Init failed: %s", SDL_GetError());
        return false;
    }
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
    }
    return true;
}

void psp_platform_present(void)
{
    /* Placeholder frame until the GE is emulated: a plain background and a border
     * showing the 480x272 PSP screen area. */
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    SDL_SetRenderDrawColor(g_renderer, 24, 32, 48, 255);
    SDL_FRect screen = {0, 0, PSP_SCREEN_W, PSP_SCREEN_H};
    SDL_RenderFillRect(g_renderer, &screen);
    SDL_SetRenderDrawColor(g_renderer, 90, 120, 170, 255);
    SDL_RenderRect(g_renderer, &screen);
    SDL_RenderPresent(g_renderer);
}

void psp_platform_shutdown(void)
{
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(g_window);
    g_renderer = NULL;
    g_window = NULL;
    SDL_Quit();
}
