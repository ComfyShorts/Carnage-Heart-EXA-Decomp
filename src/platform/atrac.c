/* PSP shim: sceAtrac3plus, the ATRAC3plus decoder the game's music player uses
 * (CSoundPlayerAT3, FUN_08b2dbf0 .. FUN_08b2dfa8).
 *
 * Hand-written from the library's documented behaviour; the decoding itself is the
 * system libavcodec's (FFmpeg, LGPL-2.1-or-later, linked dynamically), built in when
 * CMake finds it (CHEXA_HAVE_LIBAVCODEC). Without it every ID request fails and the
 * game plays no music.
 *
 * Only the case where the caller hands over the whole file is supported, which the
 * game handles itself: the remaining-frame count is then -1 and it never streams
 * (FUN_08b2dfa8 returns at once). The file is a RIFF WAVE with format 0xFFFE and the
 * ATRAC3plus GUID; "fact" holds the sample count and the samples the decoder output
 * starts with that aren't played, "smpl" the loop, in decoder positions. Decoding goes
 * a frame (2048 samples) per call, as 16-bit samples with the file's channels. */
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "psp_imports.h"
#include "psp_shim.h"

#define ATRAC_IDS 6
#define ATRAC_FRAME 2048
#define ATRAC_ERR_BAD_ID 0x80630005
#define ATRAC_ERR_UNKNOWN_FORMAT 0x80630006
#define ATRAC_ERR_NO_ID 0x8063000B
#define ATRAC_ERR_NO_LOOP_INFO 0x80630021
#define ATRAC_ERR_ALL_DECODED 0x80630024

#ifdef CHEXA_HAVE_LIBAVCODEC
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>

typedef struct Atrac {
    bool used;
    const uint8_t *file;
    int size;
    const uint8_t *data; /* the frames */
    int data_size, block_align, channels, rate;
    int total;         /* samples played (fact +0) */
    int skip;          /* decoder samples before the first one played (fact +4) */
    int loop_start;    /* decoder positions, -1 none (smpl) */
    int loop_end;
    int loops;         /* SetLoopNum: -1 forever, else how many more times */
    int frame;         /* the next frame to decode */
    bool done;
    AVCodecContext *ctx;
    AVPacket *pkt;
    AVFrame *out;
    uint8_t *packet;   /* one frame plus the padding libavcodec wants */
} Atrac;

static Atrac g_atrac[ATRAC_IDS];

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static const uint8_t k_at3plus_guid[16] = {0xBF, 0xAA, 0x23, 0xE9, 0x58, 0xCB, 0x71, 0x44,
                                           0xA1, 0x19, 0xFF, 0xFA, 0x01, 0xE4, 0xCE, 0x62};

static bool parse(Atrac *a)
{
    const uint8_t *p = a->file;
    if (a->size < 12 || memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4))
        return false;
    bool fmt = false;
    a->loop_start = a->loop_end = -1;
    for (int o = 12; o + 8 <= a->size;) {
        uint32_t len = rd32(p + o + 4);
        const uint8_t *c = p + o + 8;
        if (len > (uint32_t)(a->size - o - 8))
            len = (uint32_t)(a->size - o - 8);
        if (!memcmp(p + o, "fmt ", 4) && len >= 40) {
            if (rd16(c) != 0xFFFE || memcmp(c + 24, k_at3plus_guid, 16))
                return false; /* plain ATRAC3 isn't used by the game */
            a->channels = rd16(c + 2);
            a->rate = (int)rd32(c + 4);
            a->block_align = rd16(c + 12);
            fmt = true;
        } else if (!memcmp(p + o, "fact", 4) && len >= 8) {
            a->total = (int)rd32(c);
            a->skip = (int)rd32(c + 4);
        } else if (!memcmp(p + o, "smpl", 4) && len >= 0x34 && rd32(c + 0x1C) > 0) {
            a->loop_start = (int)rd32(c + 0x2C);
            a->loop_end = (int)rd32(c + 0x30);
        } else if (!memcmp(p + o, "data", 4)) {
            a->data = c;
            a->data_size = (int)len;
        }
        o += 8 + (int)len + (int)(len & 1);
    }
    if (!fmt || !a->data || a->block_align <= 0 || a->channels < 1 || a->channels > 2)
        return false;
    int frames = a->data_size / a->block_align;
    if (a->total <= 0 || a->total > frames * ATRAC_FRAME)
        a->total = frames * ATRAC_FRAME - a->skip;
    if (a->loop_end >= a->skip + a->total)
        a->loop_end = a->skip + a->total - 1;
    if (a->loop_start >= a->loop_end)
        a->loop_start = a->loop_end = -1;
    return true;
}

