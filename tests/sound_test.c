/* Checks the sound effects path (src/game/sound.c on src/platform/sas.c) offline:
 * loads the SE table and the system and battle banks from the game files, plays
 * effects into the mixer without a device and checks that each one makes sound and
 * then ends. With --wav DIR it also writes each effect to DIR/se_NNN.wav to listen to
 * (keep that out of the repository: it is game audio).
 *
 *   chexa-sound-test [--disc DIR] [--wav DIR] [-v] [ID...]
 *   chexa-sound-test --music [--disc DIR] [--wav DIR] [-v] [FILE...]
 *
 * --music decodes music files (sound/bgm/bgmNN.at3, numbers, default 1 and 36)
 * through the sceAtrac3plus imports: once to the end, checking the sample count
 * against the file's, then looping, checking that the loop wraps without a gap.
 * It exits 77 when the port was built without libavcodec.
 *
 * Exits 77 (CTest "skipped") when the game files aren't there. */
#include <stdio.h>

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

#include "fileio.h"
#include "psp_imports.h"
#include "heap.h"
#include "psp_shim.h"
#include "sound.h"

static void write_wav(const char *path, const int16_t *pcm, int frames)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    uint32_t data = (uint32_t)frames * 4, v;
    fwrite("RIFF", 1, 4, f);
    v = 36 + data;
    fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    uint32_t fmt[4] = {16, 0x00020001u, 44100, 44100 * 4};
    fwrite(fmt, 4, 4, f);
    uint32_t align = 0x00100004u;
    fwrite(&align, 4, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data, 4, 1, f);
    fwrite(pcm, 4, (size_t)frames, f);
    fclose(f);
}

/* Plays effect `id` and mixes up to 4 s. Returns the peak and sets the length in
 * samples until the last sound. */
static int render(int id, int16_t *pcm, int max_frames, int *length)
{
    sound_se(id, 1.0f);
    int peak = 0, last = 0, frames = 0;
    for (int frame = 0; frames + 0x200 <= max_frames; frame++) {
        if (frame % 8 == 0)
            sound_update(); /* the delays count frames; a grain is about 1/86 s */
        sound_mix(pcm + 2 * frames);
        for (int i = 0; i < 2 * 0x200; i++) {
            int a = abs(pcm[2 * frames + i]);
            if (a > peak)
                peak = a;
            if (a > 64)
                last = frames + i / 2;
        }
        frames += 0x200;
    }
    *length = last;
    return peak;
}

static uint8_t *read_disc(const char *path, uint32_t *size)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return NULL;
    SceOff n = sceIoLseek(fd, 0, PSP_SEEK_END);
    sceIoLseek(fd, 0, PSP_SEEK_SET);
    uint8_t *buf = malloc((size_t)n);
    if (buf && sceIoRead(fd, buf, (SceSize)n) != (int32_t)n) {
        free(buf);
        buf = NULL;
    }
    sceIoClose(fd);
    *size = (uint32_t)n;
    return buf;
}

