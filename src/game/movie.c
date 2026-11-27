/* See movie.h. The game's player, FUN_08b1d0ec:
 *   - opens disc0:/PSP_GAME/USRDIR/<name> (FUN_08b1ca28 / FUN_08b1cb08), reads the
 *     2048-byte PSMF header and seeks to the stream (FUN_08b1b2ec:
 *     sceMpegQueryStreamOffset / sceMpegQueryStreamSize);
 *   - sceMpegInit, a ring buffer of 0x3C0 packets filled by FUN_08b1b0d8 (whole
 *     packets read from the file), sceMpegCreate with a frame width of 512, the AVC
 *     stream and, when the movie has sound (the flag at 0x08ca7844, always set), the
 *     ATRAC stream on channel 0;
 *   - a picture queue of 4 buffers of 0x88000 bytes (FUN_08b1e274, 512x272 RGBA), an
 *     audio queue (FUN_08b1df28) and three threads: displayThread (FUN_08b1c5c4,
 *     shows a picture per vertical blank, repeating or dropping one when it is more
 *     than two pictures off the audio, FUN_08b1b7fc), readThread (FUN_08b1bef0,
 *     sceMpegRingbufferPut) and soundThread (FUN_08b1bba8, sceAudioOutputBlocking on
 *     channel 2 with 2048-sample buffers);
 *   - the loop (FUN_08b1dafc) waits until both queues are full (FUN_08b1b158), then
 *     decodes (FUN_08b1c8a0 / FUN_08b1c7e0) and polls the pad; at the end of the
 *     stream it drains the decoder (sceMpegAvcDecodeStop, FUN_08b1c984).
 *   - Pressing a skip button sets the state to 10 (0x08ca7854), flushes the decoder
 *     and plays SE 0xE at volume 1 once the threads are stopped.
 * A picture goes out with sceDmacMemcpy into one of two frame buffers at the start of
 * VRAM and sceDisplaySetFrameBuf (FUN_08b1c1e0).
 *
 * The port keeps the same queues and timing but does the reading, decoding and
 * showing on the game thread, one round per vertical blank; only the sound has its own
 * thread. Pictures are shown by the audio clock (the time stamp of the sample being
 * played), which makes the game's repeat/drop rule implicit. */
#include "movie.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "pad.h"
#include "psp_imports.h"
#include "psp_shim.h"
#include "sound.h"

#define RING_PACKETS 0x3C0
#define FRAME_WIDTH 0x200
#define PICTURE_BYTES 0x88000 /* 512 x 272 x 4 */
#define PICTURES 4
#define AUDIO_FRAMES 8
#define AUDIO_SAMPLES 2048
#define SE_SKIP 0xE

static bool g_skip_all;

void movie_set_skip_all(bool skip)
{
    g_skip_all = skip;
}

typedef struct Picture {
    uint8_t *buf;
    uint32_t pts;
} Picture;

typedef struct Player {
    int fd;
    SceMpegRingbuffer ring;
    void *ring_data, *mpeg_data;
    SceMpeg mpeg;
    SceMpegStream video, audio;
    void *es;
    SceMpegAu vau, aau;
    void *at3_es;

    Picture pic[PICTURES];
    int pic_head, pic_count;
    uint32_t last_pts;
    bool video_done;

    /* soundThread's queue. */
    SDL_Mutex *lock;
    SDL_Thread *thread;
    int16_t *pcm[AUDIO_FRAMES];
    uint32_t pcm_pts[AUDIO_FRAMES];
    int pcm_head, pcm_count;
    bool audio_done; /* nothing more will be queued */
    SDL_AtomicInt stop;
    SDL_AtomicInt played_end; /* time stamp just past the last buffer written */
    SDL_AtomicInt playing;    /* set once the first buffer went out */
    int channel;

    uint8_t *vram[2];
    int shown; /* which VRAM buffer is displayed */
} Player;

/* FUN_08b1b0d8: whole packets, or an error. */
static int32_t read_packets(void *data, int32_t packets, void *arg)
{
    Player *p = arg;
    int n = sceIoRead(p->fd, data, (SceSize)packets * 2048);
    if (n < 0)
        return n;
    return n % 2048 ? -1 : n / 2048;
}

