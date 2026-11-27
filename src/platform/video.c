/* PSP shim: sceMpeg, the PSMF movie player library (25 imports). The game's player
 * (src/game/movie.c, after FUN_08b1d0ec .. FUN_08b1e274) feeds the file through a
 * ring buffer of 2048-byte packets and takes the H.264 video and ATRAC3plus audio
 * access units out of it.
 *
 * Hand-written from the library's documented behaviour. The decoding is the system
 * libavcodec's (FFmpeg, LGPL-2.1-or-later, linked dynamically, the h264 and
 * atrac3plus decoders), built in when CMake finds it (CHEXA_HAVE_LIBAVCODEC); without
 * it sceMpegCreate fails and the game skips the movie.
 *
 * The stream (after the PSMF header, at the offset sceMpegQueryStreamOffset reads) is
 * an MPEG-2 program stream in 2048-byte packs:
 *   - video: PES 0xE0 + channel, H.264 in Annex B with an access unit delimiter
 *     (00 00 00 01 09) in front of every picture;
 *   - audio: PES 0xBD (private stream 1). The payload starts with 4 bytes: the
 *     channel, a zero, and the offset of the first frame that starts in this packet.
 *     Frames are an 8-byte header (0F D0, then 16 bits: sample-rate index << 13 |
 *     channels << 10 | (frame size - 8) / 8, then 4 zero bytes) and the ATRAC3plus
 *     frame, 2048 samples.
 * A PES time stamp belongs to the first access unit that starts in it; others get the
 * previous one plus a frame (3003 for 29.97 Hz video, 2048 samples for audio).
 *
 * Decoded pictures go out as 32-bit RGBA (the PSP's default, pixel mode 3) into the
 * caller's buffer at its stride, audio as 2048 interleaved 16-bit stereo samples.
 *
 * Not supported: more than one stream of a kind, the user data stream, the YCbCr
 * outputs (sceMpegAvcDecodeMode isn't imported), seeking. The library is only used
 * from one thread at a time, as the port's player does. */
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "psp_imports.h"
#include "psp_shim.h"

#define PACKET 2048
#define RING_PACKET_COST (PACKET + 104) /* sceMpegRingbufferQueryMemSize */
#define MPEG_MEM_SIZE 0x10000           /* the caller's buffer; the state is the port's */
#define AVC_ES_SIZE 0x8000
#define ATRAC_ES_SIZE 2112
#define ATRAC_SAMPLES 2048
#define ATRAC_OUT_SIZE (ATRAC_SAMPLES * 2 * 2)
#define AVC_FRAME_TICKS 3003 /* 90 kHz ticks per picture at 29.97 Hz */
#define MAX_MARKS 64

#define ERR_INVALID ((int32_t)0x80618002)
#define ERR_BAD_HEADER ((int32_t)0x806101FE)
#define ERR_NO_MEMORY ((int32_t)0x80618005)

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

/* ---- the ring buffer (no decoder needed) ---- */

int32_t sceMpegRingbufferQueryMemSize(int32_t packets)
{
    int32_t r = packets * RING_PACKET_COST;
    PSP_TRACE(r, "%d", packets);
    return r;
}

int32_t sceMpegRingbufferConstruct(SceMpegRingbuffer *rb, int32_t packets, void *data, int32_t size,
                                   SceMpegRingbufferCB cb, void *arg)
{
    if (!rb || packets <= 0 || !data || size < packets * PACKET) {
        PSP_TRACE(ERR_INVALID, "%p, %d, %p, %d", (void *)rb, packets, data, size);
        return ERR_INVALID;
    }
    memset(rb, 0, sizeof *rb);
    rb->packets = packets;
    rb->free = packets;
    rb->packet_size = PACKET;
    rb->data = data;
    rb->callback = cb;
    rb->cb_arg = arg;
    PSP_TRACE(0, "%p, %d, %p, %d", (void *)rb, packets, data, size);
    return 0;
}

int32_t sceMpegRingbufferDestruct(SceMpegRingbuffer *rb)
{
    PSP_TRACE(0, "%p", (void *)rb);
    return 0;
}

/* Asks the callback for up to `packets` packets, in at most two calls when the free
 * space wraps. A short read is the end of the file. */