/* Returns 0 ok, 1 failed, 77 no decoder. */
static int check_music(int file, const char *wav, int verbose)
{
    char path[96];
    snprintf(path, sizeof path, "disc0:/PSP_GAME/USRDIR/sound/bgm/bgm%02d.at3", file);
    uint32_t size = 0;
    uint8_t *buf = read_disc(path, &size);
    if (!buf) {
        printf("FAIL: %s can't be read\n", path);
        return 1;
    }
    int id = sceAtracSetDataAndGetID(buf, size);
    if (id < 0) {
        free(buf);
        printf("SKIP: no ATRAC3plus decoder (0x%08X)\n", (unsigned)id);
        return 77;
    }
    int end = 0, ls = -1, le = -1, remain = 0;
    sceAtracGetSoundSample(id, &end, &ls, &le);
    sceAtracGetRemainFrame(id, &remain);
    static int16_t frame[2 * 2048];
    /* once through */
    if (ls >= 0)
        sceAtracSetLoopNum(id, 0);
    long total = 0;
    int peak = 0, finish = 0, n = 0;
    enum { WAV_MAX = 44100 * 10 };
    static int16_t pcm[2 * WAV_MAX];
    while (!finish) {
        if (sceAtracDecodeData(id, frame, &n, &finish, &remain) < 0)
            break;
        for (int i = 0; i < 2 * n; i++)
            peak = SDL_max(peak, abs(frame[i]));
        if (total + n <= WAV_MAX)
            memcpy(pcm + 2 * total, frame, (size_t)n * 4);
        total += n;
    }
    sceAtracReleaseAtracID(id);
    bool ok = total == end + 1 && peak > 1000 && remain == -1;
    if (verbose || !ok)
        printf("%s: bgm%02d: %ld samples (file: %d), peak %d, loop %d..%d\n", ok ? "ok" : "FAIL", file,
               total, end + 1, peak, ls, le);
    if (wav) {
        char out[512];
        snprintf(out, sizeof out, "%s/bgm%02d.wav", wav, file);
        write_wav(out, pcm, (int)SDL_min(total, (long)WAV_MAX));
    }
    /* looping: the samples just before the loop end must be followed by the loop
     * start's, with no silence between */
    if (ok && ls >= 0) {
        id = sceAtracSetDataAndGetID(buf, size);
        sceAtracSetLoopNum(id, -1);
        long pos = 0, wrap_at = le + 1;
        int quiet = 0, max_quiet = 0;
        static int16_t first[2 * 4096];
        int got_first = 0;
        while (pos < wrap_at + 44100) {
            if (sceAtracDecodeData(id, frame, &n, &finish, &remain) < 0 || finish || n <= 0)
                break;
            for (int i = 0; i < n; i++) {
                long at = pos + i;
                if (at < 4096 && got_first < 4096)
                    memcpy(first + 2 * got_first++, frame + 2 * i, 4);
                if (at > wrap_at - 4096 && at < wrap_at + 4096) { /* around the wrap */
                    bool q = abs(frame[2 * i]) < 4 && abs(frame[2 * i + 1]) < 4;
                    quiet = q ? quiet + 1 : 0;
                    max_quiet = SDL_max(max_quiet, quiet);
                }
            }
            pos += n;
        }
        sceAtracReleaseAtracID(id);
        bool lok = pos >= wrap_at + 44100 && max_quiet < 512;
        if (verbose || !lok)
            printf("%s: bgm%02d: looped past %ld, longest silence at the wrap %d samples\n",
                   lok ? "ok" : "FAIL", file, wrap_at, max_quiet);
        ok = lok;
    }
    free(buf);
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    const char *disc = getenv("CHEXA_DISC"), *wav = NULL;
    int verbose = 0, ids[256], nids = 0, music = 0;
    if (!disc)
        disc = "/mnt/c/psp/chexa";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--disc") && i + 1 < argc) disc = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav = argv[++i];
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "--music")) music = 1;
        else if (nids < 256) ids[nids++] = (int)strtol(argv[i], NULL, 0);
    }
    if (!psp_io_mount_disc(disc)) {
        printf("SKIP: game files not found at %s\n", disc);
        return 77;
    }
    psp_audio_set_mute(true);
    if (music) {
        if (!nids) {
            ids[nids++] = 1;
            ids[nids++] = 36;
        }
        int failures = 0;
        for (int k = 0; k < nids; k++) {
            int r = check_music(ids[k], wav, verbose);
            if (r == 77)
                return 77;
            failures += r;
        }
        printf("%d music file(s) checked, %d failure(s)\n", nids, failures);
        return failures ? 1 : 0;
    }
    static uint8_t heap_mem[24 << 20];
    heap_init(&g_default_heap, heap_mem, sizeof heap_mem, "default heap");
    fileio_init();
    if (!packindex_read("chexa.idx") || fileset_open("boot", &g_default_heap, 1, 0) < 0) {
        printf("FAIL: the pack index or the boot file set can't be read\n");
        return 1;
    }
    sound_init_offline();
    if (!sound_bank_open(SE_BANK_BATTLE)) {
        printf("FAIL: the battle bank can't be read\n");
        return 1;
    }
    if (!nids) { /* the menu effects and a sweep of the battle ones */
        static const int k_default[] = {SE_CURSOR, SE_START, SE_CANCEL, SE_DECIDE, SE_OK, SE_ERROR, 0x2E, 0x34};
        memcpy(ids, k_default, sizeof k_default);
        nids = (int)(sizeof k_default / sizeof k_default[0]);
    }
    enum { MAX_FRAMES = 44100 * 4 };
    static int16_t pcm[2 * MAX_FRAMES];
    int failures = 0;
    for (int k = 0; k < nids; k++) {
        int length = 0;
        int peak = render(ids[k], pcm, MAX_FRAMES, &length);
        bool ok = peak > 256 && length < MAX_FRAMES - 0x400;
        if (verbose || !ok)
            printf("%s: SE %d: peak %d, %.2f s\n", ok ? "ok" : "FAIL", ids[k], peak, length / 44100.0);
        if (!ok)
            failures++;
        if (wav) {
            char path[512];
            snprintf(path, sizeof path, "%s/se_%03d.wav", wav, ids[k]);
            write_wav(path, pcm, length + 0x200 < MAX_FRAMES ? length + 0x200 : MAX_FRAMES);
        }
        for (int i = 0; i < 40; i++) /* let anything left run out */
            sound_mix(pcm);
    }
    printf("%d effect(s) checked, %d failure(s)\n", nids, failures);
    return failures ? 1 : 0;
}