/* soundThread (FUN_08b1bba8). */
static int sound_thread(void *arg)
{
    Player *p = arg;
    for (;;) {
        if (SDL_GetAtomicInt(&p->stop))
            break;
        SDL_LockMutex(p->lock);
        int16_t *buf = p->pcm_count ? p->pcm[p->pcm_head] : NULL;
        uint32_t pts = p->pcm_count ? p->pcm_pts[p->pcm_head] : 0;
        bool done = p->audio_done && !p->pcm_count;
        SDL_UnlockMutex(p->lock);
        if (done)
            break;
        if (!buf) {
            SDL_Delay(1);
            continue;
        }
        sceAudioOutputBlocking(p->channel, 0x8000, buf);
        SDL_SetAtomicInt(&p->played_end, (int)(pts + AUDIO_SAMPLES * 90000u / 44100u));
        SDL_SetAtomicInt(&p->playing, 1);
        SDL_LockMutex(p->lock);
        p->pcm_head = (p->pcm_head + 1) % AUDIO_FRAMES;
        p->pcm_count--;
        SDL_UnlockMutex(p->lock);
    }
    return 0;
}

/* The time stamp being heard: the end of what was written minus what the channel
 * still holds. */
static uint32_t audio_clock(Player *p)
{
    int rest = sceAudioGetChannelRestLength(p->channel);
    if (rest < 0)
        rest = 0;
    return (uint32_t)SDL_GetAtomicInt(&p->played_end) - (uint32_t)rest * 90000u / 44100u;
}

static void push_picture(Player *p, uint32_t pts)
{
    p->pic[(p->pic_head + p->pic_count) % PICTURES].pts = pts;
    p->pic_count++;
}

/* FUN_08b1c8a0 / FUN_08b1c984: one access unit into the next free picture buffer. */
static void decode_video(Player *p)
{
    if (p->video_done || p->pic_count >= PICTURES)
        return;
    void *buf = p->pic[(p->pic_head + p->pic_count) % PICTURES].buf;
    int32_t attr, init = 0;
    if (sceMpegGetAvcAu(&p->mpeg, p->video, &p->vau, &attr) == 0) {
        sceMpegAvcDecode(&p->mpeg, &p->vau, FRAME_WIDTH, &buf, &init);
        p->last_pts = p->vau.pts;
        if (init)
            push_picture(p, p->vau.pts);
    } else if (p->ring.eof && sceMpegRingbufferAvailableSize(&p->ring) == RING_PACKETS) {
        sceMpegAvcDecodeStop(&p->mpeg, FRAME_WIDTH, &buf, &init);
        if (init)
            push_picture(p, p->last_pts += 3003);
        else
            p->video_done = true;
    }
}

/* FUN_08b1c7e0: audio frames until the queue is full. */
static void decode_audio(Player *p)
{
    for (;;) {
        SDL_LockMutex(p->lock);
        bool full = p->pcm_count >= AUDIO_FRAMES || p->audio_done;
        int slot = (p->pcm_head + p->pcm_count) % AUDIO_FRAMES;
        SDL_UnlockMutex(p->lock);
        if (full)
            return;
        int32_t attr;
        if (sceMpegGetAtracAu(&p->mpeg, p->audio, &p->aau, &attr) != 0) {
            if (p->ring.eof && sceMpegRingbufferAvailableSize(&p->ring) == RING_PACKETS) {
                SDL_LockMutex(p->lock);
                p->audio_done = true;
                SDL_UnlockMutex(p->lock);
            }
            return;
        }
        sceMpegAtracDecode(&p->mpeg, &p->aau, p->pcm[slot], 1);
        SDL_LockMutex(p->lock);
        p->pcm_pts[slot] = p->aau.pts;
        p->pcm_count++;
        SDL_UnlockMutex(p->lock);
    }
}

/* FUN_08b1c1e0: into the VRAM buffer not on screen, then show it. */
static void show_picture(Player *p, const Picture *pic)
{
    int next = p->shown ^ 1;
    sceDmacMemcpy(p->vram[next], pic->buf, PICTURE_BYTES);
    sceDisplaySetFrameBuf(p->vram[next], FRAME_WIDTH, 3, 1);
    p->shown = next;
}