int32_t sceMpegRingbufferPut(SceMpegRingbuffer *rb, int32_t packets, int32_t available)
{
    (void)available;
    if (!rb || !rb->callback)
        return ERR_INVALID;
    if (packets > rb->free)
        packets = rb->free;
    int32_t total = 0;
    while (packets > 0) {
        int32_t chunk = rb->packets - rb->write;
        if (chunk > packets)
            chunk = packets;
        int32_t got = rb->callback((uint8_t *)rb->data + (size_t)rb->write * PACKET, chunk, rb->cb_arg);
        if (got < 0)
            return got;
        if (got > chunk)
            got = chunk;
        rb->write = (rb->write + got) % rb->packets;
        rb->free -= got;
        total += got;
        packets -= got;
        if (got < chunk) {
            rb->eof = 1;
            break;
        }
    }
    return total;
}

int32_t sceMpegRingbufferAvailableSize(SceMpegRingbuffer *rb)
{
    return rb ? rb->free : ERR_INVALID;
}

/* ---- the PSMF header ---- */

int32_t sceMpegQueryStreamOffset(SceMpeg *mpeg, const void *header, int32_t *offset)
{
    (void)mpeg;
    const uint8_t *h = header;
    if (!h || memcmp(h, "PSMF", 4) || h[4] != '0' || h[5] != '0' || h[6] != '1') {
        PSP_TRACE(ERR_BAD_HEADER, "%p", header);
        return ERR_BAD_HEADER;
    }
    *offset = (int32_t)be32(h + 8);
    PSP_TRACE(0, "%p -> 0x%x", header, (unsigned)*offset);
    return 0;
}

int32_t sceMpegQueryStreamSize(const void *header, int32_t *size)
{
    const uint8_t *h = header;
    uint32_t n = be32(h + 12);
    if (n & (PACKET - 1)) {
        *size = 0;
        return ERR_BAD_HEADER;
    }
    *size = (int32_t)n;
    PSP_TRACE(0, "%p -> 0x%x", header, (unsigned)n);
    return 0;
}

int32_t sceMpegInit(void)
{
    PSP_TRACE(0, NULL);
    return 0;
}

int32_t sceMpegFinish(void)
{
    PSP_TRACE(0, NULL);
    return 0;
}

int32_t sceMpegQueryMemSize(int32_t mode)
{
    PSP_TRACE(MPEG_MEM_SIZE, "%d", mode);
    return MPEG_MEM_SIZE;
}

int32_t sceMpegQueryAtracEsSize(SceMpeg *mpeg, int32_t *es_size, int32_t *out_size)
{
    (void)mpeg;
    *es_size = ATRAC_ES_SIZE;
    *out_size = ATRAC_OUT_SIZE;
    return 0;
}

int32_t sceMpegInitAu(SceMpeg *mpeg, void *es, SceMpegAu *au)
{
    (void)mpeg;
    memset(au, 0, sizeof *au);
    au->pts_msb = au->dts_msb = 0xFFFFFFFFu;
    au->pts = au->dts = 0xFFFFFFFFu;
    au->es_buffer = (uint32_t)(uintptr_t)es;
    return 0;
}

#ifdef CHEXA_HAVE_LIBAVCODEC
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>

/* One elementary stream's bytes, taken out of the packs in order, and where its
 * time stamps fall. */
typedef struct Fifo {
    uint8_t *buf;
    size_t len, cap;
    struct { size_t off; uint32_t pts; } mark[MAX_MARKS];
    int marks;
    uint32_t last_pts;
    bool have_pts;
} Fifo;

typedef struct Mpeg {
    SceMpegRingbuffer *rb;
    int frame_width;
    bool video_reg, audio_reg;
    int video_ch, audio_ch;
    Fifo video, audio;
    /* The access unit the last Get*Au took, until it is decoded. */
    uint8_t *avc_au;
    size_t avc_len, avc_cap;
    uint8_t *at3_au;
    size_t at3_len;
    int at3_rate, at3_channels, at3_block;
    AVCodecContext *avc, *at3;
    AVPacket *pkt;
    AVFrame *frame;
    bool avc_flushed;
} Mpeg;

