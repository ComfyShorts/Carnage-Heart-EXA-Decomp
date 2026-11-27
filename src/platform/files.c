/* PSP shim: File I/O and UMD.
 *
 * PSP device paths ("disc0:/PSP_GAME/USRDIR/chexa.idx") are served from host folders
 * mounted with psp_io_mount(). The game reads its data through disc0:, which is the disc
 * root (the folder that holds PSP_GAME/). Lookups are case-insensitive, like the UMD's
 * ISO 9660 file system, and ".." can't climb above a device's root.
 *
 * The async calls finish their work immediately and keep the result for
 * sceIoWaitAsyncCB / sceIoPollAsync, which is all the game expects of them.
 * Not implemented: raw block devices (umd0:, umd1:) and I/O callbacks. */
#include "psp_imports.h"
#include "psp_shim.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <SDL3/SDL.h>

#define MAX_MOUNTS 8
#define MAX_FDS 64
#define FD_BASE 3 /* 0-2 are the PSP's stdio handles */

typedef struct Mount {
    char device[16];  /* without the colon, e.g. "disc0" */
    char root[1024];  /* host directory */
    bool writable;
} Mount;

enum { SLOT_FREE, SLOT_FILE, SLOT_DIR, SLOT_FAILED };

typedef struct FdSlot {
    int kind;
    int host_fd;
    DIR *dir;
    int dir_dots;             /* "." and ".." still to report */
    char path[1100];          /* host path, for Dread's stat */
    char psp_path[300];       /* for logging */
    bool async_pending;
    bool close_on_collect;    /* CloseAsync: free the slot once the result is collected */
    SceInt64 async_result;
} FdSlot;

static Mount g_mounts[MAX_MOUNTS];
static int g_mount_count;
static FdSlot g_fds[MAX_FDS];
static SDL_Mutex *g_lock;

static bool log_all(void)
{
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("PSP_SHIM_LOG");
        v = e && strcmp(e, "all") == 0;
    }
    return v;
}

static void lock(void)
{
    if (!g_lock)
        g_lock = SDL_CreateMutex();
    SDL_LockMutex(g_lock);
}

static void unlock(void)
{
    SDL_UnlockMutex(g_lock);
}

static int32_t errno_to_sce(int e)
{
    switch (e) {
    case ENOENT: return SCE_ERROR_ERRNO_ENOENT;
    case EACCES: case EPERM: return SCE_ERROR_ERRNO_EACCES;
    case EEXIST: return SCE_ERROR_ERRNO_EEXIST;
    case ENOTDIR: return SCE_ERROR_ERRNO_ENOTDIR;
    case EISDIR: return SCE_ERROR_ERRNO_EISDIR;
    case EINVAL: return SCE_ERROR_ERRNO_EINVAL;
    case EMFILE: case ENFILE: return SCE_ERROR_ERRNO_EMFILE;
    case EROFS: return SCE_ERROR_ERRNO_EROFS;
    default: return SCE_ERROR_ERRNO_EIO;
    }
}

/* ---- mounts and path resolution ---- */

bool psp_io_mount(const char *device, const char *host_dir, bool writable)
{
    struct stat st;
    if (g_mount_count >= MAX_MOUNTS || strlen(device) >= sizeof g_mounts[0].device ||
        strlen(host_dir) >= sizeof g_mounts[0].root)
        return false;
    if (stat(host_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        SDL_Log("[io] can't mount %s: %s is not a directory", device, host_dir);
        return false;
    }
    Mount *m = &g_mounts[g_mount_count++];
    snprintf(m->device, sizeof m->device, "%s", device);
    snprintf(m->root, sizeof m->root, "%s", host_dir);
    size_t n = strlen(m->root);
    while (n > 1 && m->root[n - 1] == '/')
        m->root[--n] = '\0';
    m->writable = writable;
    SDL_Log("[io] mounted %s: -> %s%s", m->device, m->root, writable ? "" : " (read-only)");
    return true;
}

static const Mount *find_mount(const char *dev, size_t len)
{
    for (int i = 0; i < g_mount_count; i++)
        if (strlen(g_mounts[i].device) == len && strncasecmp(g_mounts[i].device, dev, len) == 0)
            return &g_mounts[i];
    return NULL;
}

/* Append one path component to `out`, matching an existing entry case-insensitively
 * when there's no exact match. */
static void append_component(char *out, size_t cap, const char *comp, size_t len)
{
    size_t base = strlen(out);
    snprintf(out + base, cap - base, "/%.*s", (int)len, comp);
    struct stat st;
    if (stat(out, &st) == 0)
        return;
    out[base] = '\0';
    DIR *d = opendir(base ? out : "/");
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (strlen(e->d_name) == len && strncasecmp(e->d_name, comp, len) == 0) {
                snprintf(out + base, cap - base, "/%s", e->d_name);
                closedir(d);
                return;
            }
        }
        closedir(d);
    }
    snprintf(out + base, cap - base, "/%.*s", (int)len, comp);
}