/* displayThread (FUN_08b1c5c4): the next picture once it is due, one per vertical
 * blank; more than two pictures behind (FUN_08b1b7fc), it drops to the newest due.
 * Before the sound starts the first picture waits. Returns the pictures dropped, -1
 * when none was shown. */
static int display(Player *p, uint32_t clock, bool have_clock)
{
    int due = -1;
    for (int i = 0; i < p->pic_count; i++) {
        const Picture *pic = &p->pic[(p->pic_head + i) % PICTURES];
        if (!have_clock || (int32_t)(clock - pic->pts) < 0)
            break;
        due = i;
    }
    if (due < 0)
        return -1;
    if (due < 2)
        due = 0;
    show_picture(p, &p->pic[(p->pic_head + due) % PICTURES]);
    p->pic_head = (p->pic_head + due + 1) % PICTURES;
    p->pic_count -= due + 1;
    return due;
}

static uint32_t skip_mask(int skip)
{
    if (skip == MOVIE_SKIP_START)
        return PSP_CTRL_START;
    return PSP_CTRL_START | PSP_CTRL_SELECT | PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_LEFT |
           PSP_CTRL_RIGHT | PSP_CTRL_UP | PSP_CTRL_DOWN | PSP_CTRL_CIRCLE | PSP_CTRL_CROSS |
           PSP_CTRL_TRIANGLE | PSP_CTRL_SQUARE;
}

static bool open_player(Player *p, const char *path)
{
    uint8_t header[2048];
    int32_t offset, size;
    p->fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (p->fd < 0)
        return false;
    if (sceIoRead(p->fd, header, sizeof header) != (int)sizeof header)
        return false;
    if (sceMpegInit() != 0)
        return false;
    int32_t ring_size = sceMpegRingbufferQueryMemSize(RING_PACKETS);
    int32_t mpeg_size = sceMpegQueryMemSize(0);
    p->ring_data = malloc((size_t)ring_size);
    p->mpeg_data = malloc((size_t)mpeg_size);
    if (!p->ring_data || !p->mpeg_data ||
        sceMpegRingbufferConstruct(&p->ring, RING_PACKETS, p->ring_data, ring_size, read_packets, p) != 0 ||
        sceMpegCreate(&p->mpeg, p->mpeg_data, mpeg_size, &p->ring, FRAME_WIDTH, 0, 0) != 0)
        return false;
    if (sceMpegQueryStreamOffset(&p->mpeg, header, &offset) != 0 || sceMpegQueryStreamSize(header, &size) != 0 ||
        sceIoLseek(p->fd, offset, PSP_SEEK_SET) != offset)
        return false;
    p->video = sceMpegRegistStream(&p->mpeg, SCE_MPEG_AVC_STREAM, 0);
    p->audio = sceMpegRegistStream(&p->mpeg, SCE_MPEG_ATRAC_STREAM, 0);
    p->es = sceMpegMallocAvcEsBuf(&p->mpeg);
    if (!p->video || !p->audio || !p->es || sceMpegInitAu(&p->mpeg, p->es, &p->vau) != 0)
        return false;
    int32_t es_size, out_size;
    if (sceMpegQueryAtracEsSize(&p->mpeg, &es_size, &out_size) != 0 || out_size != AUDIO_SAMPLES * 4)
        return false;
    p->at3_es = malloc((size_t)es_size);
    if (!p->at3_es || sceMpegInitAu(&p->mpeg, p->at3_es, &p->aau) != 0)
        return false;
    for (int i = 0; i < PICTURES; i++)
        if (!(p->pic[i].buf = malloc(PICTURE_BYTES)))
            return false;
    for (int i = 0; i < AUDIO_FRAMES; i++)
        if (!(p->pcm[i] = malloc((size_t)out_size)))
            return false;
    SDL_Log("[movie] %s: stream 0x%x bytes at 0x%x", path, (unsigned)size, (unsigned)offset);
    return true;
}