static const int k_at3_rates[8] = {32000, 44100, 48000, 88200, 96000, 0, 0, 0};

static bool fifo_append(Fifo *f, const uint8_t *p, size_t n)
{
    if (f->len + n > f->cap) {
        size_t cap = f->cap ? f->cap : 0x10000;
        while (cap < f->len + n)
            cap *= 2;
        uint8_t *b = realloc(f->buf, cap);
        if (!b)
            return false;
        f->buf = b;
        f->cap = cap;
    }
    memcpy(f->buf + f->len, p, n);
    f->len += n;
    return true;
}

static void fifo_mark(Fifo *f, size_t off, uint32_t pts)
{
    if (f->marks < MAX_MARKS) {
        f->mark[f->marks].off = off;
        f->mark[f->marks].pts = pts;
        f->marks++;
    }
}

/* Drops the first n bytes and the time stamps that fell in them. */
static void fifo_drop(Fifo *f, size_t n)
{
    int keep = 0;
    for (int i = 0; i < f->marks; i++)
        if (f->mark[i].off >= n) {
            f->mark[keep].off = f->mark[i].off - n;
            f->mark[keep].pts = f->mark[i].pts;
            keep++;
        }
    f->marks = keep;
    memmove(f->buf, f->buf + n, f->len - n);
    f->len -= n;
}

/* Takes the unit in the first n bytes; returns its time stamp: the first one that
 * falls in it, else the previous unit's plus `duration`. */
static uint32_t fifo_take(Fifo *f, size_t n, uint32_t duration)
{
    uint32_t pts = f->have_pts ? f->last_pts + duration : 0;
    if (f->marks && f->mark[0].off < n)
        pts = f->mark[0].pts;
    fifo_drop(f, n);
    f->last_pts = pts;
    f->have_pts = true;
    return pts;
}

static void fifo_clear(Fifo *f)
{
    f->len = 0;
    f->marks = 0;
    f->have_pts = false;
}

static Mpeg *get(SceMpeg *handle)
{
    return handle ? (Mpeg *)*handle : NULL;
}

static bool ring_empty(const SceMpegRingbuffer *rb)
{
    return rb->free >= rb->packets;
}

/* Takes one packet out of the ring and sorts its PES payloads into the FIFOs. */
static bool demux_packet(Mpeg *m)
{
    SceMpegRingbuffer *rb = m->rb;
    if (ring_empty(rb))
        return false;
    const uint8_t *p = (const uint8_t *)rb->data + (size_t)rb->read * PACKET;
    size_t o = 0;
    while (o + 6 <= PACKET && p[o] == 0 && p[o + 1] == 0 && p[o + 2] == 1) {
        uint8_t id = p[o + 3];
        if (id == 0xBA) { /* pack header */
            if (o + 14 > PACKET)
                break;
            o += 14 + (p[o + 13] & 7);
            continue;
        }
        if (id == 0xB9) /* program end */
            break;
        size_t len = (size_t)(p[o + 4] << 8 | p[o + 5]);
        size_t end = o + 6 + len;
        if (end > PACKET)
            end = PACKET;
        bool video = id == 0xE0 + m->video_ch && m->video_reg;
        bool audio = id == 0xBD && m->audio_reg;
        if ((video || audio) && o + 9 <= end) {
            size_t hl = p[o + 8];
            bool has_pts = (p[o + 7] & 0x80) != 0;
            uint32_t pts = 0;
            if (has_pts && o + 14 <= end) {
                const uint8_t *t = p + o + 9;
                pts = (uint32_t)(((uint64_t)(t[0] >> 1 & 7) << 30 | (uint64_t)t[1] << 22 |
                                  (uint64_t)(t[2] >> 1) << 15 | (uint64_t)t[3] << 7 | t[4] >> 1) &
                                 0xFFFFFFFFu);
            }
            size_t pay = o + 9 + hl;
            if (video && pay <= end) {
                if (has_pts)
                    fifo_mark(&m->video, m->video.len, pts);
                fifo_append(&m->video, p + pay, end - pay);
            } else if (audio && pay + 4 <= end && p[pay] == m->audio_ch) {
                size_t first = (size_t)(p[pay + 2] << 8 | p[pay + 3]);
                if (has_pts)
                    fifo_mark(&m->audio, m->audio.len + first, pts);
                fifo_append(&m->audio, p + pay + 4, end - pay - 4);
            }
        }
        o = o + 6 + len;
    }
    rb->read = (rb->read + 1) % rb->packets;
    rb->free++;
    return true;
}

