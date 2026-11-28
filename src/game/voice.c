/* See voice.h. The game plays voices through CSoundPlayerAT3 like the music; this is
 * the port's small player for them, on the shim's sceAtrac and sceAudio. */
#include "voice.h"

#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "fileio.h"
#include "psp_imports.h"
#include "psp_shim.h"

#define GRAIN 0x200
#define FRAME 2048
#define RING 0x2000

static struct {
    bool init;
    int channel;
    SDL_Thread *thread;
    SDL_AtomicInt running;
    SDL_Mutex *lock;
    /* the request (under lock): a loaded file to start, or a stop */
    uint8_t *pending_file;
    uint32_t pending_size;
    bool pending_stop;
    SDL_AtomicInt playing;
    /* the thread's own */
    uint8_t *file;
    int id, channels;
    bool finished;
    int16_t ring[2 * RING];
    int read, write, avail;
} g = {.channel = -1, .id = -1};

static void release(void)
{
    if (g.id >= 0)
        sceAtracReleaseAtracID(g.id);
    g.id = -1;
    free(g.file);
    g.file = NULL;
    g.avail = g.read = g.write = 0;
    SDL_SetAtomicInt(&g.playing, 0);
}

static void start(uint8_t *file, uint32_t size)
{
    release();
    g.file = file;
    g.id = sceAtracSetDataAndGetID(file, size);
    if (g.id < 0) {
        free(g.file);
        g.file = NULL;
        g.id = -1;
        return;
    }
    int end = 0, loop_start = -1, loop_end = -1;
    sceAtracGetSoundSample(g.id, &end, &loop_start, &loop_end);
    if (loop_start >= 0)
        sceAtracSetLoopNum(g.id, 0);
    /* The shim decodes to the file's channels: the voices are mono. */
    g.channels = file[0x16] == 2 ? 2 : 1;
    g.finished = false;
    SDL_SetAtomicInt(&g.playing, 1);
}

static void fill(void)
{
    static int16_t frame[2 * FRAME];
    while (!g.finished && g.avail < GRAIN) {
        int n = 0, finish = 0, remain = 0;
        if (sceAtracDecodeData(g.id, frame, &n, &finish, &remain) < 0) {
            g.finished = true;
            break;
        }
        for (int i = 0; i < n && g.avail < RING; i++) {
            int16_t l = frame[g.channels * i], r = frame[g.channels * i + g.channels - 1];
            g.ring[2 * g.write] = l;
            g.ring[2 * g.write + 1] = r;
            g.write = (g.write + 1) % RING;
            g.avail++;
        }
        if (finish)
            g.finished = true;
    }
}

static int SDLCALL voice_thread(void *arg)
{
    (void)arg;
    static int16_t grain[2 * GRAIN];
    while (SDL_GetAtomicInt(&g.running)) {
        SDL_LockMutex(g.lock);
        uint8_t *file = g.pending_file;
        uint32_t size = g.pending_size;
        bool stop = g.pending_stop;
        g.pending_file = NULL;
        g.pending_stop = false;
        SDL_UnlockMutex(g.lock);
        if (stop)
            release();
        if (file)
            start(file, size);
        if (g.id >= 0) {
            fill();
            int n = SDL_min(g.avail, GRAIN);
            for (int i = 0; i < n; i++) {
                grain[2 * i] = g.ring[2 * g.read];
                grain[2 * i + 1] = g.ring[2 * g.read + 1];
                g.read = (g.read + 1) % RING;
            }
            g.avail -= n;
            memset(grain + 2 * n, 0, (size_t)(GRAIN - n) * 4);
            sceAudioOutputPannedBlocking(g.channel, 0x8000, 0x8000, grain);
            if (g.finished && g.avail == 0)
                release();
        } else {
            memset(grain, 0, sizeof grain);
            sceAudioOutputPannedBlocking(g.channel, 0, 0, grain);
        }
    }
    release();
    return 0;
}

void voice_init(void)
{
    if (g.init)
        return;
    g.lock = SDL_CreateMutex();
    g.channel = sceAudioChReserve(-1, GRAIN, 0);
    if (g.channel >= 0) {
        SDL_SetAtomicInt(&g.running, 1);
        g.thread = SDL_CreateThread(voice_thread, "VoiceThread", NULL);
    }
    g.init = true;
}

void voice_shutdown(void)
{
    if (!g.init)
        return;
    SDL_SetAtomicInt(&g.running, 0);
    if (g.thread)
        SDL_WaitThread(g.thread, NULL);
    if (g.channel >= 0)
        sceAudioChRelease(g.channel);
    free(g.pending_file);
    SDL_DestroyMutex(g.lock);
    memset(&g, 0, sizeof g);
    g.channel = -1;
    g.id = -1;
}

bool voice_play(const char *path)
{
    if (!g.init || !g.thread || !path || !*path)
        return false;
    int32_t size = fileio_file_size(path);
    if (size < 0x40)
        return false;
    uint8_t *buf = malloc((size_t)size);
    if (!buf || fileio_read_file(path, buf, (uint32_t)size) != size || memcmp(buf, "RIFF", 4)) {
        free(buf);
        return false;
    }
    SDL_LockMutex(g.lock);
    free(g.pending_file);
    g.pending_file = buf;
    g.pending_size = (uint32_t)size;
    g.pending_stop = true;
    SDL_UnlockMutex(g.lock);
    SDL_SetAtomicInt(&g.playing, 1);
    return true;
}

void voice_stop(void)
{
    if (!g.init || !g.lock)
        return;
    SDL_LockMutex(g.lock);
    free(g.pending_file);
    g.pending_file = NULL;
    g.pending_stop = true;
    SDL_UnlockMutex(g.lock);
}

bool voice_playing(void)
{
    return g.init && SDL_GetAtomicInt(&g.playing) != 0;
}
