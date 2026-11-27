/* The game's file pack: an index (chexa.idx) plus archives (common.dat, voice.dat).
 * Format notes are in tools/chexa_pack.py and docs/program-format.md. All access goes
 * through the sceIo imports, so paths are PSP paths such as "disc0:/PSP_GAME/USRDIR". */
#ifndef PACKFILE_H
#define PACKFILE_H

#include <stddef.h>
#include <stdint.h>

typedef struct PackEntry {
    uint32_t is_dir;
    uint32_t name;      /* string-table offset */
    uint32_t a;         /* dir: child count    file: archive name (string offset) */
    uint32_t b;         /* dir: first child    file: offset in the archive */
    uint32_t size;      /* file: unpacked size */
    uint32_t stored;    /* file: stored (ARZ) size */
} PackEntry;

typedef struct Pack {
    char dir[256];          /* PSP directory holding chexa.idx and the archives */
    uint8_t *index;
    size_t index_size;
    const PackEntry *entries;
    uint32_t entry_count;
    uint32_t root_count;
    uint32_t strings;       /* string table offset in index */
} Pack;

/* Opens "<dir>/chexa.idx". Returns 0 or a negative error. */
int pack_open(Pack *pack, const char *dir);
void pack_close(Pack *pack);

/* Looks up "preset/arena/oke_b01_1.bin" (case-insensitive). NULL if missing. */
const PackEntry *pack_find(const Pack *pack, const char *path);
const char *pack_name(const Pack *pack, uint32_t offset);

/* Reads and unpacks a file entry into dst. Returns the unpacked size or a negative error. */
long pack_read(const Pack *pack, const PackEntry *entry, void *dst, size_t cap);

/* Calls fn for every file under the root, with its full path. Stops if fn returns nonzero. */
int pack_foreach(const Pack *pack, int (*fn)(void *user, const char *path, const PackEntry *e),
                 void *user);

/* ARZ decoding (lz.c). Return the unpacked size, or -1 on bad data. */
long arz_unpacked_size(const uint8_t *src, size_t len);
long arz_decode(const uint8_t *src, size_t len, uint8_t *dst, size_t cap);

#endif