/* Map "dev:/a/b" to a host path. Returns the mount, or NULL with *err set. */
static const Mount *resolve(const char *psp, char *out, size_t cap, int32_t *err)
{
    const char *colon = psp ? strchr(psp, ':') : NULL;
    if (!colon) {
        *err = SCE_ERROR_ERRNO_ENOENT;
        return NULL;
    }
    const Mount *m = find_mount(psp, (size_t)(colon - psp));
    if (!m) {
        *err = SCE_ERROR_ERRNO_ENODEV;
        return NULL;
    }
    /* Collect the components, applying "." and ".." without leaving the root. */
    const char *comps[64];
    size_t lens[64];
    int n = 0;
    for (const char *p = colon + 1; *p;) {
        while (*p == '/' || *p == '\\')
            p++;
        const char *s = p;
        while (*p && *p != '/' && *p != '\\')
            p++;
        size_t len = (size_t)(p - s);
        if (len == 0 || (len == 1 && s[0] == '.'))
            continue;
        if (len == 2 && s[0] == '.' && s[1] == '.') {
            if (n > 0)
                n--;
            continue;
        }
        if (n == 64) {
            *err = SCE_ERROR_ERRNO_EINVAL;
            return NULL;
        }
        comps[n] = s;
        lens[n++] = len;
    }
    snprintf(out, cap, "%s", m->root);
    if (strcmp(out, "/") == 0)
        out[0] = '\0';
    for (int i = 0; i < n; i++)
        append_component(out, cap, comps[i], lens[i]);
    if (!out[0])
        snprintf(out, cap, "/");
    return m;
}

/* ---- fd table ---- */

static int alloc_slot(void)
{
    for (int i = 0; i < MAX_FDS; i++)
        if (g_fds[i].kind == SLOT_FREE) {
            memset(&g_fds[i], 0, sizeof g_fds[i]);
            g_fds[i].host_fd = -1;
            return i;
        }
    return -1;
}

static FdSlot *get_slot(SceUID fd)
{
    int i = fd - FD_BASE;
    if (i < 0 || i >= MAX_FDS || g_fds[i].kind == SLOT_FREE)
        return NULL;
    return &g_fds[i];
}

static void free_slot(FdSlot *s)
{
    if (s->host_fd >= 0)
        close(s->host_fd);
    if (s->dir)
        closedir(s->dir);
    memset(s, 0, sizeof *s);
    s->kind = SLOT_FREE;
}

static void to_psp_time(time_t t, ScePspDateTime *out)
{
    struct tm tm;
    localtime_r(&t, &tm);
    out->year = (uint16_t)(tm.tm_year + 1900);
    out->month = (uint16_t)(tm.tm_mon + 1);
    out->day = (uint16_t)tm.tm_mday;
    out->hour = (uint16_t)tm.tm_hour;
    out->minute = (uint16_t)tm.tm_min;
    out->second = (uint16_t)tm.tm_sec;
    out->microsecond = 0;
}