static bool is_aud(const uint8_t *b, size_t i, size_t len)
{
    return i + 4 <= len && b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 1 && (b[i + 3] & 0x1F) == 9;
}

/* The length of the access unit at the front of the video FIFO: up to the next
 * delimiter, or everything once the file has been read. 0: not complete yet. */
static size_t avc_unit(Mpeg *m)
{
    const Fifo *f = &m->video;
    for (size_t i = 4; i + 4 <= f->len; i++)
        if (is_aud(f->buf, i, f->len))
            return f->buf[i - 1] == 0 ? i - 1 : i;
    if (m->rb->eof && ring_empty(m->rb))
        return f->len;
    return 0;
}

/* The total length of the audio frame at the front of the FIFO (resyncing on its
 * header), 0: not complete yet. */
static size_t at3_unit(Mpeg *m)
{
    Fifo *f = &m->audio;
    size_t skip = 0;
    while (skip + 2 <= f->len && !(f->buf[skip] == 0x0F && f->buf[skip + 1] == 0xD0))
        skip++;
    if (skip)
        fifo_drop(f, skip);
    if (f->len < 8)
        return 0;
    unsigned h = (unsigned)(f->buf[2] << 8 | f->buf[3]);
    size_t size = (h & 0x3FF) * 8 + 8;
    if (!m->at3_block) {
        m->at3_rate = k_at3_rates[h >> 13 & 7];
        m->at3_channels = (int)(h >> 10 & 7);
        m->at3_block = (int)size;
    }
    return f->len >= 8 + size ? 8 + size : 0;
}

int32_t sceMpegCreate(SceMpeg *handle, void *data, int32_t size, SceMpegRingbuffer *rb, int32_t frame_width,
                      int32_t mode, int32_t ddr_top)
{
    (void)data;
    (void)mode;
    (void)ddr_top;
    if (!handle || !rb || size < MPEG_MEM_SIZE)
        return ERR_INVALID;
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    Mpeg *m = calloc(1, sizeof *m);
    if (!m || !codec)
        goto fail;
    m->rb = rb;
    m->frame_width = frame_width;
    m->avc = avcodec_alloc_context3(codec);
    m->pkt = av_packet_alloc();
    m->frame = av_frame_alloc();
    if (!m->avc || !m->pkt || !m->frame)
        goto fail;
    /* One picture in, one out: no frame threading and its delay. */
    m->avc->thread_count = 1;
    m->avc->flags |= AV_CODEC_FLAG_LOW_DELAY;
    if (avcodec_open2(m->avc, codec, NULL) != 0)
        goto fail;
    rb->mpeg = m;
    *handle = m;
    PSP_TRACE(0, "%p, %d, %p, %d", (void *)handle, size, (void *)rb, frame_width);
    return 0;
fail:
    if (m) {
        avcodec_free_context(&m->avc);
        av_packet_free(&m->pkt);
        av_frame_free(&m->frame);
        free(m);
    }
    SDL_Log("[mpeg] no H.264 decoder in libavcodec");
    return ERR_NO_MEMORY;
}

int32_t sceMpegDelete(SceMpeg *handle)
{
    Mpeg *m = get(handle);
    if (!m)
        return ERR_INVALID;
    avcodec_free_context(&m->avc);
    avcodec_free_context(&m->at3);
    av_packet_free(&m->pkt);
    av_frame_free(&m->frame);
    free(m->video.buf);
    free(m->audio.buf);
    free(m->avc_au);
    free(m->at3_au);
    free(m);
    *handle = NULL;
    PSP_TRACE(0, "%p", (void *)handle);
    return 0;
}

