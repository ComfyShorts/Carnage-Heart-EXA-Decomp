/* Pack index reader (the game's own is packfile.c). See packfile.h. */
#include "packfile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "psp_imports.h"

#define PIDX_HEADER_WORDS 10

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Reads a whole file through sceIo into a malloc'd buffer. */
static long read_file(const char *path, uint8_t **out)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return fd;
    SceOff size = sceIoLseek(fd, 0, PSP_SEEK_END);
    if (size < 0 || size > 64 * 1024 * 1024 || sceIoLseek(fd, 0, PSP_SEEK_SET) != 0) {
        sceIoClose(fd);
        return SCE_ERROR_ERRNO_EIO;
    }
    uint8_t *buf = malloc((size_t)size + 1);
    int32_t got = buf ? sceIoRead(fd, buf, (SceSize)size) : -1;
    sceIoClose(fd);
    if (got != (int32_t)size) {
        free(buf);
        return SCE_ERROR_ERRNO_EIO;
    }
    *out = buf;
    return (long)size;
}

int pack_open(Pack *pack, const char *dir)
{
    memset(pack, 0, sizeof *pack);
    snprintf(pack->dir, sizeof pack->dir, "%s", dir);
    char path[300];
    snprintf(path, sizeof path, "%s/chexa.idx", dir);
    long size = read_file(path, &pack->index);
    if (size < 0)
        return (int)size;
    pack->index_size = (size_t)size;
    const uint8_t *h = pack->index;
    if (size < PIDX_HEADER_WORDS * 4 || memcmp(h, "PIDX", 4) != 0) {
        pack_close(pack);
        return SCE_ERROR_ERRNO_EINVAL;
    }
    uint32_t ent_off = rd32(h + 0x0C), ent_n = rd32(h + 0x10);
    pack->root_count = rd32(h + 0x14);
    pack->strings = rd32(h + 0x20);
    if (ent_off % 4 || ent_off > pack->index_size ||
        ent_n > (pack->index_size - ent_off) / sizeof(PackEntry) || pack->root_count > ent_n ||
        pack->strings >= pack->index_size) {
        pack_close(pack);
        return SCE_ERROR_ERRNO_EINVAL;
    }
    /* Entries are little-endian u32s, which is the host order on x86. */
    pack->entries = (const PackEntry *)(pack->index + ent_off);
    pack->entry_count = ent_n;
    return 0;
}

void pack_close(Pack *pack)
{
    free(pack->index);
    memset(pack, 0, sizeof *pack);
}

const char *pack_name(const Pack *pack, uint32_t offset)
{
    size_t o = (size_t)pack->strings + offset;
    if (o >= pack->index_size || !memchr(pack->index + o, 0, pack->index_size - o))
        return "";
    return (const char *)pack->index + o;
}

static int child_range_ok(const Pack *pack, uint32_t first, uint32_t count)
{
    return first <= pack->entry_count && count <= pack->entry_count - first;
}

const PackEntry *pack_find(const Pack *pack, const char *path)
{
    uint32_t first = 0, count = pack->root_count;
    const PackEntry *found = NULL;
    while (*path) {
        while (*path == '/')
            path++;
        const char *end = strchr(path, '/');
        size_t len = end ? (size_t)(end - path) : strlen(path);
        if (!len)
            break;
        found = NULL;
        for (uint32_t i = first; i < first + count; i++) {
            const char *n = pack_name(pack, pack->entries[i].name);
            if (strlen(n) == len && strncasecmp(n, path, len) == 0) {
                found = &pack->entries[i];
                break;
            }
        }
        if (!found)
            return NULL;
        path += len;
        if (found->is_dir) {
            if (!child_range_ok(pack, found->b, found->a))
                return NULL;
            first = found->b;
            count = found->a;
        } else if (*path) {
            return NULL; /* a file in the middle of the path */
        }
    }
    return found && !found->is_dir ? found : NULL;
}

long pack_read(const Pack *pack, const PackEntry *e, void *dst, size_t cap)
{
    if (!e || e->is_dir)
        return SCE_ERROR_ERRNO_EINVAL;
    if (e->size > cap)
        return SCE_ERROR_ERRNO_EINVAL;
    char path[300];
    snprintf(path, sizeof path, "%s/%s", pack->dir, pack_name(pack, e->a));
    uint8_t *stored = malloc(e->stored ? e->stored : 1);
    if (!stored)
        return SCE_ERROR_ERRNO_EIO;
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    long r = fd;
    if (fd >= 0) {
        if (sceIoLseek(fd, e->b, PSP_SEEK_SET) == (SceOff)e->b &&
            sceIoRead(fd, stored, e->stored) == (int32_t)e->stored) {
            r = arz_decode(stored, e->stored, dst, cap);
            if (r != (long)e->size)
                r = SCE_ERROR_ERRNO_EIO;
        } else {
            r = SCE_ERROR_ERRNO_EIO;
        }
        sceIoClose(fd);
    }
    free(stored);
    return r;
}

static int walk(const Pack *pack, uint32_t first, uint32_t count, char *path, size_t len,
                int depth, int (*fn)(void *, const char *, const PackEntry *), void *user)
{
    if (depth > 32 || !child_range_ok(pack, first, count))
        return 0;
    for (uint32_t i = first; i < first + count; i++) {
        const PackEntry *e = &pack->entries[i];
        int n = snprintf(path + len, 512 - len, "%s%s", len ? "/" : "", pack_name(pack, e->name));
        if (n < 0 || len + (size_t)n >= 512)
            continue;
        int r = e->is_dir ? walk(pack, e->b, e->a, path, len + (size_t)n, depth + 1, fn, user)
                          : fn(user, path, e);
        path[len] = '\0';
        if (r)
            return r;
    }
    return 0;
}

int pack_foreach(const Pack *pack, int (*fn)(void *user, const char *path, const PackEntry *e),
                 void *user)
{
    char path[512] = "";
    return walk(pack, 0, pack->root_count, path, 0, 0, fn, user);
}
