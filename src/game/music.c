/* See music.h and docs/screens.md ("Music"). Hand-written from:
 *   CSoundDriver::strInit FUN_08b2d82c: sceAtracReinit(0, 3), three players
 *     (CSoundPlayerSTR), each with its own channel;
 *   CSoundPlayerAT3 init FUN_08b2dbf0: sceAtracSetDataAndGetID, IsSecondBufferNeeded,
 *     GetSoundSample, SetLoopNum(-1, or 0 when told to play once) when the file has
 *     a loop, GetRemainFrame;
 *   its decode FUN_08b2ddbc: DecodeData into a ring of 0x1800 stereo samples until a
 *     grain (0x200) is ready, the end flag set once the decoder says it finished;
 *   its output FUN_08b2e120: a grain at a time with sceAudioOutputPannedBlocking
 *     (FUN_08b53d10), the tail padded with silence (FUN_08b2e0a8);
 *   the BGM table (TblBGMOpen FUN_08b27638, rows of 0x34 bytes, FUN_08b277e0):
 *     +0x00 title, +0x20 name ("BGM_36"), +0x30 file number (sound/bgm/bgm%02d.at3,
 *     -1 none), +0x32 volume (0-100; FUN_08b35410 makes it a 0-1 factor);
 *   CSound::BGMPlay FUN_08b34de8 (row, picker, loop) and CBgmPlayer: FUN_0888c718
 *     (the title, row 0x24), FUN_0888d2ac (menus), FUN_0888dfb0 (battles), the lists
 *     from FUN_0888cbd4 / FUN_0888cf44.
 * The game reads the file in pieces as it plays (CSoundPlayerSTR's streaming thread);
 * here the whole file (about 450 KB) is read when the track starts, which the decoder
 * library handles as its no-streaming case. The fades and the system data's BGM volume
 * are the port's: what plays fades out over half a second, the next starts at its
 * row's volume. Story Mode's choices (by story progress) aren't ported. */
#include "music.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "fileio.h"
#include "psp_imports.h"
#include "psp_shim.h"
#include "savedata.h"

#define GRAIN 0x200
#define RING 0x2000 /* stereo samples: the game's 0x1800 plus a decoded frame */
#define FRAME 2048
#define BGM_ROW 0x34
#define BGM_ROWS 0x37 /* the pickers look at rows 0..0x36 */
#define FADE_GRAINS 43 /* about half a second */

typedef struct Player {
    bool active, finished;
    uint8_t *file;
    int id, channels;
    int16_t ring[2 * RING];
    int read, write, avail;
    float vol, target, step;
    int row;
} Player;

static struct {
    bool init;
    uint8_t *table;
    int rows;
    int channel;
    SDL_Thread *thread;
    SDL_AtomicInt running;
    SDL_Mutex *lock;
    /* the request the thread picks up (under lock) */
    bool pending, pending_stop, pending_loop;
    int pending_row;
    int kind;
    Player p;
    /* CBgmPlayer's lists: rows, count, the one playing (-1 none yet) */
    int list[3][BGM_ROWS], count[3], cur[3];
    bool lists_built;
} g = {.kind = MUSIC_NONE, .channel = -1};

static int16_t rd16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }

bool music_row(int row, const char **title, int *file, int *volume)
{
    if (!g.table || row < 0 || row >= g.rows)
        return false;
    const uint8_t *r = g.table + BGM_ROW * row;
    if (title)
        *title = (const char *)r; /* NUL-padded within its 0x20 bytes */
    if (file)
        *file = rd16(r + 0x30);
    if (volume)
        *volume = rd16(r + 0x32);
    return true;
}

/* ---- the player (CSoundPlayerAT3) ---- */

static void player_release(Player *p)
{
    if (p->id >= 0 && p->active)
        sceAtracReleaseAtracID(p->id);
    free(p->file);
    p->file = NULL;
    p->active = false;
    p->id = -1;
}

static uint8_t *read_whole(const char *path, uint32_t *size)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return NULL;
    SceOff n = sceIoLseek(fd, 0, PSP_SEEK_END);
    uint8_t *buf = n > 0 && n < (64 << 20) ? malloc((size_t)n) : NULL;
    if (buf && (sceIoLseek(fd, 0, PSP_SEEK_SET) != 0 || sceIoRead(fd, buf, (SceSize)n) != (int32_t)n)) {
        free(buf);
        buf = NULL;
    }
    sceIoClose(fd);
    *size = (uint32_t)n;
    return buf;
}