SceMpegStream sceMpegRegistStream(SceMpeg *handle, int32_t type, int32_t channel)
{
    Mpeg *m = get(handle);
    if (!m)
        return NULL;
    if (type == SCE_MPEG_AVC_STREAM) {
        m->video_reg = true;
        m->video_ch = channel;
        return &m->video;
    }
    if (type == SCE_MPEG_ATRAC_STREAM) {
        m->audio_reg = true;
        m->audio_ch = channel;
        return &m->audio;
    }
    return NULL;
}

int32_t sceMpegUnRegistStream(SceMpeg *handle, SceMpegStream stream)
{
    Mpeg *m = get(handle);
    if (!m)
        return ERR_INVALID;
    if (stream == &m->video)
        m->video_reg = false;
    else if (stream == &m->audio)
        m->audio_reg = false;
    return 0;
}

void *sceMpegMallocAvcEsBuf(SceMpeg *handle)
{
    /* The access unit lives in the port's state; the handle only has to be non-zero. */
    return get(handle) ? (void *)(uintptr_t)1 : NULL;
}

int32_t sceMpegFreeAvcEsBuf(SceMpeg *handle, void *es)
{
    (void)handle;
    (void)es;
    return 0;
}

static void fill_au(SceMpegAu *au, uint32_t pts, size_t size)
{
    au->pts_msb = au->dts_msb = 0;
    au->pts = au->dts = pts;
    au->au_size = (uint32_t)size;
}