static void fill_stat(const struct stat *st, bool writable, SceIoStat *out)
{
    memset(out, 0, sizeof *out);
    int perm = writable ? 0777 : 0555;
    if (S_ISDIR(st->st_mode)) {
        out->st_mode = FIO_S_IFDIR | perm;
        out->st_attr = FIO_SO_IFDIR;
    } else {
        out->st_mode = FIO_S_IFREG | perm;
        out->st_attr = FIO_SO_IFREG;
        out->st_size = st->st_size;
    }
    to_psp_time(st->st_ctime, &out->sce_ctime);
    to_psp_time(st->st_atime, &out->sce_atime);
    to_psp_time(st->st_mtime, &out->sce_mtime);
}

/* ---- synchronous operations (callers hold the lock) ---- */

static SceUID do_open(const char *file, int flags, SceMode mode, bool keep_failed)
{
    char host[1100];
    int32_t err = 0;
    const Mount *m = resolve(file, host, sizeof host, &err);
    int access = flags & PSP_O_RDWR;
    bool wants_write = access != PSP_O_RDONLY || (flags & (PSP_O_CREAT | PSP_O_TRUNC | PSP_O_APPEND));
    int hfd = -1;
    if (m && wants_write && !m->writable)
        err = SCE_ERROR_ERRNO_EROFS;
    else if (m) {
        int oflags = access == PSP_O_RDWR ? O_RDWR : access == PSP_O_WRONLY ? O_WRONLY : O_RDONLY;
        if (flags & PSP_O_APPEND) oflags |= O_APPEND;
        if (flags & PSP_O_CREAT) oflags |= O_CREAT;
        if (flags & PSP_O_TRUNC) oflags |= O_TRUNC;
        if (flags & PSP_O_EXCL) oflags |= O_EXCL;
        hfd = open(host, oflags | O_CLOEXEC, mode ? (mode & 0777) : 0666);
        struct stat st;
        if (hfd < 0)
            err = errno_to_sce(errno);
        else if (fstat(hfd, &st) == 0 && S_ISDIR(st.st_mode)) {
            close(hfd);
            hfd = -1;
            err = SCE_ERROR_ERRNO_EISDIR;
        }
    }
    if (hfd < 0 && !keep_failed) {
        SDL_Log("[io] open %s (flags 0x%X) failed: 0x%08X", file ? file : "(null)", flags, (unsigned)err);
        return err;
    }
    int i = alloc_slot();
    if (i < 0) {
        if (hfd >= 0)
            close(hfd);
        return SCE_ERROR_ERRNO_EMFILE;
    }
    FdSlot *s = &g_fds[i];
    snprintf(s->psp_path, sizeof s->psp_path, "%s", file ? file : "");
    if (hfd < 0) {
        s->kind = SLOT_FAILED;
        s->async_result = err;
        SDL_Log("[io] open %s (flags 0x%X) failed: 0x%08X", s->psp_path, flags, (unsigned)err);
    } else {
        s->kind = SLOT_FILE;
        s->host_fd = hfd;
        s->async_result = FD_BASE + i;
        SDL_Log("[io] open %s -> fd %d", s->psp_path, FD_BASE + i);
    }
    return FD_BASE + i;
}

static int32_t do_read(FdSlot *s, void *data, SceSize size)
{
    if (s->kind != SLOT_FILE)
        return SCE_KERNEL_ERROR_BADF;
    size_t done = 0;
    while (done < size) {
        ssize_t r = read(s->host_fd, (char *)data + done, size - done);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return errno_to_sce(errno);
        }
        if (r == 0)
            break;
        done += (size_t)r;
    }
    if (log_all())
        SDL_Log("[io] read %s: %u of %u bytes", s->psp_path, (unsigned)done, (unsigned)size);
    return (int32_t)done;
}

static int32_t do_write(FdSlot *s, const void *data, SceSize size)
{
    if (s->kind != SLOT_FILE)
        return SCE_KERNEL_ERROR_BADF;
    size_t done = 0;
    while (done < size) {
        ssize_t r = write(s->host_fd, (const char *)data + done, size - done);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return errno_to_sce(errno);
        }
        done += (size_t)r;
    }
    return (int32_t)done;
}