static void release(Atrac *a)
{
    avcodec_free_context(&a->ctx);
    av_packet_free(&a->pkt);
    av_frame_free(&a->out);
    free(a->packet);
    memset(a, 0, sizeof *a);
}

static bool open_decoder(Atrac *a)
{
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_ATRAC3P);
    if (!codec)
        return false;
    a->ctx = avcodec_alloc_context3(codec);
    a->pkt = av_packet_alloc();
    a->out = av_frame_alloc();
    a->packet = calloc(1, (size_t)a->block_align + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!a->ctx || !a->pkt || !a->out || !a->packet)
        return false;
    a->ctx->sample_rate = a->rate;
    av_channel_layout_default(&a->ctx->ch_layout, a->channels);
    a->ctx->block_align = a->block_align;
    return avcodec_open2(a->ctx, codec, NULL) == 0;
}

static Atrac *atrac(int id)
{
    return id >= 0 && id < ATRAC_IDS && g_atrac[id].used ? &g_atrac[id] : NULL;
}

/* Decodes frame `f` into out (nb_samples ATRAC_FRAME), false past the data. */
static bool decode_frame(Atrac *a, int f)
{
    if ((f + 1) * a->block_align > a->data_size)
        return false;
    memcpy(a->packet, a->data + f * a->block_align, (size_t)a->block_align);
    a->pkt->data = a->packet;
    a->pkt->size = a->block_align;
    if (avcodec_send_packet(a->ctx, a->pkt) < 0)
        return false;
    return avcodec_receive_frame(a->ctx, a->out) == 0;
}

/* Starts decoding again at the frame holding decoder position `pos`, priming the
 * decoder with the frame before it so that the overlap is right. */
static void seek(Atrac *a, int pos)
{
    avcodec_flush_buffers(a->ctx);
    a->frame = pos / ATRAC_FRAME;
    if (a->frame > 0)
        decode_frame(a, a->frame - 1);
}

/* Copies the part of the decoded frame (decoder positions base..base+n) between first
 * and last into out as 16-bit samples. Returns the samples copied. */
static int copy_out(Atrac *a, int base, int first, int last, int16_t *out)
{
    int from = SDL_max(first - base, 0), to = SDL_min(last - base + 1, a->out->nb_samples);
    int n = 0;
    bool planar = av_sample_fmt_is_planar(a->ctx->sample_fmt);
    for (int i = from; i < to; i++, n++)
        for (int ch = 0; ch < a->channels; ch++) {
            float v;
            if (a->ctx->sample_fmt == AV_SAMPLE_FMT_FLTP || a->ctx->sample_fmt == AV_SAMPLE_FMT_FLT)
                v = planar ? ((const float *)a->out->extended_data[ch])[i]
                           : ((const float *)a->out->extended_data[0])[i * a->channels + ch];
            else
                v = (planar ? ((const int16_t *)a->out->extended_data[ch])[i]
                            : ((const int16_t *)a->out->extended_data[0])[i * a->channels + ch]) /
                    32768.0f;
            int s = (int)(v * 32767.0f);
            out[n * a->channels + ch] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
        }
    return n;
}
#endif /* CHEXA_HAVE_LIBAVCODEC */

