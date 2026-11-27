/* Decodes a PSMF movie (default movie/che_op.pmf) through the sceMpeg imports
 * (src/platform/video.c) offline, the way the game's player feeds them (a ring buffer
 * of 960 packets filled by a read callback), and checks that every picture and audio
 * frame comes out, with time stamps that cover the stream's duration.
 *
 *   chexa-movie-test [--disc DIR] [--file PATH] [--png N FILE]... [--wav FILE] [-v]
 *
 * --png saves picture N (from 0) as a PNG, --wav the audio as a WAV; keep both out of
 * the repository (orig/ is ignored): they are game data.
 *
 * Exits 77 (CTest "skipped") when the game files aren't there or the port was built
 * without libavcodec. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "fileio.h"
#include "psp_imports.h"
#include "psp_shim.h"

#define RING_PACKETS 0x3C0
#define STRIDE 512
#define MAX_PNG 8

static int32_t read_packets(void *data, int32_t packets, void *arg)
{
    int fd = *(int *)arg;
    int n = sceIoRead(fd, data, (SceSize)packets * 2048);
    if (n < 0)
        return n;
    return n % 2048 ? -1 : n / 2048;
}

int main(int argc, char **argv)
{
    const char *disc = getenv("CHEXA_DISC"), *file = "disc0:/PSP_GAME/USRDIR/movie/che_op.pmf";
    const char *wav = NULL;
    struct { int n; const char *path; } png[MAX_PNG];
    int pngs = 0;
    bool verbose = false;
    if (!disc)
        disc = "/mnt/c/psp/chexa";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--disc") && i + 1 < argc) disc = argv[++i];
        else if (!strcmp(argv[i], "--file") && i + 1 < argc) file = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav = argv[++i];
        else if (!strcmp(argv[i], "--png") && i + 2 < argc && pngs < MAX_PNG) {
            png[pngs].n = atoi(argv[++i]);
            png[pngs++].path = argv[++i];
        } else if (!strcmp(argv[i], "-v")) verbose = true;
        else {
            fprintf(stderr, "usage: %s [--disc DIR] [--file PATH] [--png N FILE]... [--wav FILE] [-v]\n", argv[0]);
            return 2;
        }
    }
    if (!psp_io_mount_disc(disc)) {
        printf("SKIP: game files not found at %s\n", disc);
        return 77;
    }
    int fd = sceIoOpen(file, PSP_O_RDONLY, 0);
    if (fd < 0) {
        printf("SKIP: %s not found\n", file);
        return 77;
    }

    uint8_t header[2048];
    if (sceIoRead(fd, header, sizeof header) != (int)sizeof header) {
        printf("FAIL: can't read the header\n");
        return 1;
    }
    sceMpegInit();
    int32_t ring_size = sceMpegRingbufferQueryMemSize(RING_PACKETS);
    void *ring_data = malloc((size_t)ring_size);
    SceMpegRingbuffer ring;
    sceMpegRingbufferConstruct(&ring, RING_PACKETS, ring_data, ring_size, read_packets, &fd);
    int32_t mem_size = sceMpegQueryMemSize(0);
    void *mem = malloc((size_t)mem_size);
    SceMpeg mpeg = NULL;
    if (sceMpegCreate(&mpeg, mem, mem_size, &ring, STRIDE, 0, 0) != 0) {
        printf("SKIP: no decoder (built without libavcodec?)\n");
        return 77;
    }
    int32_t offset = 0, size = 0;
    if (sceMpegQueryStreamOffset(&mpeg, header, &offset) || sceMpegQueryStreamSize(header, &size)) {
        printf("FAIL: not a PSMF header\n");
        return 1;
    }
    sceIoLseek(fd, offset, PSP_SEEK_SET);

    SceMpegStream video = sceMpegRegistStream(&mpeg, SCE_MPEG_AVC_STREAM, 0);
    SceMpegStream audio = sceMpegRegistStream(&mpeg, SCE_MPEG_ATRAC_STREAM, 0);
    void *es = sceMpegMallocAvcEsBuf(&mpeg);
    SceMpegAu vau, aau;
    sceMpegInitAu(&mpeg, es, &vau);
    int32_t at3_es = 0, at3_out = 0;
    sceMpegQueryAtracEsSize(&mpeg, &at3_es, &at3_out);
    void *at3_buf = malloc((size_t)at3_es);
    sceMpegInitAu(&mpeg, at3_buf, &aau);

    uint32_t *picture = calloc(STRIDE * 272, 4);
    int16_t *pcm = malloc((size_t)at3_out);
    FILE *wf = wav ? fopen(wav, "wb") : NULL;
    if (wf)
        fwrite(header, 1, 44, wf); /* the header is rewritten at the end */

    int pictures = 0, units = 0, frames = 0, peak = 0;
    uint32_t v_first = 0, v_last = 0, a_first = 0, a_last = 0;
    bool done_v = false, done_a = false;
    while (!done_v || !done_a) {
        sceMpegRingbufferPut(&ring, RING_PACKETS, sceMpegRingbufferAvailableSize(&ring));
        int32_t attr;
        bool progress = false;
        if (!done_a) {
            int r = sceMpegGetAtracAu(&mpeg, audio, &aau, &attr);
            if (r == 0) {
                sceMpegAtracDecode(&mpeg, &aau, pcm, 1);
                if (!frames)
                    a_first = aau.pts;
                a_last = aau.pts;
                frames++;
                for (int i = 0; i < at3_out / 2; i++)
                    if (abs(pcm[i]) > peak)
                        peak = abs(pcm[i]);
                if (wf)
                    fwrite(pcm, 1, (size_t)at3_out, wf);
                progress = true;
            } else if (ring.eof && sceMpegRingbufferAvailableSize(&ring) == RING_PACKETS) {
                done_a = true;
            }
        }
        if (!done_v) {
            void *buf = picture;
            int32_t init = 0;
            int r = sceMpegGetAvcAu(&mpeg, video, &vau, &attr);
            if (r == 0) {
                if (!units)
                    v_first = vau.pts;
                v_last = vau.pts;
                units++;
                sceMpegAvcDecode(&mpeg, &vau, STRIDE, &buf, &init);
                progress = true;
            } else if (ring.eof && sceMpegRingbufferAvailableSize(&ring) == RING_PACKETS) {
                sceMpegAvcDecodeStop(&mpeg, STRIDE, &buf, &init);
                if (!init)
                    done_v = true;
            }
            if (init) {
                for (int i = 0; i < pngs; i++)
                    if (png[i].n == pictures) {
                        SDL_Surface *s = SDL_CreateSurfaceFrom(480, 272, SDL_PIXELFORMAT_ABGR8888, picture, STRIDE * 4);
                        if (s && SDL_SavePNG(s, png[i].path))
                            printf("picture %d (pts %u) saved to %s\n", pictures, (unsigned)vau.pts, png[i].path);
                        SDL_DestroySurface(s);
                    }
                if (verbose && pictures % 100 == 0)
                    printf("picture %d pts %u\n", pictures, (unsigned)vau.pts);
                pictures++;
            }
        }
        if (!progress && !(ring.eof && sceMpegRingbufferAvailableSize(&ring) == RING_PACKETS))
            sceMpegRingbufferPut(&ring, RING_PACKETS, 0);
    }
    if (wf) {
        uint32_t data = (uint32_t)frames * (uint32_t)at3_out;
        uint8_t h[44] = "RIFF....WAVEfmt ";
        uint32_t v[] = {36 + data};
        memcpy(h + 4, v, 4);
        uint32_t fmt[4] = {16, 0x00020001u, 44100, 44100 * 4};
        memcpy(h + 16, fmt, 16);
        uint32_t align = 0x00100004u;
        memcpy(h + 32, &align, 4);
        memcpy(h + 36, "data", 4);
        memcpy(h + 40, &data, 4);
        fseek(wf, 0, SEEK_SET);
        fwrite(h, 1, 44, wf);
        fclose(wf);
    }

    double v_sec = (v_last - v_first) / 90000.0, a_sec = frames * 2048 / 44100.0;
    printf("%s: stream 0x%x bytes at 0x%x\n", file, (unsigned)size, (unsigned)offset);
    printf("video: %d access units, %d pictures, pts %u .. %u (%.2f s, %.2f per s)\n", units, pictures,
           (unsigned)v_first, (unsigned)v_last, v_sec, v_sec > 0 ? (units - 1) / v_sec : 0);
    printf("audio: %d frames (%.2f s), pts %u .. %u, peak %d\n", frames, a_sec, (unsigned)a_first,
           (unsigned)a_last, peak);
    bool ok = pictures == units && units > 0 && frames > 0 && peak > 1000 &&
              (a_sec - v_sec) < 1.0 && (v_sec - a_sec) < 1.0;
    sceMpegUnRegistStream(&mpeg, video);
    sceMpegUnRegistStream(&mpeg, audio);
    sceMpegFreeAvcEsBuf(&mpeg, es);
    sceMpegDelete(&mpeg);
    sceMpegRingbufferDestruct(&ring);
    sceMpegFinish();
    sceIoClose(fd);
    printf("%s\n", ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
}
