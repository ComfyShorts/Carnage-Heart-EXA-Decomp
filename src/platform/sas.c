/* PSP shim: sceSasCore, the sound mixer the game's sound driver runs on (its own
 * library keeps a static core block and calls these, FUN_08b7a6b4 .. FUN_08b7a974).
 *
 * Hand-written from the documented behaviour of the hardware it stands for: up to 32
 * voices playing PSX-style ADPCM ("VAG") data at a pitch (0x1000 = 44100 Hz), each with
 * an ADSR envelope in the SPU register format and left/right volumes (0x1000 = full),
 * mixed `grain` stereo samples at a time by __sceSasCore into a 16-bit buffer.
 * Reverb, noise and PCM voices aren't implemented (the calls are accepted).
 *
 * The voices are changed from the game's threads while the sound thread mixes, so
 * everything runs under one lock. */
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "psp_imports.h"
#include "psp_shim.h"

#define SAS_VOICES 32
#define SAS_ERR_VOICE 0x80420010
#define SAS_ERR_ADDRESS 0x80420011
#define SAS_ERR_PITCH 0x80420012
#define SAS_ERR_VOLUME 0x80420018
#define SAS_ERR_NOT_INIT 0x80420100

typedef enum { ENV_OFF, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE } EnvPhase;

typedef struct SasVoice {
    const uint8_t *vag;   /* the ADPCM data */
    int size;             /* its bytes */
    int loop;             /* SetVoice's loop mode: 1 loops at the data's loop flags */
    int pitch;            /* 0x1000 = one sample per output sample */
    int vol_l, vol_r;     /* 0..0x1000 */
    uint16_t adsr1, adsr2;
    bool on;              /* keyed on and still sounding */
    bool ended;           /* GetEndFlag */
    /* decoder */
    int block;            /* the current 16-byte block */
    int loop_block;
    int16_t pcm[28];
    int hist1, hist2;
    uint32_t frac;        /* 12-bit position inside the block's 28 samples */
    int pos;              /* sample index inside pcm[] */
    /* envelope (0..0x7FFF) */
    EnvPhase phase;
    int env;
    int env_counter;
} SasVoice;

static struct {
    SDL_Mutex *lock;
    bool init;
    int grain;
    int out_mode;
    uint32_t pause_mask;
    SasVoice v[SAS_VOICES];
} g_sas;

static void lock(void)
{
    if (!g_sas.lock)
        g_sas.lock = SDL_CreateMutex();
    SDL_LockMutex(g_sas.lock);
}

static void unlock(void)
{
    SDL_UnlockMutex(g_sas.lock);
}

/* ---- ADPCM ---- */

static const int k_filter[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};

/* One 16-byte block: byte 0 shift (low nibble) and filter (high), byte 1 flags (bit 0
 * end, bit 1 repeat, bit 2 loop start), 14 bytes of 28 4-bit samples. */
static void decode_block(SasVoice *v)
{
    const uint8_t *b = v->vag + v->block * 16;
    int shift = b[0] & 0x0F, filter = (b[0] >> 4) & 0x0F, flags = b[1];
    if (filter > 4)
        filter = 0;
    if (shift > 12)
        shift = 9;
    if (flags & 4)
        v->loop_block = v->block;
    for (int i = 0; i < 28; i++) {
        int nib = (b[2 + i / 2] >> ((i & 1) * 4)) & 0x0F;
        int s = (int16_t)(nib << 12) >> shift;
        s += (v->hist1 * k_filter[filter][0] + v->hist2 * k_filter[filter][1] + 32) >> 6;
        if (s > 32767)
            s = 32767;
        if (s < -32768)
            s = -32768;
        v->pcm[i] = (int16_t)s;
        v->hist2 = v->hist1;
        v->hist1 = s;
    }
    v->pos = 0;
}

