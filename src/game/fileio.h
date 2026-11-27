/* The game's file layer as far as startup uses it: synchronous reads through the
 * async sceIo calls, the pack index (chexa.idx), and file sets (bundles of small files
 * stored together in common.dat and loaded in one read).
 *
 * Game paths are relative to disc0:/PSP_GAME/USRDIR and are lower-cased; backslashes
 * become slashes. Names inside file sets look like "common/jis2ucs.bin". */
#ifndef GAME_FILEIO_H
#define GAME_FILEIO_H

#include <stdint.h>

#include "heap.h"

/* File I/O init (FUN_08b12698): clears the open-file cache, creates the two mutexes. */
void fileio_init(void);

/* GetFileSizeDirect (FUN_08b12940). Negative on error. */
int32_t fileio_get_size(const char *name);

/* ReadFileInternal (FUN_08b12494): reads `size` bytes at `offset`. keep_open puts the
 * file in the open-file cache. Returns size, or negative on error. */
int32_t fileio_read_at(const char *name, void *buf, uint32_t offset, uint32_t size, int keep_open);

/* FUN_08b127f4: reads the start of a file, up to cap bytes. */
int32_t fileio_read_head(const char *name, void *buf, int32_t cap);

/* ---- pack index ---- */

typedef struct FileSetInfo {
    uint32_t name;         /* string offset, e.g. "boot" */
    uint32_t archive;      /* string offset, e.g. "common.dat" */
    uint32_t offset;       /* where the set starts in the archive */
    uint32_t size;         /* bytes to read */
    uint32_t member_count;
    uint32_t unk14;
    uint32_t unk18;
    uint32_t next_offset;  /* offset of the set that follows it in the archive */
} FileSetInfo;

/* PackFile::ReadIndex (FUN_08b1eb9c). Returns 1 when the index was loaded. */
int packindex_read(const char *name);
/* FUN_08b1e940: binary search of the file-set table. */
const FileSetInfo *packindex_find_set(const char *name);
const char *packindex_string(uint32_t offset);

/* ---- file sets ---- */

/* OpenFileSet (FUN_08b14984): returns the slot (0..11) or -1. heap NULL means the
 * default heap. Only the synchronous form (async = 0) is implemented. */
int fileset_open(const char *name, GameHeap *heap, int from_top, int async);

/* GetFileSize for a file in an open file set or a loose pack entry (FUN_08b12a60):
 * the unpacked size, or -1. */
int32_t fileio_file_size(const char *name);

/* ReadFile (FUN_08b1385c): from the open file sets first, then the loose pack
 * entries. Returns the unpacked size or -1. */
int32_t fileio_read_file(const char *name, void *buf, uint32_t cap);

#endif
