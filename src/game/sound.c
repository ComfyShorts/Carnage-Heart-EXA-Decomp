/* See sound.h and docs/screens.md ("Sound"). Hand-written from:
 *   CSoundDriver::initLibrary FUN_08b2d01c: sceWaveInit, sceAudioOutput2Reserve(0x200),
 *     then the sound library's sceSsInitWithGrain(32 voices, 0x200) (FUN_08b6b8f4 ->
 *     FUN_08b7a70c: __sceSasInit at 44100 Hz, each voice at pitch 0x1000, simple ADSR
 *     0x000F / 0x5FC0, volume 0); its thread mixes a grain (FUN_08b6ed28 ->
 *     __sceSasCore) and sends it to the output channel;
 *   CAplSound: the SE table (TblSEOpen FUN_08b273cc, "sound/sound_se_tbl.bin", rows of
 *     0x38 bytes, FUN_08b27574) and the three PHD slots (ResPHDOpen FUN_08b26998,
 *     ResPHDOpenByFileSet FUN_08b366e8 with "sound/se/%s.phd" / ".pbd");
 *   CSound::SEPlay FUN_08b36b6c -> FUN_08b36c3c -> FUN_08b278ac: the row's volume and
 *     pan (with its random spread, FUN_08b37074), its delay (FUN_08b37004), the repeat
 *     guard (FUN_08b28168), then an ATRAC3plus effect (FUN_08b289d0) or a bank program
 *     (FUN_08b28664 -> the library's voice start FUN_08b6fc8c).
 *
 * The SE table row (0x38 bytes):
 *   +0x00 name ("SE_SYS_000"...)    +0x10 bank   +0x12 program   +0x14 at3se number (0: none)
 *   +0x16 category                  +0x18 volume (float)         +0x1C priority
 *   +0x20 pan, +0x24 pan spread     +0x28, +0x2C (fade, distance; unused here)
 *   +0x30 restart when repeated     +0x32 repeat guard (half vblanks)
 *   +0x34 delay, +0x36 delay spread (frames)
 *
 * The bank (.phd, chunks named at +0x10 / +0x14 / +0x18 of the "PPHD" header):
 *   "PPPG": +0x20 128 program offsets (from the file's start, -1 none); a program is
 *     a count, three unused words and `count` tone numbers;
 *   "PPTN": +0x10 / +0x14 the first and last tone number, tones of 0x60 bytes from
 *     +0x20: +0x00 sample, +0x20 volume (0x1000 full), +0x24 pan (0x40 centre),
 *     +0x28 / +0x2C the effect send, +0x30 root key, +0x50 / +0x54 the ADSR registers;
 *   "PPVA": samples of 16 bytes from +0x20: offset in the .pbd, sample rate, size.
 * Which numbers the library takes the pitch from (the sample's rate against 44100
 * Hz) is the port's reading; reverb, the category volumes and the system data's SE
 * volume aren't applied. */
#include "sound.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "fileio.h"
#include "music.h"
#include "voice.h"
#include "psp_imports.h"
#include "psp_shim.h"

#define VOICES 32
#define GRAIN 0x200
#define SE_ROW 0x38
#define DELAYED_MAX 16

typedef struct Bank {
    uint8_t *phd, *pbd;
    int phd_size, pbd_size;
} Bank;

static const char *const k_bank_name[3] = {"seset_00sys", "seset_01bat", "seset_02adv"};