static SceOff do_seek(FdSlot *s, SceOff offset, int whence)
{
    if (s->kind != SLOT_FILE)
        return SCE_KERNEL_ERROR_BADF;
    int w = whence == PSP_SEEK_SET ? SEEK_SET : whence == PSP_SEEK_CUR ? SEEK_CUR
          : whence == PSP_SEEK_END ? SEEK_END : -1;
    if (w < 0)
        return SCE_ERROR_ERRNO_EINVAL;
    off_t r = lseek(s->host_fd, (off_t)offset, w);
    return r < 0 ? errno_to_sce(errno) : (SceOff)r;
}

static void set_async(FdSlot *s, SceInt64 result)
{
    s->async_pending = true;
    s->async_result = result;
}

/* ---- sceIo ---- */

SceUID sceIoOpen(const char *file, int flags, SceMode mode)
{
    lock();
    SceUID r = do_open(file, flags, mode, false);
    unlock();
    PSP_TRACE(r, "\"%s\", 0x%X, 0%o", file ? file : "(null)", flags, (unsigned)mode);
    return r;
}

SceUID sceIoOpenAsync(const char *file, int flags, SceMode mode)
{
    lock();
    /* The fd is handed out even when the open fails; the error comes from the wait. */
    SceUID r = do_open(file, flags, mode, true);
    FdSlot *s = get_slot(r);
    if (s)
        s->async_pending = true;
    unlock();
    PSP_TRACE(r, "\"%s\", 0x%X, 0%o", file ? file : "(null)", flags, (unsigned)mode);
    return r;
}

int32_t sceIoClose(SceUID fd)
{
    lock();
    FdSlot *s = get_slot(fd);
    int32_t r = SCE_KERNEL_ERROR_BADF;
    if (s && s->kind != SLOT_DIR) {
        free_slot(s);
        r = 0;
    }
    unlock();
    PSP_TRACE(r, "%d", (int)fd);
    return r;
}

int32_t sceIoCloseAsync(SceUID fd)
{
    lock();
    FdSlot *s = get_slot(fd);
    int32_t r = SCE_KERNEL_ERROR_BADF;
    if (s && s->kind != SLOT_DIR) {
        if (s->host_fd >= 0)
            close(s->host_fd);
        s->host_fd = -1;
        s->close_on_collect = true;
        set_async(s, 0);
        r = 0;
    }
    unlock();
    PSP_TRACE(r, "%d", (int)fd);
    return r;
}

int32_t sceIoRead(SceUID fd, void *data, SceSize size)
{
    lock();
    FdSlot *s = get_slot(fd);
    int32_t r = s ? do_read(s, data, size) : SCE_KERNEL_ERROR_BADF;
    unlock();
    PSP_TRACE(r, "%d, %p, %u", (int)fd, data, (unsigned)size);
    return r;
}

int32_t sceIoReadAsync(SceUID fd, void *data, SceSize size)
{
    lock();
    FdSlot *s = get_slot(fd);
    int32_t r = SCE_KERNEL_ERROR_BADF;
    if (s && s->kind == SLOT_FILE) {
        set_async(s, do_read(s, data, size));
        r = 0;
    }
    unlock();
    PSP_TRACE(r, "%d, %p, %u", (int)fd, data, (unsigned)size);
    return r;
}

int32_t sceIoWrite(SceUID fd, const void *data, SceSize size)
{
    if (fd == 1 || fd == 2) { /* PSP stdout / stderr */
        fwrite(data, 1, size, fd == 1 ? stdout : stderr);
        PSP_TRACE(size, "%d, %p, %u", (int)fd, data, (unsigned)size);
        return (int32_t)size;
    }
    lock();
    FdSlot *s = get_slot(fd);
    int32_t r = s ? do_write(s, data, size) : SCE_KERNEL_ERROR_BADF;
    unlock();
    PSP_TRACE(r, "%d, %p, %u", (int)fd, data, (unsigned)size);
    return r;
}

