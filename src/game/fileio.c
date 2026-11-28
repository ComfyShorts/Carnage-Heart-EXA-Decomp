/* Game file layer used during startup. See fileio.h.
 *
 * On the PSP every retry loop here waits forever for the disc (sleeping 0.1 s and
 * re-checking the drive). On the PC a missing file would hang, so each loop gives up
 * after RETRY_LIMIT tries and logs why. */
#include "fileio.h"

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "packfile.h"
#include "psp_imports.h"

#define USRDIR "disc0:/PSP_GAME/USRDIR"
#define USRDIR_LEN 22
#define RETRY_LIMIT 30
#define OPEN_CACHE_SLOTS 9
#define FILESET_SLOTS 12

/* ---- paths ---- */

/* First byte of a two-byte Shift-JIS character (FUN_08b18e78). */
static int sjis_lead(unsigned char c)
{
    return (c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC);
}

/* FUN_08b1f1b0: drops a leading "./", lower-cases ASCII, turns '\' into '/', and copies
 * Shift-JIS pairs untouched. Returns the length, or -1 if it doesn't fit. */
static int normalise_name(char *out, const char *in, int cap)
{
    if (in[0] == '.' && (in[1] == '/' || in[1] == '\\'))
        in += 2;
    int n = 0;
    while (*in) {
        if (sjis_lead((unsigned char)*in)) {
            if (n + 2 >= cap)
                return -1;
            out[n++] = in[0];
            out[n++] = in[1];
            in += in[1] ? 2 : 1;
        } else {
            if (n + 1 >= cap)
                return -1;
            char c = *in++;
            out[n++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c == '\\' ? '/' : c;
        }
    }
    out[n] = '\0';
    return n;
}

/* FUN_08b1f010: true if the name ends in a path separator (a directory). */
static int names_directory(const char *s)
{
    const char *last = s;
    while (*s) {
        last = s;
        if (sjis_lead((unsigned char)*s)) {
            if (!s[1])
                return 0;
            s += 2;
        } else {
            s++;
        }
    }
    return *last == '/' || *last == '\\';
}

/* FUN_08b1f330: the full device path, "disc0:/PSP_GAME/USRDIR/<lower-cased name>". */
static int disc_path(char *out, const char *name, int cap)
{
    int n;
    if (strncmp(name, USRDIR, USRDIR_LEN) == 0) {
        if (cap < USRDIR_LEN + 1)
            return -1;
        memcpy(out, name, USRDIR_LEN);
        name += USRDIR_LEN;
        n = USRDIR_LEN;
    } else {
        if (cap < USRDIR_LEN + 2)
            return -1;
        memcpy(out, USRDIR, USRDIR_LEN);
        n = USRDIR_LEN;
        if (*name != '/' && *name != '\\')
            out[n++] = '/';
    }
    while (*name) {
        if (sjis_lead((unsigned char)*name)) {
            if (n + 2 >= cap)
                return -1;
            out[n++] = name[0];
            out[n++] = name[1];
            name += name[1] ? 2 : 1;
        } else {
            if (n + 1 >= cap)
                return -1;
            char c = *name++;
            out[n++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
        }
    }
    out[n] = '\0';
    return n + 1;
}

/* ---- open files ---- */

static SceUID g_fileio_mtx; /* "fileio_mtx": the request queue */
static SceUID g_read_mtx;   /* "fileio_mtx2": one read at a time */

static struct {
    char path[0x80];
    SceUID fd;
} g_open_cache[OPEN_CACHE_SLOTS];

void fileio_init(void)
{
    for (int i = 0; i < OPEN_CACHE_SLOTS; i++) {
        g_open_cache[i].path[0] = '\0';
        g_open_cache[i].fd = -1;
    }
    /* The game also resets its async request queue and file-set slots here
     * (FUN_08b146e8, FUN_08b12bc0, FUN_08b117b0); both start empty in this port. */
    if (g_fileio_mtx)
        SDL_Log("[fileio] INIT_MULTI_THREAD: already initialised");
    SceUID m = sceKernelCreateMutex("fileio_mtx", 0x200, 0, NULL);
    if (m < 1)
        SDL_Log("[fileio] INIT_MULTI_THREAD: can't create fileio_mtx (0x%08X)", (unsigned)m);
    else
        g_fileio_mtx = m;
    m = sceKernelCreateMutex("fileio_mtx2", 0x200, 0, NULL);
    if (m < 1)
        SDL_Log("[fileio] INIT_READ_MULTI_THREAD: can't create fileio_mtx2 (0x%08X)", (unsigned)m);
    else
        g_read_mtx = m;
}

static void lock(SceUID m, const char *what)
{
    if (sceKernelLockMutexCB(m, 1, NULL) != 0)
        SDL_Log("[fileio] %s failed", what);
}

static void unlock(SceUID m, const char *what)
{
    if (sceKernelUnlockMutex(m, 1) != 0)
        SDL_Log("[fileio] %s failed", what);
}

/* After a failed disc access: sleep, then wait for the drive (FUN_08b11b3c). */
static void retry_pause(void)
{
    sceKernelDelayThreadCB(100000);
    sceUmdWaitDriveStatCB(PSP_UMD_READY, 1000000);
}

/* FUN_08b12d64: waits until the async loader has no request in flight. The async
 * loader isn't written yet, so the queue is always idle; the lock is still taken. */
static void wait_loader_idle(void)
{
    lock(g_fileio_mtx, "LOCK");
    unlock(g_fileio_mtx, "UNLOCK");
}

/* FUN_08b11c24: returns an fd for a game path, from the cache or a fresh open. */
static SceUID open_file(const char *name, int keep_open)
{
    char path[256];
    if (disc_path(path, name, sizeof path) < 0)
        return SCE_ERROR_ERRNO_ENAMETOOLONG;
    int free_slot = -1;
    for (int i = 0; i < OPEN_CACHE_SLOTS; i++) {
        if (!g_open_cache[i].path[0]) {
            if (free_slot < 0)
                free_slot = i;
            continue;
        }
        if (strcmp(g_open_cache[i].path, path) == 0)
            return g_open_cache[i].fd;
    }
    SceUID fd = -1;
    for (int tries = 0;; tries++) {
        fd = sceIoOpenAsync(path, PSP_O_RDONLY, 0);
        if (fd >= 0) {
            SceInt64 res;
            int32_t w = sceIoWaitAsyncCB(fd, &res);
            if (w == 0 && res < 0)
                sceIoClose(fd); /* the fd of a failed async open is still allocated */
            fd = w == 0 ? (SceUID)res : w;
        }
        if (fd >= 0)
            break;
        if (tries + 1 >= RETRY_LIMIT) {
            SDL_Log("[fileio] can't open %s (0x%08X); giving up", path, (unsigned)fd);
            return fd;
        }
        retry_pause();
    }
    if (keep_open && free_slot >= 0 && strlen(path) < sizeof g_open_cache[0].path) {
        memcpy(g_open_cache[free_slot].path, path, strlen(path) + 1);
        g_open_cache[free_slot].fd = fd;
    }
    return fd;
}

/* FUN_08b11f60: closes an fd unless the cache holds it. */
static void close_file(SceUID fd)
{
    if (fd < 0)
        return;
    for (int i = 0; i < OPEN_CACHE_SLOTS; i++)
        if (g_open_cache[i].fd == fd)
            return;
    if (sceIoCloseAsync(fd) >= 0) {
        SceInt64 res;
        sceIoWaitAsyncCB(fd, &res);
    }
}

/* FUN_08b12394: the size by seeking to the end. */
static int32_t file_size(SceUID fd)
{
    if (sceIoLseek(fd, 0, PSP_SEEK_SET) < 0)
        return -1;
    SceOff end = sceIoLseek(fd, 0, PSP_SEEK_END);
    if (end < 0)
        return -1;
    if (sceIoLseek(fd, 0, PSP_SEEK_SET) < 0)
        return -1;
    return (int32_t)end;
}

/* FUN_08b12068 with wait = 1. */
static int seek_wait(SceUID fd, uint32_t offset)
{
    if (sceIoLseekAsync(fd, offset, PSP_SEEK_SET) < 0)
        return -1;
    SceInt64 res;
    return sceIoWaitAsyncCB(fd, &res) == 0 && res >= 0 ? 0 : -1;
}

/* FUN_08b12250 with wait = 1. */
static int read_wait(SceUID fd, void *buf, uint32_t size)
{
    sceKernelDcacheWritebackInvalidateAll();
    if (sceIoReadAsync(fd, buf, size) < 0)
        return -1;
    SceInt64 res;
    if (sceIoWaitAsyncCB(fd, &res) != 0)
        return -1;
    if (res != (SceInt64)size) {
        if (res > (SceInt64)size)
            SDL_Log("[fileio] Read: ReadSize Over!!");
        return -1;
    }
    return 0;
}

static int check_name(char *out, const char *name, const char *func)
{
    int len = normalise_name(out, name, 1024);
    if (len < 1 || len > 127) {
        SDL_Log("[fileio] %s: assertion 0 < iNameLen && iNameLen < 128 failed for \"%s\"", func, name);
        return -1;
    }
    return names_directory(out) ? -1 : 0;
}

int32_t fileio_get_size(const char *name)
{
    char norm[1024];
    if (check_name(norm, name, "GetFileSizeDirect") < 0)
        return -1;
    wait_loader_idle();
    SceUID fd = open_file(norm, 0);
    if (fd < 0)
        return fd;
    int32_t size;
    for (int tries = 0; (size = file_size(fd)) < 0; tries++) {
        if (tries + 1 >= RETRY_LIMIT)
            break;
        retry_pause();
    }
    close_file(fd);
    return size;
}

int32_t fileio_read_at(const char *name, void *buf, uint32_t offset, uint32_t size, int keep_open)
{
    char norm[1024];
    if (check_name(norm, name, "ReadFileInternal") < 0)
        return -1;
    wait_loader_idle();
    SceUID fd = open_file(norm, keep_open);
    if (fd < 0)
        return fd;
    lock(g_read_mtx, "READ_LOCK");
    int ok = 0;
    for (int tries = 0; tries < RETRY_LIMIT && !ok; tries++) {
        if (tries)
            retry_pause();
        ok = seek_wait(fd, offset) == 0 && read_wait(fd, buf, size) == 0;
    }
    unlock(g_read_mtx, "READ_UNLOCK");
    close_file(fd);
    if (!ok) {
        SDL_Log("[fileio] can't read %u bytes at 0x%X from %s; giving up", (unsigned)size,
                (unsigned)offset, norm);
        return -1;
    }
    return (int32_t)size;
}

int32_t fileio_read_head(const char *name, void *buf, int32_t cap)
{
    int32_t size = fileio_get_size(name);
    if (size < 0)
        return -1;
    if (cap < size)
        size = cap;
    return fileio_read_at(name, buf, 0, (uint32_t)size, 0);
}

/* ---- pack index ---- */

/* chexa.idx header (all offsets from the start of the index). */
typedef struct PackIndexHeader {
    char magic[4];          /* "PIDX" */
    uint32_t archive_off;   /* 32-byte archive records (common.dat, voice.dat) */
    uint32_t archive_count;
    uint32_t entry_off;     /* PackEntry tree */
    uint32_t entry_count;
    uint32_t root_count;
    uint32_t fileset_off;   /* u32 count, then that many offsets (from fileset_off) */
    uint32_t fileset_size;
    uint32_t string_off;
    uint32_t string_size;
} PackIndexHeader;

static uint8_t *g_index;
static uint32_t g_index_size;
static const uint32_t *g_set_offsets; /* FileSetInfo offsets, sorted by name */
static uint32_t g_set_count;

const char *packindex_string(uint32_t offset)
{
    const PackIndexHeader *h = (const PackIndexHeader *)g_index;
    if (!g_index || (uint64_t)h->string_off + offset >= g_index_size)
        return "";
    return (const char *)g_index + h->string_off + offset;
}

/* FUN_08b1ea38 turns the index's offsets into pointers in place. This port keeps the
 * offsets (so the index layout works on 64-bit hosts too) and checks them instead. */
static int index_check(void)
{
    const PackIndexHeader *h = (const PackIndexHeader *)g_index;
    uint64_t size = g_index_size;
    if (size < sizeof *h || memcmp(h->magic, "PIDX", 4) != 0)
        return 0;
    if ((uint64_t)h->archive_off + (uint64_t)h->archive_count * 32 > size ||
        (uint64_t)h->entry_off + (uint64_t)h->entry_count * sizeof(PackEntry) > size ||
        h->string_off >= size || (uint64_t)h->fileset_off + 4 > size)
        return 0;
    uint32_t n = *(const uint32_t *)(g_index + h->fileset_off);
    if ((uint64_t)h->fileset_off + 4 + (uint64_t)n * 4 > size)
        return 0;
    const uint32_t *offs = (const uint32_t *)(g_index + h->fileset_off + 4);
    for (uint32_t i = 0; i < n; i++)
        if ((uint64_t)h->fileset_off + offs[i] + sizeof(FileSetInfo) > size)
            return 0;
    g_set_offsets = offs;
    g_set_count = n;
    return 1;
}

int packindex_read(const char *name)
{
    if (g_index) {
        heap_free(&g_default_heap, g_index);
        g_index = NULL;
    }
    int32_t size = fileio_get_size(name);
    if (size < 1)
        return 0;
    g_index = heap_alloc(&g_default_heap, ((uint32_t)size + 63) & ~63u, 64, "PackFile", 0);
    if (!g_index)
        return 0;
    int32_t got = fileio_read_head(name, g_index, size);
    if (got != size)
        SDL_Log("[fileio] PackFile::ReadIndex: iSize = %d, lSize = %ld", (int)got, (long)size);
    g_index_size = (uint32_t)size;
    if (!index_check()) {
        SDL_Log("[fileio] %s: not a valid pack index", name);
        return 0;
    }
    const PackIndexHeader *h = (const PackIndexHeader *)g_index;
    SDL_Log("[fileio] pack index: %u entries, %u roots, %u archives, %u file sets",
            (unsigned)h->entry_count, (unsigned)h->root_count, (unsigned)h->archive_count,
            (unsigned)g_set_count);
    return 1;
}

static const FileSetInfo *set_at(uint32_t i)
{
    const PackIndexHeader *h = (const PackIndexHeader *)g_index;
    return (const FileSetInfo *)(g_index + h->fileset_off + g_set_offsets[i]);
}

const FileSetInfo *packindex_find_set(const char *name)
{
    if (!g_index)
        return NULL;
    uint32_t lo = 0, hi = g_set_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        int c = strcmp(packindex_string(set_at(mid)->name), name);
        if (c == 0)
            return set_at(mid);
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return NULL;
}

/* ---- file sets ---- */

/* Header at the start of a loaded file set. */
typedef struct FstsHeader {
    char magic[4];       /* "FSTS" */
    uint32_t count;
    uint32_t table_off;  /* FstsMember[count], sorted by name */
    uint32_t string_off;
} FstsHeader;

typedef struct FstsMember {
    uint32_t name;    /* offset from string_off */
    uint32_t offset;  /* from the start of the set */
    uint32_t size;    /* unpacked size */
    uint32_t stored;  /* ARZ size, or 0 when stored as-is */
} FstsMember;

typedef struct FileSetSlot {
    const FileSetInfo *info;
    const FstsHeader *header;
    const FstsMember *members;
    uint8_t *data;
    GameHeap *heap;
    uint32_t loaded;
    int ready;
} FileSetSlot;

static FileSetSlot g_sets[FILESET_SLOTS];

/* MapFileSetHeader (FUN_08b14544), with bounds checks in place of pointer fix-ups. */
static int map_header(FileSetSlot *s)
{
    const FstsHeader *h = (const FstsHeader *)s->data;
    if (s->loaded < sizeof *h || memcmp(h->magic, "FSTS", 4) != 0) {
        SDL_Log("[fileio] MapFileSetHeader: FILSET_HEADER not found!! (ptr = %p)", (void *)s->data);
        return 0;
    }
    if ((uint64_t)h->table_off + (uint64_t)h->count * sizeof(FstsMember) > s->loaded ||
        h->string_off >= s->loaded)
        return 0;
    const FstsMember *m = (const FstsMember *)(s->data + h->table_off);
    for (uint32_t i = 0; i < h->count; i++)
        if ((uint64_t)h->string_off + m[i].name >= s->loaded ||
            (uint64_t)m[i].offset + (m[i].stored ? m[i].stored : m[i].size) > s->loaded)
            return 0;
    s->header = h;
    s->members = m;
    return 1;
}

int fileset_open(const char *name, GameHeap *heap, int from_top, int async)
{
    if (!heap)
        heap = &g_default_heap;
    const FileSetInfo *info = packindex_find_set(name);
    if (!info)
        return -1;
    int slot = -1;
    for (int i = 0; i < FILESET_SLOTS && slot < 0; i++)
        if (!g_sets[i].info)
            slot = i;
    if (slot < 0)
        return -1;
    FileSetSlot *s = &g_sets[slot];
    memset(s, 0, sizeof *s);
    s->info = info;
    s->heap = heap;
    s->data = heap_alloc(heap, (info->size + 63) & ~63u, 64, "FILSETMAN", from_top);
    if (!s->data) {
        s->info = NULL;
        return -1;
    }
    if (async) {
        /* The game queues the read (FUN_08b13014) and maps the header in a completion
         * callback. Not needed for startup. */
        SDL_Log("[fileio] OpenFileSet(\"%s\"): async loading not implemented", name);
        heap_free(heap, s->data);
        s->info = NULL;
        return -1;
    }
    const char *archive = packindex_string(info->archive);
    int32_t got = -1;
    for (int tries = 0; tries < RETRY_LIMIT; tries++) {
        got = fileio_read_at(archive, s->data, info->offset, info->size, 0);
        if (got >= (int32_t)info->size)
            break;
        sceKernelDelayThreadCB(100000);
    }
    if (got != (int32_t)info->size || !(s->loaded = info->size, map_header(s))) {
        SDL_Log("[fileio] OpenFileSet(\"%s\") failed", name);
        heap_free(heap, s->data);
        memset(s, 0, sizeof *s);
        return -1;
    }
    s->ready = 1;
    SDL_Log("[fileio] file set \"%s\": %u files, %u KiB from %s+0x%X (slot %d)", name,
            (unsigned)s->header->count, (unsigned)(info->size / 1024), archive, (unsigned)info->offset, slot);
    return slot;
}

int fileset_close(int slot)
{
    if (slot < 0 || slot >= FILESET_SLOTS || !g_sets[slot].info)
        return -1; /* FUN_08b1431c finds no set */
    FileSetSlot *s = &g_sets[slot];
    /* The game waits for an async read to finish first (FUN_08b12ec4); reads here are
     * synchronous. */
    SDL_Log("[fileio] file set \"%s\" closed (slot %d)", packindex_string(s->info->name), slot);
    if (s->data)
        heap_free(s->heap, s->data);
    memset(s, 0, sizeof *s); /* FUN_08b14304 */
    return 0;
}

/* FUN_08b145ec / FUN_08b14464: a member of any open set, by binary search. */
static const FstsMember *find_member(const char *name, const FileSetSlot **owner)
{
    for (int i = 0; i < FILESET_SLOTS; i++) {
        const FileSetSlot *s = &g_sets[i];
        if (!s->header)
            continue;
        uint32_t lo = 0, hi = s->header->count;
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            const char *n = (const char *)s->data + s->header->string_off + s->members[mid].name;
            int c = strcmp(n, name);
            if (c == 0) {
                *owner = s;
                return &s->members[mid];
            }
            if (c < 0)
                lo = mid + 1;
            else
                hi = mid;
        }
    }
    return NULL;
}

/* ReadFileFromFileSet (FUN_08b14778). */
static int32_t read_from_set(const char *name, void *buf, uint32_t cap)
{
    const FileSetSlot *s;
    const FstsMember *m = find_member(name, &s);
    if (!m || cap < m->size)
        return -1;
    const uint8_t *src = s->data + m->offset;
    if (!m->stored) {
        memcpy(buf, src, m->size);
        return (int32_t)m->size;
    }
    long n = arz_decode(src, m->stored, buf, cap);
    if (n != (long)m->size) {
        SDL_Log("[fileio] ReadFileFromFileSet: %s unpacked to %ld bytes, expected %u", name, n,
                (unsigned)m->size);
        return -1;
    }
    return (int32_t)n;
}

/* Loose files in the pack (chexa.idx entries outside any file set), for ReadFile's
 * fall-back. Opened on first use. */
static Pack g_loose;
static int g_loose_state; /* 0 not tried, 1 open, -1 failed */

static const PackEntry *loose_entry(const char *norm)
{
    if (g_loose_state == 0)
        g_loose_state = pack_open(&g_loose, "disc0:/PSP_GAME/USRDIR") < 0 ? -1 : 1;
    return g_loose_state > 0 ? pack_find(&g_loose, norm) : NULL;
}

int32_t fileio_file_size(const char *name)
{
    char norm[1024];
    if (check_name(norm, name, "GetFileSize") < 0)
        return -1;
    const FileSetSlot *s;
    const FstsMember *m = find_member(norm, &s);
    if (m)
        return (int32_t)m->size;
    const PackEntry *e = loose_entry(norm);
    return e && !e->is_dir ? (int32_t)e->size : -1;
}

int32_t fileio_read_file(const char *name, void *buf, uint32_t cap)
{
    char norm[1024];
    if (check_name(norm, name, "ReadFile") < 0)
        return -1;
    int32_t n = read_from_set(norm, buf, cap);
    if (n > 0)
        return n;
    /* Then the loose pack entries (the PackFile branch of FUN_08b1385c). */
    const PackEntry *e = loose_entry(norm);
    if (e && !e->is_dir) {
        long r = pack_read(&g_loose, e, buf, cap);
        if (r > 0)
            return (int32_t)r;
    }
    SDL_Log("[fileio] ReadFile(\"%s\"): not found in the file sets or the pack", norm);
    return -1;
}