/* The next block, or false when the voice has run out. */
static bool next_block(SasVoice *v)
{
    const uint8_t *b = v->vag + v->block * 16;
    int flags = b[1];
    if (flags & 1) {
        if ((flags & 2) && v->loop) {
            v->block = v->loop_block;
        } else {
            return false;
        }
    } else {
        v->block++;
    }
    if ((v->block + 1) * 16 > v->size)
        return false;
    if (v->vag[v->block * 16 + 1] == 7) /* the end marker block */
        return false;
    decode_block(v);
    return true;
}

/* ---- the envelope (SPU ADSR registers) ---- */

/* One step of a rate: returns the change and sets how many samples it waits. */
static int rate_step(int rate, bool decrease, bool exp, int env, int *wait)
{
    int shift = rate >> 2;
    int step = decrease ? -8 + (rate & 3) : 7 - (rate & 3);
    int cycles = 1;
    if (shift < 11)
        step <<= 11 - shift;
    else
        cycles = 1 << (shift - 11);
    if (exp && !decrease && env > 0x6000)
        cycles *= 4;
    if (exp && decrease)
        step = step * env >> 15;
    *wait = cycles;
    return step;
}

static void envelope(SasVoice *v)
{
    if (v->env_counter > 0 && --v->env_counter > 0)
        return;
    int a1 = v->adsr1, a2 = v->adsr2, wait = 1;
    switch (v->phase) {
    case ENV_ATTACK:
        v->env += rate_step((a1 >> 8) & 0x7F, false, (a1 & 0x8000) != 0, v->env, &wait);
        if (v->env >= 0x7FFF) {
            v->env = 0x7FFF;
            v->phase = ENV_DECAY;
        }
        break;
    case ENV_DECAY: {
        int sustain = ((a1 & 0x0F) + 1) * 0x800;
        v->env += rate_step(((a1 >> 4) & 0x0F) << 2, true, true, v->env, &wait);
        if (v->env <= sustain) {
            v->env = sustain < 0x7FFF ? sustain : 0x7FFF;
            v->phase = ENV_SUSTAIN;
        }
        break;
    }
    case ENV_SUSTAIN:
        v->env += rate_step((a2 >> 6) & 0x7F, (a2 & 0x4000) != 0, (a2 & 0x8000) != 0, v->env, &wait);
        break;
    case ENV_RELEASE:
        v->env += rate_step((a2 & 0x1F) << 2, true, (a2 & 0x20) != 0, v->env, &wait);
        break;
    default:
        break;
    }
    if (v->env > 0x7FFF)
        v->env = 0x7FFF;
    if (v->env <= 0 && v->phase != ENV_ATTACK) {
        v->env = 0;
        if (v->phase == ENV_RELEASE || v->phase == ENV_SUSTAIN) {
            v->on = false;
            v->ended = true;
            v->phase = ENV_OFF;
        }
    }
    v->env_counter = wait;
}

/* ---- the imports ---- */

int32_t __sceSasInit(void *core, int grain, int max_voices, int out_mode, int freq)
{
    (void)core;
    lock();
    memset(g_sas.v, 0, sizeof g_sas.v);
    g_sas.grain = grain;
    g_sas.out_mode = out_mode;
    g_sas.pause_mask = 0;
    g_sas.init = true;
    for (int i = 0; i < SAS_VOICES; i++) {
        g_sas.v[i].pitch = 0x1000;
        g_sas.v[i].ended = true;
    }
    unlock();
    PSP_TRACE(0, "grain %d, %d voices, mode %d, %d Hz", grain, max_voices, out_mode, freq);
    return 0;
}

