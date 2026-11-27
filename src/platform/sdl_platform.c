/* SDL3 window and frame loop backing the PSP display. */
#include "psp_shim.h"
#include "ge.h"

#include <SDL3/SDL.h>

static SDL_Window *g_window;
static SDL_Renderer *g_renderer;
static SDL_Texture *g_screen;

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
    /* The frame buffer the game is displaying, or a dark placeholder with the screen
     * outline while the display is off. */
    static uint32_t pixels[PSP_SCREEN_W * PSP_SCREEN_H];
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    if (psp_display_snapshot(pixels, NULL)) {
        if (!g_screen)
            g_screen = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING,
                                         PSP_SCREEN_W, PSP_SCREEN_H);
        if (g_screen) {
            SDL_SetTextureScaleMode(g_screen, SDL_SCALEMODE_NEAREST);
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
    SDL_RenderPresent(g_renderer);
}

void psp_platform_shutdown(void)
{
    if (g_screen)
        SDL_DestroyTexture(g_screen);
    g_screen = NULL;
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(g_window);
    g_renderer = NULL;
    g_window = NULL;
    SDL_Quit();
}