static void close_player(Player *p)
{
    if (p->thread) {
        SDL_SetAtomicInt(&p->stop, 1);
        SDL_WaitThread(p->thread, NULL);
    }
    if (p->channel >= 0)
        sceAudioChRelease(p->channel);
    if (p->lock)
        SDL_DestroyMutex(p->lock);
    if (p->mpeg) {
        if (p->video)
            sceMpegUnRegistStream(&p->mpeg, p->video);
        if (p->audio)
            sceMpegUnRegistStream(&p->mpeg, p->audio);
        if (p->es)
            sceMpegFreeAvcEsBuf(&p->mpeg, p->es);
        sceMpegDelete(&p->mpeg);
    }
    sceMpegRingbufferDestruct(&p->ring);
    sceMpegFinish();
    for (int i = 0; i < PICTURES; i++)
        free(p->pic[i].buf);
    for (int i = 0; i < AUDIO_FRAMES; i++)
        free(p->pcm[i]);
    free(p->at3_es);
    free(p->ring_data);
    free(p->mpeg_data);
    if (p->fd >= 0)
        sceIoClose(p->fd);
}

bool movie_play(const char *name, int skip)
{
    if (g_skip_all) {
        SDL_Log("[movie] %s skipped (--skip-movie)", name);
        return false;
    }
    char path[256];
    snprintf(path, sizeof path, "disc0:/PSP_GAME/USRDIR/%s", name);
    Player *p = calloc(1, sizeof *p);
    if (!p)
        return false;
    p->fd = -1;
    p->channel = -1;
    if (!open_player(p, path)) {
        SDL_Log("[movie] %s can't be played", path);
        close_player(p);
        free(p);
        return false;
    }

    /* FUN_08b1c250: the display waits for the first picture. */
    sceDisplaySetMode(0, PSP_SCREEN_W, PSP_SCREEN_H);
    sceDisplaySetFrameBuf(NULL, FRAME_WIDTH, 3, 1);
    p->vram[0] = sceGeEdramGetAddr();
    p->vram[1] = p->vram[0] + PICTURE_BYTES;
    p->shown = 1;

    p->lock = SDL_CreateMutex();
    p->channel = sceAudioChReserve(-1, AUDIO_SAMPLES, 0);

    bool started = false, skipped = false;
    uint32_t mask = skip_mask(skip);
    uint64_t t0 = 0; /* without a channel: the clock is the time since the start */
    uint32_t pts0 = 0;
    int shown = 0, dropped = 0;
    for (;;) {
        sceMpegRingbufferPut(&p->ring, RING_PACKETS, sceMpegRingbufferAvailableSize(&p->ring));
        decode_audio(p);
        decode_video(p);
        SDL_LockMutex(p->lock);
        bool audio_full = p->pcm_count >= AUDIO_FRAMES || p->audio_done;
        bool audio_idle = p->audio_done && !p->pcm_count;
        SDL_UnlockMutex(p->lock);
        /* FUN_08b1b158: start once both queues are full. */
        if (!started && audio_full && (p->pic_count >= PICTURES || p->video_done)) {
            started = true;
            if (p->channel >= 0)
                p->thread = SDL_CreateThread(sound_thread, "soundThread", p);
            t0 = SDL_GetTicksNS();
            pts0 = p->pic_count ? p->pic[p->pic_head].pts : 0;
        }
        if (started) {
            pad_update(1);
            if (pad_pressed(mask)) {
                skipped = true;
                sceMpegAvcDecodeFlush(&p->mpeg);
                break;
            }
            int d = p->thread ? display(p, audio_clock(p), SDL_GetAtomicInt(&p->playing) != 0)
                              : display(p, pts0 + (uint32_t)((SDL_GetTicksNS() - t0) * 9 / 100000), true);
            if (d >= 0) {
                shown++;
                dropped += d;
            }
            if (p->video_done && !p->pic_count && (audio_idle || !p->thread))
                break;
        }
        sceDisplayWaitVblankStartCB();
    }
    if (!skipped)
        while (p->channel >= 0 && sceAudioGetChannelRestLength(p->channel) > 0)
            sceDisplayWaitVblankStartCB();
    SDL_Log("[movie] %s %s after %d pictures (%d dropped), %.2f s", name, skipped ? "skipped" : "ended",
            shown, dropped, t0 ? (double)(SDL_GetTicksNS() - t0) / 1e9 : 0.0);
    close_player(p);
    free(p);
    if (skipped)
        sound_se(SE_SKIP, 1.0f);
    return skipped;
}