int32_t __sceSasCore(void *core, int16_t *out)
{
    (void)core;
    if (!g_sas.init)
        return (int32_t)SAS_ERR_NOT_INIT;
    lock();
    int n = g_sas.grain;
    static int32_t mix[2 * 2048];
    if (n > 2048)
        n = 2048;
    memset(mix, 0, sizeof(int32_t) * 2 * (size_t)n);
    for (int vi = 0; vi < SAS_VOICES; vi++) {
        SasVoice *v = &g_sas.v[vi];
        if (!v->on || (g_sas.pause_mask & (1u << vi)))
            continue;
        for (int i = 0; i < n && v->on; i++) {
            envelope(v);
            int s = v->pcm[v->pos];
            int64_t a = (int64_t)s * v->env >> 15;
            mix[2 * i] += (int32_t)(a * v->vol_l >> 12);
            mix[2 * i + 1] += (int32_t)(a * v->vol_r >> 12);
            v->frac += (uint32_t)v->pitch;
            while (v->frac >= 0x1000 && v->on) {
                v->frac -= 0x1000;
                if (++v->pos >= 28) {
                    if (!next_block(v)) {
                        v->on = false;
                        v->ended = true;
                    }
                }
            }
        }
    }
    unlock();
    for (int i = 0; i < 2 * n; i++) {
        int32_t s = mix[i];
        out[i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
    }
    return 0;
}

int32_t __sceSasCoreWithMix(void *core, int16_t *inout, int left, int right)
{
    static int16_t tmp[2 * 2048];
    int32_t r = __sceSasCore(core, tmp);
    if (r)
        return r;
    for (int i = 0; i < 2 * g_sas.grain && i < 2 * 2048; i++) {
        int32_t s = inout[i] * ((i & 1) ? right : left) / 0x1000 + tmp[i];
        inout[i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
    }
    return 0;
}

static SasVoice *voice(int i)
{
    return i >= 0 && i < SAS_VOICES ? &g_sas.v[i] : NULL;
}

int32_t __sceSasSetVoice(void *core, int i, const void *vag, int size, int loop)
{
    (void)core;
    SasVoice *v = voice(i);
    if (!v)
        return (int32_t)SAS_ERR_VOICE;
    if (!vag || size <= 0 || (size & 0xF))
        return (int32_t)SAS_ERR_ADDRESS;
    lock();
    v->vag = vag;
    v->size = size;
    v->loop = loop;
    unlock();
    return 0;
}

int32_t __sceSasSetVoicePCM(void *core, int i, const void *pcm, int samples, int loop)
{
    (void)core, (void)pcm, (void)samples, (void)loop;
    PSP_STUB("sceSasCore", 0xE1CD9561);
    return voice(i) ? 0 : (int32_t)SAS_ERR_VOICE;
}

int32_t __sceSasSetPitch(void *core, int i, int pitch)
{
    (void)core;
    SasVoice *v = voice(i);
    if (!v)
        return (int32_t)SAS_ERR_VOICE;
    if (pitch < 1 || pitch > 0x4000)
        return (int32_t)SAS_ERR_PITCH;
    lock();
    v->pitch = pitch;
    unlock();
    return 0;
}

int32_t __sceSasSetVolume(void *core, int i, int l, int r, int el, int er)
{
    (void)core, (void)el, (void)er; /* the effect (reverb) send isn't mixed */
    SasVoice *v = voice(i);
    if (!v)
        return (int32_t)SAS_ERR_VOICE;
    if (abs(l) > 0x1000 || abs(r) > 0x1000)
        return (int32_t)SAS_ERR_VOLUME;
    lock();
    v->vol_l = l;
    v->vol_r = r;
    unlock();
    return 0;
}

int32_t __sceSasSetSimpleADSR(void *core, int i, int adsr1, int adsr2)
{
    (void)core;
    SasVoice *v = voice(i);
    if (!v)
        return (int32_t)SAS_ERR_VOICE;
    lock();
    v->adsr1 = (uint16_t)adsr1;
    v->adsr2 = (uint16_t)adsr2;
    unlock();
    return 0;
}

/* The full-parameter forms aren't used by the game's driver: accepted, ignored. */
int32_t __sceSasSetADSR(void *core, int i, int flags, int a, int d, int s, int r)
{
    (void)core, (void)flags, (void)a, (void)d, (void)s, (void)r;
    return voice(i) ? 0 : (int32_t)SAS_ERR_VOICE;
}

int32_t __sceSasSetADSRmode(void *core, int i, int flags, int a, int d, int s, int r)
{
    (void)core, (void)flags, (void)a, (void)d, (void)s, (void)r;
    return voice(i) ? 0 : (int32_t)SAS_ERR_VOICE;
}

int32_t __sceSasSetSL(void *core, int i, int level)
{
    (void)core, (void)level;
    return voice(i) ? 0 : (int32_t)SAS_ERR_VOICE;
}

int32_t __sceSasSetKeyOn(void *core, int i)
{
    (void)core;
    SasVoice *v = voice(i);
    if (!v)
        return (int32_t)SAS_ERR_VOICE;
    lock();
    if (v->vag) {
        v->block = 0;
        v->loop_block = 0;
        v->hist1 = v->hist2 = 0;
        v->frac = 0;
        decode_block(v);
        v->env = 0;
        v->env_counter = 0;
        v->phase = ENV_ATTACK;
        v->on = true;
        v->ended = false;
    }
    unlock();
    return 0;
}

int32_t __sceSasSetKeyOff(void *core, int i)
{
    (void)core;
    SasVoice *v = voice(i);
    if (!v)
        return (int32_t)SAS_ERR_VOICE;
    lock();
    if (v->on) {
        v->phase = ENV_RELEASE;
        v->env_counter = 0;
    }
    unlock();
    return 0;
}

int32_t __sceSasGetEndFlag(void *core)
{
    (void)core;
    lock();
    uint32_t m = 0;
    for (int i = 0; i < SAS_VOICES; i++)
        if (g_sas.v[i].ended)
            m |= 1u << i;
    unlock();
    return (int32_t)m;
}

int32_t __sceSasGetEnvelopeHeight(void *core, int i)
{
    (void)core;
    SasVoice *v = voice(i);
    return v ? v->env << 15 : (int32_t)SAS_ERR_VOICE; /* the PSP's scale tops at 0x40000000 */
}

int32_t __sceSasGetAllEnvelopeHeights(void *core, int32_t *heights)
{
    (void)core;
    lock();
    for (int i = 0; i < SAS_VOICES; i++)
        heights[i] = g_sas.v[i].env << 15;
    unlock();
    return 0;
}

int32_t __sceSasGetGrain(void *core)
{
    (void)core;
    return g_sas.grain;
}

int32_t __sceSasSetGrain(void *core, int grain)
{
    (void)core;
    g_sas.grain = grain;
    return 0;
}

int32_t __sceSasGetOutputmode(void *core)
{
    (void)core;
    return g_sas.out_mode;
}

int32_t __sceSasSetOutputmode(void *core, int mode)
{
    (void)core;
    g_sas.out_mode = mode;
    return 0;
}

int32_t __sceSasGetPauseFlag(void *core)
{
    (void)core;
    return (int32_t)g_sas.pause_mask;
}

int32_t __sceSasSetPause(void *core, uint32_t mask, int pause)
{
    (void)core;
    lock();
    if (pause)
        g_sas.pause_mask |= mask;
    else
        g_sas.pause_mask &= ~mask;
    unlock();
    return 0;
}

int32_t __sceSasSetNoise(void *core, int i, int freq)
{
    (void)core, (void)freq;
    return voice(i) ? 0 : (int32_t)SAS_ERR_VOICE;
}

/* Reverb isn't mixed. */
int32_t __sceSasRevType(void *core, int type) { (void)core, (void)type; return 0; }
int32_t __sceSasRevParam(void *core, int delay, int feedback) { (void)core, (void)delay, (void)feedback; return 0; }
int32_t __sceSasRevEVOL(void *core, int l, int r) { (void)core, (void)l, (void)r; return 0; }
int32_t __sceSasRevVON(void *core, int dry, int wet) { (void)core, (void)dry, (void)wet; return 0; }