static struct {
    bool init;
    uint8_t core[64]; /* the library's core block; the shim keeps its own state */
    uint8_t *table;
    int rows;
    Bank bank[3];
    SDL_Thread *thread;
    SDL_AtomicInt running;
    int next_voice;
    struct { int id, bank; uint32_t vcount; } last[VOICES]; /* per voice: what it plays */
    struct { int id, frames; float volume, pan; } delayed[DELAYED_MAX];
    uint32_t warned_at3[8];
} g;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static int16_t rd16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }
static float rdf(const uint8_t *p)
{
    uint32_t u = rd32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static uint8_t *read_file(const char *path, int *size)
{
    int32_t n = fileio_file_size(path);
    if (n <= 0)
        return NULL;
    uint8_t *buf = malloc((size_t)n);
    if (buf && fileio_read_file(path, buf, (uint32_t)n) != n) {
        free(buf);
        return NULL;
    }
    *size = n;
    return buf;
}

/* ---- the sound thread (CSoundDriver) ---- */

static int SDLCALL sound_thread(void *arg)
{
    (void)arg;
    static int16_t buf[2 * GRAIN];
    while (SDL_GetAtomicInt(&g.running)) {
        __sceSasCore(g.core, buf);
        sceAudioOutput2OutputBlocking(0x8000, buf);
    }
    return 0;
}

static void init_common(bool thread)
{
    if (thread)
        sceAudioOutput2Reserve(GRAIN);
    __sceSasInit(g.core, GRAIN, VOICES, 0, 44100);
    for (int v = 0; v < VOICES; v++) {
        __sceSasSetPitch(g.core, v, 0x1000);
        __sceSasSetSimpleADSR(g.core, v, 0x000F, 0x5FC0);
        __sceSasSetVolume(g.core, v, 0, 0, 0, 0);
    }
    for (int v = 0; v < VOICES; v++)
        g.last[v].id = g.last[v].bank = -1;
    int size = 0;
    g.table = read_file("sound/sound_se_tbl.bin", &size);
    g.rows = g.table ? size / SE_ROW : 0;
    if (!g.table)
        SDL_Log("[sound] sound/sound_se_tbl.bin not found: no effects");
    sound_bank_open(SE_BANK_SYS);
    if (thread) {
        SDL_SetAtomicInt(&g.running, 1);
        g.thread = SDL_CreateThread(sound_thread, "SOUND_SasThread", NULL);
        music_init(); /* CSoundDriver::strInit and the BGM table */
        voice_init(); /* the scenes' voices (voice.c) */
    }
    g.init = true;
    SDL_Log("[sound] %d effects in the table%s", g.rows, psp_audio_muted() ? " (muted)" : "");
}

void sound_init(void)
{
    if (!g.init)
        init_common(true);
}

void sound_init_offline(void)
{
    if (!g.init)
        init_common(false);
}

void sound_mix(int16_t *out)
{
    __sceSasCore(g.core, out);
}

void sound_shutdown(void)
{
    if (!g.init)
        return;
    voice_shutdown();
    music_shutdown();
    SDL_SetAtomicInt(&g.running, 0);
    if (g.thread) {
        SDL_WaitThread(g.thread, NULL);
        sceAudioOutput2Release();
    }
    for (int b = 0; b < 3; b++)
        sound_bank_close(b);
    free(g.table);
    memset(&g, 0, sizeof g);
}

/* ---- banks ---- */

bool sound_bank_open(int bank)
{
    if (bank < 0 || bank > 2)
        return false;
    Bank *b = &g.bank[bank];
    if (b->phd)
        return true;
    char path[48];
    snprintf(path, sizeof path, "sound/se/%s.phd", k_bank_name[bank]);
    b->phd = read_file(path, &b->phd_size);
    snprintf(path, sizeof path, "sound/se/%s.pbd", k_bank_name[bank]);
    b->pbd = read_file(path, &b->pbd_size);
    if (!b->phd || !b->pbd || b->phd_size < 0x40 || memcmp(b->phd, "PPHD", 4)) {
        SDL_Log("[sound] bank %s couldn't be read", k_bank_name[bank]);
        sound_bank_close(bank);
        return false;
    }
    SDL_Log("[sound] bank %s: %d + %d bytes", k_bank_name[bank], b->phd_size, b->pbd_size);
    return true;
}

void sound_bank_close(int bank)
{
    if (bank < 0 || bank > 2)
        return;
    Bank *b = &g.bank[bank];
    if (b->pbd && g.init) /* stop the voices that play from it */
        for (int v = 0; v < VOICES; v++)
            if (g.last[v].bank == bank) {
                __sceSasSetKeyOff(g.core, v);
                __sceSasSetVolume(g.core, v, 0, 0, 0, 0);
                g.last[v].id = g.last[v].bank = -1;
            }
    free(b->phd);
    free(b->pbd);
    memset(b, 0, sizeof *b);
}

/* A chunk of the bank by its header slot (+0x10 programs, +0x14 tones, +0x18
 * samples), NULL when it is outside the file. */
static const uint8_t *chunk(const Bank *b, int slot, const char *tag)
{
    uint32_t off = rd32(b->phd + slot);
    if (off + 0x20 > (uint32_t)b->phd_size || memcmp(b->phd + off, tag, 4))
        return NULL;
    return b->phd + off;
}

/* ---- playing ---- */

static int free_voice(void)
{
    uint32_t ended = (uint32_t)__sceSasGetEndFlag(g.core);
    for (int i = 0; i < VOICES; i++) {
        int v = (g.next_voice + i) % VOICES;
        if (ended & (1u << v)) {
            g.next_voice = (v + 1) % VOICES;
            return v;
        }
    }
    int v = g.next_voice; /* all busy: take the oldest */
    g.next_voice = (v + 1) % VOICES;
    return v;
}

/* One tone of a program on a free voice. */
static int play_tone(const Bank *b, int tone, float volume, float pan)
{
    const uint8_t *tn = chunk(b, 0x14, "PPTN"), *va = chunk(b, 0x18, "PPVA");
    if (!tn || !va)
        return -1;
    int first = (int)rd32(tn + 0x10), last = (int)rd32(tn + 0x14);
    if (tone < first || tone > last)
        return -1;
    const uint8_t *t = tn + 0x20 + 0x60 * (tone - first);
    if (t + 0x60 > b->phd + b->phd_size)
        return -1;
    int sample = (int)rd32(t), vfirst = (int)rd32(va + 0x10), vlast = (int)rd32(va + 0x14);
    if (sample < vfirst || sample > vlast)
        return -1;
    const uint8_t *s = va + 0x20 + 16 * (sample - vfirst);
    if (s + 12 > b->phd + b->phd_size)
        return -1;
    uint32_t off = rd32(s), rate = rd32(s + 4), size = rd32(s + 8);
    if (off == 0xFFFFFFFFu || off + size > (uint32_t)b->pbd_size || size < 16)
        return -1;
    int pitch = (int)((uint64_t)rate * 0x1000 / 44100);
    float tone_vol = (float)(int32_t)rd32(t + 0x20) / 4096.0f;
    float tone_pan = ((float)(int32_t)rd32(t + 0x24) - 64.0f) / 64.0f;
    float p = SDL_clamp(pan + tone_pan, -1.0f, 1.0f);
    float vol = SDL_clamp(volume * tone_vol, 0.0f, 1.0f) * 4096.0f;
    int l = (int)(vol * SDL_min(1.0f, 1.0f - p)), r = (int)(vol * SDL_min(1.0f, 1.0f + p));
    int v = free_voice();
    __sceSasSetKeyOff(g.core, v);
    __sceSasSetVoice(g.core, v, b->pbd + off, (int)(size & ~15u), 0);
    __sceSasSetPitch(g.core, v, SDL_clamp(pitch, 1, 0x4000));
    __sceSasSetVolume(g.core, v, l, r, 0, 0);
    __sceSasSetSimpleADSR(g.core, v, (int)rd32(t + 0x50) & 0xFFFF, (int)rd32(t + 0x54) & 0xFFFF);
    __sceSasSetKeyOn(g.core, v);
    return v;
}

static void warn_at3(int at3, int id)
{
    if (at3 < 0 || at3 >= 256 || (g.warned_at3[at3 / 32] & (1u << (at3 % 32))))
        return;
    g.warned_at3[at3 / 32] |= 1u << (at3 % 32);
    SDL_Log("[sound] SE %d is sound/se/at3se/%03d.at3 (ATRAC3plus): no decoder yet, not played", id, at3);
}

/* FUN_08b278ac after the delay: the bank program's tones. */
static int start(int id, float volume, float pan)
{
    const uint8_t *row = g.table + SE_ROW * id;
    int bank = rd16(row + 0x10), program = rd16(row + 0x12), at3 = rd16(row + 0x14);
    if (at3 != 0) {
        warn_at3(at3, id);
        return 0;
    }
    if (bank < 0 || bank > 2 || !g.bank[bank].phd)
        return 0;
    /* the repeat guard (FUN_08b28168): the same effect started less than `guard` * 2
     * vblanks ago is restarted (+0x30 set) or this one dropped */
    int guard = rd16(row + 0x32);
    uint32_t now = (uint32_t)sceDisplayGetVcount();
    if (guard > 0)
        for (int v = 0; v < VOICES; v++)
            if (g.last[v].id == id && now - g.last[v].vcount <= (uint32_t)guard * 2) {
                if (!row[0x30])
                    return 0;
                __sceSasSetKeyOff(g.core, v);
            }
    const Bank *b = &g.bank[bank];
    const uint8_t *pg = chunk(b, 0x10, "PPPG");
    if (!pg || program < 0 || program >= 128)
        return 0;
    uint32_t po = rd32(pg + 0x20 + 4 * program);
    if (po == 0xFFFFFFFFu || po + 16 > (uint32_t)b->phd_size)
        return 0;
    int count = (int)rd32(b->phd + po), played = -1;
    for (int k = 0; k < count && po + 16 + 4 * k + 4 <= (uint32_t)b->phd_size; k++) {
        int v = play_tone(b, (int)rd32(b->phd + po + 16 + 4 * k), volume, pan);
        if (v >= 0) {
            played = v;
            g.last[v].id = id;
            g.last[v].bank = bank;
            g.last[v].vcount = now;
        }
    }
    if (getenv("CHEXA_SOUND_TRACE"))
        SDL_Log("[sound] %.16s (bank %d program %d)%s", (const char *)row, bank, program,
                played >= 0 ? "" : ": nothing played");
    return played >= 0 ? played + 1 : 0;
}

int sound_se_pan(int id, float volume, float pan)
{
    if (!g.init || !g.table || id < 0 || id >= g.rows)
        return 0;
    const uint8_t *row = g.table + SE_ROW * id;
    volume *= rdf(row + 0x18);
    /* the pan spread (FUN_08b37074): up to +-spread in sixteenths */
    float spread = rdf(row + 0x24);
    pan += rdf(row + 0x20);
    if (spread > 0.0f) {
        float d = SDL_min(spread, 1.0f) * 0.0625f * (float)(rand() % 17);
        pan += (rand() & 1) ? -d : d;
    }
    pan = SDL_clamp(pan, -1.0f, 1.0f);
    /* the delay and its spread (FUN_08b37004) */
    int delay = rd16(row + 0x34), dspread = rd16(row + 0x36);
    if (dspread > 0)
        delay += rand() % (dspread + 1);
    if (delay > 0) {
        for (int i = 0; i < DELAYED_MAX; i++)
            if (g.delayed[i].frames <= 0) {
                g.delayed[i].id = id;
                g.delayed[i].frames = delay;
                g.delayed[i].volume = volume;
                g.delayed[i].pan = pan;
                return 1;
            }
        return 0;
    }
    return start(id, volume, pan);
}

int sound_se(int id, float volume)
{
    return sound_se_pan(id, volume, 0.0f);
}

static float g_eye[3], g_fwd[3] = {0, 0, 1}, g_right[3] = {1, 0, 0};

void sound_set_listener(const float eye[3], const float at[3])
{
    float f[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
    float n = SDL_sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (n < 1e-4f)
        return;
    for (int i = 0; i < 3; i++) {
        g_eye[i] = eye[i];
        g_fwd[i] = f[i] / n;
    }
    /* right = forward x up (y up) */
    float r[3] = {-g_fwd[2], 0.0f, g_fwd[0]};
    float rn = SDL_sqrtf(r[0] * r[0] + r[2] * r[2]);
    if (rn > 1e-4f) {
        g_right[0] = r[0] / rn;
        g_right[1] = 0.0f;
        g_right[2] = r[2] / rn;
    }
}

int sound_se_3d(int id, float volume, const float pos[3])
{
    if (!g.init || !g.table || id < 0 || id >= g.rows)
        return 0;
    const uint8_t *row = g.table + SE_ROW * id;
    float d[3] = {pos[0] - g_eye[0], pos[1] - g_eye[1], pos[2] - g_eye[2]};
    float x = d[0] * g_right[0] + d[1] * g_right[1] + d[2] * g_right[2];
    float z = d[0] * g_fwd[0] + d[1] * g_fwd[1] + d[2] * g_fwd[2];
    float dist = SDL_sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) - rdf(row + 0x2C);
    if (dist < 0.0f)
        dist = 0.0f;
    volume *= 1.0f - dist * rdf(row + 0x28);
    if (volume <= 0.0f)
        return 0;
    float across = SDL_sqrtf(x * x + z * z);
    return sound_se_pan(id, SDL_min(volume, 1.0f), across > 1e-3f ? x / across : 0.0f);
}

void sound_update(void)
{
    if (!g.init)
        return;
    for (int i = 0; i < DELAYED_MAX; i++)
        if (g.delayed[i].frames > 0 && --g.delayed[i].frames == 0)
            start(g.delayed[i].id, g.delayed[i].volume, g.delayed[i].pan);
}