int32_t sceIoWriteAsync(SceUID fd, const void *data, SceSize size)
{
    lock();
    FdSlot *s = get_slot(fd);
    int32_t r = SCE_KERNEL_ERROR_BADF;
    if (s && s->kind == SLOT_FILE) {
        set_async(s, do_write(s, data, size));
        r = 0;
    }
    unlock();
    PSP_TRACE(r, "%d, %p, %u", (int)fd, data, (unsigned)size);
    return r;
}

SceOff sceIoLseek(SceUID fd, SceOff offset, int whence)
{
    lock();
    FdSlot *s = get_slot(fd);
    SceOff r = s ? do_seek(s, offset, whence) : SCE_KERNEL_ERROR_BADF;
    unlock();
    PSP_TRACE(r, "%d, %lld, %d", (int)fd, (long long)offset, whence);
    return r;
}

int32_t sceIoLseekAsync(SceUID fd, SceOff offset, int whence)
{
    lock();
    FdSlot *s = get_slot(fd);
    int32_t r = SCE_KERNEL_ERROR_BADF;
    if (s && s->kind == SLOT_FILE) {
        set_async(s, do_seek(s, offset, whence));
        r = 0;
    }
    unlock();
    PSP_TRACE(r, "%d, %lld, %d", (int)fd, (long long)offset, whence);
    return r;
}

static int32_t collect_async(SceUID fd, SceInt64 *res)
{
    lock();
    FdSlot *s = get_slot(fd);
    int32_t r;
    if (!s)
        r = SCE_KERNEL_ERROR_BADF;
    else if (!s->async_pending)
        r = SCE_KERNEL_ERROR_NOASYNC;
    else {
        if (res)
            *res = s->async_result;
        s->async_pending = false;
        if (s->close_on_collect)
            free_slot(s);
        r = 0;
    }
    unlock();
    return r;
}

int32_t sceIoWaitAsyncCB(SceUID fd, SceInt64 *res)
{
    psp_kernel_run_callbacks();
    int32_t r = collect_async(fd, res);
    PSP_TRACE(r, "%d, -> %lld", (int)fd, res && r == 0 ? (long long)*res : 0LL);
    return r;
}

int32_t sceIoPollAsync(SceUID fd, SceInt64 *res)
{
    /* Work is never actually in flight, so a poll always finds it done (0). */
    int32_t r = collect_async(fd, res);
    PSP_TRACE(r, "%d, -> %lld", (int)fd, res && r == 0 ? (long long)*res : 0LL);
    return r;
}

SceUID sceIoDopen(const char *dirname)
{
    char host[1100];
    int32_t err = 0;
    lock();
    SceUID r;
    const Mount *m = resolve(dirname, host, sizeof host, &err);
    DIR *d = m ? opendir(host) : NULL;
    if (!d) {
        r = m ? errno_to_sce(errno) : err;
        SDL_Log("[io] dopen %s failed: 0x%08X", dirname ? dirname : "(null)", (unsigned)r);
    } else {
        int i = alloc_slot();
        if (i < 0) {
            closedir(d);
            r = SCE_ERROR_ERRNO_EMFILE;
        } else {
            FdSlot *s = &g_fds[i];
            s->kind = SLOT_DIR;
            s->dir = d;
            s->dir_dots = 2; /* the UMD file system lists "." and ".." */
            snprintf(s->path, sizeof s->path, "%s", host);
            snprintf(s->psp_path, sizeof s->psp_path, "%s", dirname);
            r = FD_BASE + i;
            SDL_Log("[io] dopen %s -> fd %d", dirname, (int)r);
        }
    }
    unlock();
    PSP_TRACE(r, "\"%s\"", dirname ? dirname : "(null)");
    return r;
}