int32_t sceMpegGetAvcAu(SceMpeg *handle, SceMpegStream stream, SceMpegAu *au, int32_t *attr)
{
    Mpeg *m = get(handle);
    if (!m || stream != &m->video)
        return ERR_INVALID;
    size_t n;
    while (!(n = avc_unit(m)))
        if (!demux_packet(m))
            return SCE_MPEG_ERR_NO_DATA;
    if (n > m->avc_cap) {
        uint8_t *b = realloc(m->avc_au, n + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!b)
            return ERR_NO_MEMORY;
        m->avc_au = b;
        m->avc_cap = n;
    }
    memcpy(m->avc_au, m->video.buf, n);
    memset(m->avc_au + n, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    m->avc_len = n;
    fill_au(au, fifo_take(&m->video, n, AVC_FRAME_TICKS), n);
    if (attr)
        *attr = 1;
    return 0;
}

int32_t sceMpegGetAtracAu(SceMpeg *handle, SceMpegStream stream, SceMpegAu *au, int32_t *attr)
{
    Mpeg *m = get(handle);
    if (!m || stream != &m->audio)
        return ERR_INVALID;
    size_t n;
    while (!(n = at3_unit(m)))
        if (!demux_packet(m))
            return SCE_MPEG_ERR_NO_DATA;
    uint8_t *b = realloc(m->at3_au, n + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!b)
        return ERR_NO_MEMORY;
    m->at3_au = b;
    memcpy(b, m->audio.buf, n);
    memset(b + n, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    m->at3_len = n;
    uint32_t ticks = m->at3_rate ? (uint32_t)((uint64_t)ATRAC_SAMPLES * 90000 / (uint32_t)m->at3_rate) : 0;
    fill_au(au, fifo_take(&m->audio, n, ticks), n);
    if (attr)
        *attr = 0;
    return 0;
}

static uint8_t clamp8(int v) { return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v; }

/* BT.601, limited range, to the PSP's 32-bit RGBA at the caller's stride. */
static void to_rgba(const AVFrame *f, uint32_t *dst, int stride)
{
    int w = f->width < PSP_SCREEN_W ? f->width : PSP_SCREEN_W;
    int h = f->height < PSP_SCREEN_H ? f->height : PSP_SCREEN_H;
    for (int y = 0; y < h; y++) {
        const uint8_t *py = f->data[0] + (size_t)y * f->linesize[0];
        const uint8_t *pu = f->data[1] + (size_t)(y >> 1) * f->linesize[1];
        const uint8_t *pv = f->data[2] + (size_t)(y >> 1) * f->linesize[2];
        uint32_t *row = dst + (size_t)y * stride;
        for (int x = 0; x < w; x++) {
            int c = 298 * (py[x] - 16), d = pu[x >> 1] - 128, e = pv[x >> 1] - 128;
            uint8_t r = clamp8((c + 409 * e + 128) >> 8);
            uint8_t g = clamp8((c - 100 * d - 208 * e + 128) >> 8);
            uint8_t b = clamp8((c + 516 * d + 128) >> 8);
            row[x] = 0xFF000000u | (uint32_t)b << 16 | (uint32_t)g << 8 | r;
        }
    }
}

static bool receive_picture(Mpeg *m, void **buffer, int stride)
{
    if (avcodec_receive_frame(m->avc, m->frame) != 0)
        return false;
    bool ok = m->frame->format == AV_PIX_FMT_YUV420P || m->frame->format == AV_PIX_FMT_YUVJ420P;
    if (ok && buffer && *buffer)
        to_rgba(m->frame, *buffer, stride);
    else if (!ok)
        SDL_Log("[mpeg] picture format %d not handled", m->frame->format);
    av_frame_unref(m->frame);
    return ok;
}

int32_t sceMpegAvcDecode(SceMpeg *handle, SceMpegAu *au, int32_t frame_width, void **buffer, int32_t *init)
{
    Mpeg *m = get(handle);
    (void)au;
    if (!m)
        return ERR_INVALID;
    *init = 0;
    if (!m->avc_len)
        return SCE_MPEG_ERR_NO_DATA;
    m->pkt->data = m->avc_au;
    m->pkt->size = (int)m->avc_len;
    int r = avcodec_send_packet(m->avc, m->pkt);
    av_packet_unref(m->pkt);
    m->avc_len = 0;
    if (r < 0 && r != AVERROR(EAGAIN))
        SDL_Log("[mpeg] H.264 access unit not decoded (%d)", r);
    if (receive_picture(m, buffer, frame_width))
        *init = 1;
    return 0;
}

/* After the last access unit: the pictures the decoder still holds, one per call
 * (*status 1), then *status 0. */
int32_t sceMpegAvcDecodeStop(SceMpeg *handle, int32_t frame_width, void **buffer, int32_t *status)
{
    Mpeg *m = get(handle);
    if (!m)
        return ERR_INVALID;
    if (!m->avc_flushed) {
        avcodec_send_packet(m->avc, NULL);
        m->avc_flushed = true;
    }
    *status = receive_picture(m, buffer, frame_width) ? 1 : 0;
    return 0;
}

int32_t sceMpegAvcDecodeFlush(SceMpeg *handle)
{
    Mpeg *m = get(handle);
    if (!m)
        return ERR_INVALID;
    avcodec_flush_buffers(m->avc);
    m->avc_flushed = false;
    m->avc_len = 0;
    return 0;
}

static bool open_atrac(Mpeg *m)
{
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_ATRAC3P);
    if (!codec || !m->at3_rate || m->at3_channels < 1 || m->at3_block <= 0)
        return false;
    m->at3 = avcodec_alloc_context3(codec);
    if (!m->at3)
        return false;
    m->at3->sample_rate = m->at3_rate;
    av_channel_layout_default(&m->at3->ch_layout, m->at3_channels);
    m->at3->block_align = m->at3_block;
    if (avcodec_open2(m->at3, codec, NULL) != 0) {
        avcodec_free_context(&m->at3);
        return false;
    }
    SDL_Log("[mpeg] ATRAC3plus %d Hz, %d channels, %d-byte frames", m->at3_rate, m->at3_channels,
            m->at3_block);
    return true;
}

static int16_t s16(float v)
{
    v *= 32768.0f;
    return (int16_t)(v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : v);
}

/* One frame into 2048 stereo 16-bit samples (silence where the decoder gave none). */
int32_t sceMpegAtracDecode(SceMpeg *handle, SceMpegAu *au, void *buffer, int32_t init)
{
    (void)au;
    (void)init;
    Mpeg *m = get(handle);
    if (!m || !buffer)
        return ERR_INVALID;
    int16_t *out = buffer;
    memset(out, 0, ATRAC_OUT_SIZE);
    if (!m->at3_len || (!m->at3 && !open_atrac(m)))
        return 0;
    m->pkt->data = m->at3_au + 8; /* past the PSMF frame header */
    m->pkt->size = (int)m->at3_len - 8;
    int r = avcodec_send_packet(m->at3, m->pkt);
    av_packet_unref(m->pkt);
    m->at3_len = 0;
    if (r < 0 || avcodec_receive_frame(m->at3, m->frame) != 0)
        return 0;
    AVFrame *f = m->frame;
    int n = f->nb_samples < ATRAC_SAMPLES ? f->nb_samples : ATRAC_SAMPLES;
    int ch = f->ch_layout.nb_channels;
    for (int i = 0; i < n; i++)
        for (int c = 0; c < 2; c++) {
            int src = c < ch ? c : 0;
            float v = 0;
            if (f->format == AV_SAMPLE_FMT_FLTP)
                v = ((const float *)f->extended_data[src])[i];
            else if (f->format == AV_SAMPLE_FMT_FLT)
                v = ((const float *)f->data[0])[i * ch + src];
            out[i * 2 + c] = s16(v);
        }
    av_frame_unref(f);
    return 0;
}

int32_t sceMpegFlushAllStream(SceMpeg *handle)
{
    Mpeg *m = get(handle);
    if (!m)
        return ERR_INVALID;
    fifo_clear(&m->video);
    fifo_clear(&m->audio);
    m->avc_len = m->at3_len = 0;
    avcodec_flush_buffers(m->avc);
    if (m->at3)
        avcodec_flush_buffers(m->at3);
    m->avc_flushed = false;
    SceMpegRingbuffer *rb = m->rb;
    rb->read = rb->write = 0;
    rb->free = rb->packets;
    rb->eof = 0;
    return 0;
}

#else /* no libavcodec: no movie */

int32_t sceMpegCreate(SceMpeg *handle, void *data, int32_t size, SceMpegRingbuffer *rb, int32_t frame_width,
                      int32_t mode, int32_t ddr_top)
{
    (void)handle; (void)data; (void)size; (void)rb; (void)frame_width; (void)mode; (void)ddr_top;
    SDL_Log("[mpeg] built without libavcodec: no movie");
    return ERR_NO_MEMORY;
}

int32_t sceMpegDelete(SceMpeg *handle) { (void)handle; return 0; }
SceMpegStream sceMpegRegistStream(SceMpeg *handle, int32_t type, int32_t channel)
{
    (void)handle; (void)type; (void)channel;
    return NULL;
}
int32_t sceMpegUnRegistStream(SceMpeg *handle, SceMpegStream stream) { (void)handle; (void)stream; return 0; }
void *sceMpegMallocAvcEsBuf(SceMpeg *handle) { (void)handle; return NULL; }
int32_t sceMpegFreeAvcEsBuf(SceMpeg *handle, void *es) { (void)handle; (void)es; return 0; }
int32_t sceMpegGetAvcAu(SceMpeg *handle, SceMpegStream stream, SceMpegAu *au, int32_t *attr)
{
    (void)handle; (void)stream; (void)au; (void)attr;
    return SCE_MPEG_ERR_NO_DATA;
}
int32_t sceMpegGetAtracAu(SceMpeg *handle, SceMpegStream stream, SceMpegAu *au, int32_t *attr)
{
    (void)handle; (void)stream; (void)au; (void)attr;
    return SCE_MPEG_ERR_NO_DATA;
}
int32_t sceMpegAvcDecode(SceMpeg *handle, SceMpegAu *au, int32_t frame_width, void **buffer, int32_t *init)
{
    (void)handle; (void)au; (void)frame_width; (void)buffer;
    *init = 0;
    return ERR_INVALID;
}
int32_t sceMpegAvcDecodeStop(SceMpeg *handle, int32_t frame_width, void **buffer, int32_t *status)
{
    (void)handle; (void)frame_width; (void)buffer;
    *status = 0;
    return 0;
}
int32_t sceMpegAvcDecodeFlush(SceMpeg *handle) { (void)handle; return 0; }
int32_t sceMpegAtracDecode(SceMpeg *handle, SceMpegAu *au, void *buffer, int32_t init)
{
    (void)handle; (void)au; (void)buffer; (void)init;
    return ERR_INVALID;
}
int32_t sceMpegFlushAllStream(SceMpeg *handle) { (void)handle; return 0; }

#endif