int32_t sceAtracReinit(int at3_ids, int at3plus_ids)
{
    PSP_TRACE(0, "%d ATRAC3, %d ATRAC3plus", at3_ids, at3plus_ids);
    return 0;
}

#ifdef CHEXA_HAVE_LIBAVCODEC
static int32_t set_data(int id, const void *buf, uint32_t size);
#endif

int32_t sceAtracSetDataAndGetID(const void *buf, uint32_t size)
{
#ifdef CHEXA_HAVE_LIBAVCODEC
    /* The music and the voices take IDs from their own threads: the free slot is
     * claimed under a lock, then set up outside it. */
    static SDL_SpinLock lock;
    static bool claimed[ATRAC_IDS];
    SDL_LockSpinlock(&lock);
    int id = 0;
    while (id < ATRAC_IDS && (g_atrac[id].used || claimed[id]))
        id++;
    if (id < ATRAC_IDS)
        claimed[id] = true;
    SDL_UnlockSpinlock(&lock);
    if (id == ATRAC_IDS)
        return (int32_t)ATRAC_ERR_NO_ID;
    int32_t r = set_data(id, buf, size);
    claimed[id] = false;
    return r;
#else
    static bool warned;
    if (!warned) {
        warned = true;
        SDL_Log("[atrac] built without libavcodec: no ATRAC3plus, the music doesn't play "
                "(tools/setup_pc_deps.sh --libavcodec)");
    }
    (void)buf, (void)size;
    return (int32_t)ATRAC_ERR_UNKNOWN_FORMAT;
#endif
}

#ifdef CHEXA_HAVE_LIBAVCODEC
static int32_t set_data(int id, const void *buf, uint32_t size)
{
    Atrac *a = &g_atrac[id];
    memset(a, 0, sizeof *a);
    a->file = buf;
    a->size = (int)size;
    if (!parse(a)) {
        memset(a, 0, sizeof *a);
        PSP_TRACE((int32_t)ATRAC_ERR_UNKNOWN_FORMAT, "%p, %u bytes: not ATRAC3plus", buf, size);
        return (int32_t)ATRAC_ERR_UNKNOWN_FORMAT;
    }
    if (!open_decoder(a)) {
        release(a);
        SDL_Log("[atrac] libavcodec has no ATRAC3plus decoder");
        return (int32_t)ATRAC_ERR_UNKNOWN_FORMAT;
    }
    a->used = true;
    a->loops = 0;
    seek(a, a->skip);
    PSP_TRACE(id, "%u bytes: %d ch, %d Hz, %d samples, loop %d..%d", size, a->channels, a->rate,
              a->total, a->loop_start, a->loop_end);
    return id;
}
#endif

int32_t sceAtracReleaseAtracID(int id)
{
#ifdef CHEXA_HAVE_LIBAVCODEC
    Atrac *a = atrac(id);
    if (!a)
        return (int32_t)ATRAC_ERR_BAD_ID;
    release(a);
    return 0;
#else
    (void)id;
    return (int32_t)ATRAC_ERR_BAD_ID;
#endif
}

int32_t sceAtracIsSecondBufferNeeded(int id)
{
    (void)id;
    return 0;
}

/* The last sample and the loop, counted from the first sample played. */
int32_t sceAtracGetSoundSample(int id, int *end, int *loop_start, int *loop_end)
{
#ifdef CHEXA_HAVE_LIBAVCODEC
    Atrac *a = atrac(id);
    if (!a)
        return (int32_t)ATRAC_ERR_BAD_ID;
    *end = a->total - 1;
    *loop_start = a->loop_start >= 0 ? a->loop_start - a->skip : -1;
    *loop_end = a->loop_end >= 0 ? a->loop_end - a->skip : -1;
    return 0;
#else
    (void)id, (void)end, (void)loop_start, (void)loop_end;
    return (int32_t)ATRAC_ERR_BAD_ID;
#endif
}