int32_t sceIoDread(SceUID fd, SceIoDirent *dir)
{
    lock();
    FdSlot *s = get_slot(fd);
    int32_t r = SCE_KERNEL_ERROR_BADF;
    if (s && s->kind == SLOT_DIR && dir) {
        void *priv = dir->d_private; /* the caller's, left alone */
        const char *name = NULL;
        struct dirent *e = NULL;
        if (s->dir_dots > 0)
            name = s->dir_dots-- == 2 ? "." : "..";
        else {
            while ((e = readdir(s->dir)) && (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")))
                ;
            name = e ? e->d_name : NULL;
        }
        if (!name)
            r = 0; /* end of listing */
        else {
            char full[1400];
            struct stat st;
            snprintf(full, sizeof full, "%s/%s", s->path, name);
            memset(dir, 0, sizeof *dir);
            dir->d_private = priv;
            if (stat(full, &st) == 0)
                fill_stat(&st, false, &dir->d_stat);
            snprintf(dir->d_name, sizeof dir->d_name, "%s", name);
            r = 1;
        }
    }
    unlock();
    PSP_TRACE(r, "%d", (int)fd);
    return r;
}

int32_t sceIoDclose(SceUID fd)
{
    lock();
    FdSlot *s = get_slot(fd);
    int32_t r = SCE_KERNEL_ERROR_BADF;
    if (s && s->kind == SLOT_DIR) {
        free_slot(s);
        r = 0;
    }
    unlock();
    PSP_TRACE(r, "%d", (int)fd);
    return r;
}

int32_t sceIoGetstat(const char *file, SceIoStat *stat_out)
{
    char host[1100];
    int32_t err = 0;
    struct stat st;
    lock();
    const Mount *m = resolve(file, host, sizeof host, &err);
    int32_t r = err;
    if (m)
        r = stat(host, &st) == 0 ? 0 : errno_to_sce(errno);
    if (r == 0 && stat_out)
        fill_stat(&st, m->writable, stat_out);
    unlock();
    if (r != 0 || log_all())
        SDL_Log("[io] getstat %s -> 0x%08X", file ? file : "(null)", (unsigned)r);
    PSP_TRACE(r, "\"%s\"", file ? file : "(null)");
    return r;
}

int32_t sceIoChstat(const char *file, SceIoStat *stat_in, int bits)
{
    /* Only used on memory-stick saves; changing modes/times isn't needed on the host. */
    char host[1100];
    int32_t err = 0;
    lock();
    const Mount *m = resolve(file, host, sizeof host, &err);
    struct stat st;
    int32_t r = !m ? err : !m->writable ? SCE_ERROR_ERRNO_EROFS
              : stat(host, &st) == 0 ? 0 : errno_to_sce(errno);
    unlock();
    SDL_Log("[io] chstat %s (bits 0x%X) -> 0x%08X (ignored)", file ? file : "(null)", bits, (unsigned)r);
    PSP_TRACE(r, "\"%s\", 0x%X", file ? file : "(null)", bits);
    return r;
}

int32_t sceIoMkdir(const char *dir, SceMode mode)
{
    char host[1100];
    int32_t err = 0;
    lock();
    const Mount *m = resolve(dir, host, sizeof host, &err);
    int32_t r = !m ? err : !m->writable ? SCE_ERROR_ERRNO_EROFS
              : mkdir(host, mode ? (mode & 0777) : 0777) == 0 ? 0 : errno_to_sce(errno);
    unlock();
    SDL_Log("[io] mkdir %s -> 0x%08X", dir ? dir : "(null)", (unsigned)r);
    PSP_TRACE(r, "\"%s\"", dir ? dir : "(null)");
    return r;
}

int32_t sceIoRename(const char *oldname, const char *newname)
{
    char a[1100], b[1100];
    int32_t err = 0;
    lock();
    const Mount *ma = resolve(oldname, a, sizeof a, &err);
    /* The PSP allows a device-less new name, meaning the same directory. */
    char tmp[600];
    if (ma && newname && !strchr(newname, ':')) {
        const char *slash = strrchr(oldname, '/');
        int dirlen = slash ? (int)(slash - oldname + 1) : 0;
        snprintf(tmp, sizeof tmp, "%.*s%s", dirlen, oldname, newname);
        newname = tmp;
    }
    const Mount *mb = ma ? resolve(newname, b, sizeof b, &err) : NULL;
    int32_t r = !ma || !mb ? err : (ma != mb) ? SCE_ERROR_ERRNO_EINVAL
              : !ma->writable ? SCE_ERROR_ERRNO_EROFS
              : rename(a, b) == 0 ? 0 : errno_to_sce(errno);
    unlock();
    SDL_Log("[io] rename %s -> %s: 0x%08X", oldname ? oldname : "(null)", newname ? newname : "(null)", (unsigned)r);
    PSP_TRACE(r, "\"%s\", \"%s\"", oldname ? oldname : "(null)", newname ? newname : "(null)");
    return r;
}

int32_t sceIoDevctl(const char *dev, unsigned int cmd, void *indata, int inlen, void *outdata, int outlen)
{
    /* fatms0: 0x02425823 asks whether a memory stick is inserted (1 = yes). The game
     * calls it before touching ms0:, so answer according to whether ms0: is mounted. */
    if (dev && !strncasecmp(dev, "fatms0:", 7) && cmd == 0x02425823) {
        if (outdata && outlen >= 4)
            *(int32_t *)outdata = find_mount("ms0", 3) ? 1 : 0;
        PSP_TRACE(0, "\"%s\", 0x%08X", dev, cmd);
        return 0;
    }
    SDL_Log("[io] devctl %s cmd 0x%08X (in %d, out %d) unsupported", dev ? dev : "(null)", cmd, inlen, outlen);
    PSP_TRACE(SCE_KERNEL_ERROR_UNSUP, "\"%s\", 0x%08X", dev ? dev : "(null)", cmd);
    return SCE_KERNEL_ERROR_UNSUP;
}

/* ---- sceUmdUser: the disc is always inserted and ready once disc0: is mounted ---- */

static int32_t umd_stat(void)
{
    return find_mount("disc0", 5) ? PSP_UMD_PRESENT | PSP_UMD_INITED | PSP_UMD_READY
                                  : PSP_UMD_NOT_PRESENT;
}

int32_t sceUmdActivate(int mode, const char *drive)
{
    PSP_TRACE(0, "%d, \"%s\"", mode, drive ? drive : "(null)");
    return 0;
}

int32_t sceUmdCancelWaitDriveStat(void)
{
    PSP_TRACE(0, NULL);
    return 0;
}

int32_t sceUmdCheckMedium(void)
{
    int32_t r = find_mount("disc0", 5) ? 1 : 0;
    PSP_TRACE(r, NULL);
    return r;
}

int32_t sceUmdGetDriveStat(void)
{
    int32_t r = umd_stat();
    PSP_TRACE(r, NULL);
    return r;
}

int32_t sceUmdWaitDriveStat(int stat)
{
    int32_t r = (umd_stat() & stat) ? 0 : SCE_KERNEL_ERROR_WAIT_TIMEOUT;
    PSP_TRACE(r, "0x%X", stat);
    return r;
}

int32_t sceUmdWaitDriveStatCB(int stat, unsigned int timeout)
{
    psp_kernel_run_callbacks();
    int32_t r = (umd_stat() & stat) ? 0 : SCE_KERNEL_ERROR_WAIT_TIMEOUT;
    PSP_TRACE(r, "0x%X, %u", stat, timeout);
    return r;
}

/* ---- disc root discovery ---- */

static bool is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool psp_io_mount_disc(const char *dir)
{
    char real[1024], probe[1100];
    if (!dir || !realpath(dir, real)) {
        SDL_Log("[io] disc folder %s not found", dir ? dir : "(null)");
        return false;
    }
    snprintf(probe, sizeof probe, "%s/PSP_GAME", real);
    if (is_dir(probe))
        return psp_io_mount("disc0", real, false);
    /* Also accept the PSP_GAME/USRDIR folder itself (where the game's data files are). */
    size_t n = strlen(real);
    if (n > 16 && !strcasecmp(real + n - 16, "/PSP_GAME/USRDIR")) {
        real[n - 16] = '\0';
        SDL_Log("[io] %s is PSP_GAME/USRDIR; using %s as the disc root", dir, real);
        return psp_io_mount("disc0", real, false);
    }
    SDL_Log("[io] %s doesn't look like a disc root (no PSP_GAME folder)", dir);
    return false;
}