/* FUN_08b2dbf0 */
static bool player_start(Player *p, int row, bool loop)
{
    int file = -1, volume = 0;
    const char *title = "";
    if (!music_row(row, &title, &file, &volume) || file < 0)
        return false;
    char path[96];
    snprintf(path, sizeof path, "disc0:/PSP_GAME/USRDIR/sound/bgm/bgm%02d.at3", file);
    uint32_t size = 0;
    p->file = read_whole(path, &size);
    if (!p->file) {
        SDL_Log("[music] %s can't be read", path);
        return false;
    }
    p->id = sceAtracSetDataAndGetID(p->file, size);
    if (p->id < 0) {
        free(p->file);
        p->file = NULL;
        return false;
    }
    int end = 0, loop_start = -1, loop_end = -1, remain = 0;
    sceAtracIsSecondBufferNeeded(p->id);
    sceAtracGetSoundSample(p->id, &end, &loop_start, &loop_end);
    if (loop_start >= 0)
        sceAtracSetLoopNum(p->id, loop ? -1 : 0);
    sceAtracGetRemainFrame(p->id, &remain);
    p->active = true;
    p->finished = false;
    p->channels = 2;
    p->read = p->write = p->avail = 0;
    p->vol = p->target = (float)volume / 100.0f;
    p->row = row;
    SDL_Log("[music] BGM_%02d \"%.32s\" (bgm%02d.at3, volume %d)%s", row, title, file, volume,
            loop_start >= 0 && loop ? ", looping" : "");
    return true;
}

/* FUN_08b2ddbc: decode until a grain is ready or the track has ended. */
static void player_fill(Player *p)
{
    static int16_t frame[2 * FRAME];
    while (!p->finished && p->avail < GRAIN) {
        int n = 0, finish = 0, remain = 0;
        if (sceAtracDecodeData(p->id, frame, &n, &finish, &remain) < 0) {
            p->finished = true;
            break;
        }
        for (int i = 0; i < n && p->avail < RING; i++) {
            p->ring[2 * p->write] = frame[2 * i];
            p->ring[2 * p->write + 1] = frame[2 * i + 1];
            p->write = (p->write + 1) % RING;
            p->avail++;
        }
        if (finish)
            p->finished = true;
    }
}

/* One grain out of the ring, padded with silence at the end (FUN_08b2e0a8). */
static void player_take(Player *p, int16_t *out)
{
    int n = SDL_min(p->avail, GRAIN);
    for (int i = 0; i < n; i++) {
        out[2 * i] = p->ring[2 * p->read];
        out[2 * i + 1] = p->ring[2 * p->read + 1];
        p->read = (p->read + 1) % RING;
    }
    p->avail -= n;
    memset(out + 2 * n, 0, (size_t)(GRAIN - n) * 4);
}

/* ---- the thread (CSoundPlayerSTR) ---- */

static int SDLCALL str_thread(void *arg)
{
    (void)arg;
    static int16_t grain[2 * GRAIN];
    Player *p = &g.p;
    while (SDL_GetAtomicInt(&g.running)) {
        SDL_LockMutex(g.lock);
        bool take = false, stop = false, loop = false;
        int row = -1;
        if (g.pending || g.pending_stop) {
            if (p->active && p->vol > 0.0f) {
                if (p->target > 0.0f) { /* fade out first */
                    p->step = p->vol / FADE_GRAINS;
                    p->target = 0.0f;
                }
            } else {
                take = g.pending;
                stop = g.pending_stop;
                row = g.pending_row;
                loop = g.pending_loop;
                g.pending = g.pending_stop = false;
            }
        }
        SDL_UnlockMutex(g.lock);
        if (take || stop)
            player_release(p);
        if (take && !player_start(p, row, loop)) {
            SDL_LockMutex(g.lock);
            if (!g.pending)
                g.kind = MUSIC_NONE;
            SDL_UnlockMutex(g.lock);
        }
        if (p->active) {
            player_fill(p);
            player_take(p, grain);
            if (p->vol > p->target)
                p->vol = SDL_max(p->vol - p->step, p->target);
            sceAudioOutputPannedBlocking(g.channel, (int)(p->vol * 0x8000), (int)(p->vol * 0x8000), grain);
            if ((p->finished && p->avail == 0) || (p->target <= 0.0f && p->vol <= 0.0f)) {
                player_release(p);
                SDL_LockMutex(g.lock);
                if (!g.pending)
                    g.kind = MUSIC_NONE;
                SDL_UnlockMutex(g.lock);
            }
        } else {
            memset(grain, 0, sizeof grain);
            sceAudioOutputPannedBlocking(g.channel, 0, 0, grain);
        }
    }
    player_release(p);
    return 0;
}