int32_t sceAtracSetLoopNum(int id, int loops)
{
#ifdef CHEXA_HAVE_LIBAVCODEC
    Atrac *a = atrac(id);
    if (!a)
        return (int32_t)ATRAC_ERR_BAD_ID;
    if (a->loop_start < 0)
        return (int32_t)ATRAC_ERR_NO_LOOP_INFO;
    a->loops = loops;
    return 0;
#else
    (void)id, (void)loops;
    return (int32_t)ATRAC_ERR_BAD_ID;
#endif
}

/* -1: the whole file is in the buffer, nothing to stream. */
int32_t sceAtracGetRemainFrame(int id, int *remain)
{
    if (remain)
        *remain = -1;
#ifdef CHEXA_HAVE_LIBAVCODEC
    return atrac(id) ? 0 : (int32_t)ATRAC_ERR_BAD_ID;
#else
    (void)id;
    return (int32_t)ATRAC_ERR_BAD_ID;
#endif
}

int32_t sceAtracDecodeData(int id, int16_t *out, int *samples, int *finish, int *remain)
{
    *samples = 0;
    if (remain)
        *remain = -1;
#ifdef CHEXA_HAVE_LIBAVCODEC
    Atrac *a = atrac(id);
    if (!a) {
        *finish = 1;
        return (int32_t)ATRAC_ERR_BAD_ID;
    }
    if (a->done) {
        *finish = 1;
        return (int32_t)ATRAC_ERR_ALL_DECODED;
    }
    *finish = 0;
    bool looping = a->loops != 0 && a->loop_start >= 0;
    int last = looping ? a->loop_end : a->skip + a->total - 1;
    int base = a->frame * ATRAC_FRAME;
    if (!decode_frame(a, a->frame)) {
        a->done = true;
        *finish = 1;
        return 0;
    }
    a->frame++;
    *samples = copy_out(a, base, a->skip, last, out);
    if (base + ATRAC_FRAME > last) { /* the end, or the loop's */
        if (looping) {
            if (a->loops > 0)
                a->loops--;
            /* the samples from the loop start fill the rest of this call's frame */
            seek(a, a->loop_start);
            int nbase = a->frame * ATRAC_FRAME;
            if (*samples < ATRAC_FRAME && decode_frame(a, a->frame)) {
                a->frame++;
                int room = ATRAC_FRAME - *samples;
                int nlast = SDL_min(a->loop_start + room - 1, a->loop_end);
                *samples += copy_out(a, nbase, a->loop_start, nlast, out + *samples * a->channels);
                if (nlast < nbase + ATRAC_FRAME - 1) /* the rest of that frame comes next */
                    seek(a, nlast + 1);
            }
        } else {
            a->done = true;
            *finish = 1;
        }
    }
    return 0;
#else
    (void)id, (void)out;
    *finish = 1;
    return (int32_t)ATRAC_ERR_BAD_ID;
#endif
}

/* Never needed with the whole file in the buffer: nothing to write, nothing to add. */
int32_t sceAtracGetStreamDataInfo(int id, uint8_t **write, uint32_t *writable, uint32_t *offset)
{
#ifdef CHEXA_HAVE_LIBAVCODEC
    Atrac *a = atrac(id);
    if (!a)
        return (int32_t)ATRAC_ERR_BAD_ID;
    *write = (uint8_t *)a->file + a->size;
    *writable = 0;
    *offset = (uint32_t)a->size;
    return 0;
#else
    (void)id, (void)write, (void)writable, (void)offset;
    return (int32_t)ATRAC_ERR_BAD_ID;
#endif
}

int32_t sceAtracAddStreamData(int id, uint32_t bytes)
{
    (void)id, (void)bytes;
    return 0;
}

int32_t sceAtracGetInternalErrorInfo(int id, int *error)
{
    (void)id;
    if (error)
        *error = 0;
    return 0;
}