void music_init(void)
{
    if (g.init)
        return;
    int32_t n = fileio_file_size("sound/sound_bgm_tbl.bin");
    if (n > 0 && (g.table = malloc((size_t)n)) && fileio_read_file("sound/sound_bgm_tbl.bin", g.table, (uint32_t)n) == n)
        g.rows = n / BGM_ROW;
    else
        SDL_Log("[music] sound/sound_bgm_tbl.bin not found: no music");
    sceAtracReinit(0, 3);
    g.p.id = -1;
    g.lock = SDL_CreateMutex();
    g.channel = sceAudioChReserve(-1, GRAIN, 0);
    if (g.channel >= 0) {
        SDL_SetAtomicInt(&g.running, 1);
        g.thread = SDL_CreateThread(str_thread, "SOUND_STRThread", NULL);
    }
    g.init = true;
}

void music_shutdown(void)
{
    if (!g.init)
        return;
    SDL_SetAtomicInt(&g.running, 0);
    if (g.thread)
        SDL_WaitThread(g.thread, NULL);
    if (g.channel >= 0)
        sceAudioChRelease(g.channel);
    SDL_DestroyMutex(g.lock);
    free(g.table);
    memset(&g, 0, sizeof g);
    g.kind = MUSIC_NONE;
    g.channel = -1;
}

void music_play(int row, int kind, bool loop)
{
    if (!g.init || !g.lock)
        return;
    SDL_LockMutex(g.lock);
    g.pending = true;
    g.pending_stop = false;
    g.pending_row = row;
    g.pending_loop = loop;
    g.kind = kind;
    SDL_UnlockMutex(g.lock);
}

void music_stop(void)
{
    if (!g.init || !g.lock)
        return;
    SDL_LockMutex(g.lock);
    g.pending = false;
    g.pending_stop = true;
    g.kind = MUSIC_NONE;
    SDL_UnlockMutex(g.lock);
}

int music_kind(void)
{
    return g.kind;
}

/* ---- CBgmPlayer ---- */

static bool playable(int row)
{
    int file = -1;
    return row >= 0 && row < BGM_ROWS && music_row(row, NULL, &file, NULL) && file >= 0;
}

/* FUN_0888cbd4 (the menus) and its kin for the other lists: the rows ticked in the
 * BGM Player, shuffled in mode 1 (as many random swaps as there are rows). */
static void build_list(int l)
{
    int n = 0;
    for (int row = 0; row < BGM_ROWS; row++)
        if (savedata_bgm_enabled(l, row))
            g.list[l][n++] = row;
    if (savedata_bgm_mode(l) == 1)
        for (int k = 0; k < n; k++) {
            int a = rand() % n, b = rand() % n, t = g.list[l][a];
            g.list[l][a] = g.list[l][b];
            g.list[l][b] = t;
        }
    g.count[l] = n;
    g.cur[l] = -1;
}

void music_lists_build(void)
{
    for (int l = 0; l < 3; l++)
        build_list(l);
    g.lists_built = true;
}

/* The common part of FUN_0888d2ac / FUN_0888dfb0 for Battle Mode: mode 0 plays the
 * list in order, 1 too but shuffles it again after the last, 2 picks at random. */
static void next_from_list(int l, int kind)
{
    if (!g.lists_built)
        music_lists_build();
    int n = 0;
    for (int i = 0; i < g.count[l]; i++)
        n += playable(g.list[l][i]);
    if (n < 1) {
        music_stop();
        return;
    }
    int mode = savedata_bgm_mode(l);
    if (mode == 2) {
        int pick = rand() % g.count[l];
        while (!playable(g.list[l][pick]))
            pick = (pick + 1) % g.count[l];
        g.cur[l] = -1;
        music_play(g.list[l][pick], kind, true);
        return;
    }
    int c = g.cur[l];
    do
        c = (c + 1) % g.count[l];
    while (!playable(g.list[l][c]));
    g.cur[l] = c;
    music_play(g.list[l][c], kind, true);
    if (mode == 1 && c + 1 >= g.count[l])
        build_list(l);
}

void music_title(void)
{
    if (g.kind != MUSIC_TITLE)
        music_play(0x24, MUSIC_TITLE, true);
}

void music_menu(void)
{
    if (g.kind != MUSIC_MENU)
        next_from_list(0, MUSIC_MENU);
}

void music_battle(void)
{
    if (g.kind != MUSIC_BATTLE)
        next_from_list(2, MUSIC_BATTLE);
}
