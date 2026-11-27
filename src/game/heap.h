/* The game's block allocator ("InitialzeAllocator" in its asserts), used for every
 * `new` and for file buffers. Two heaps exist after startup:
 *   default heap  DAT_08ccd490  most of the 18 MiB user memory
 *   fast heap     DAT_08cce978  the part of video memory above the frame buffers
 *
 * This is a stand-in with the game's interface, not its block format: a first-fit
 * list of blocks with coalescing, allocating from the bottom or (from_top) the top of
 * the region like the game's two allocation entry points. Like the game it fills a
 * new heap with 0xCB. */
#ifndef GAME_HEAP_H
#define GAME_HEAP_H

#include <stddef.h>
#include <stdint.h>

typedef struct HeapBlock HeapBlock;

typedef struct GameHeap {
    const char *name;
    uint8_t *base;  /* 16-aligned start */
    uint32_t size;  /* usable bytes, a multiple of 16 */
    HeapBlock *first;
    uint32_t used;  /* bytes in allocated blocks, headers included */
    uint32_t peak;
    uint32_t blocks;
} GameHeap;

/* InitialzeAllocator (FUN_08b19f18). */
void heap_init(GameHeap *heap, void *mem, uint32_t size, const char *name);

/* AllocateInternal (FUN_08b1aaa8) via its two wrappers: FUN_08b1a0ac allocates from the
 * bottom, FUN_08b1a0d4 from the top. align 0 means 16. NULL when out of memory. */
void *heap_alloc(GameHeap *heap, uint32_t size, uint32_t align, const char *tag, int from_top);
void heap_free(GameHeap *heap, void *p);

/* Largest free block, for logging. */
uint32_t heap_largest_free(const GameHeap *heap);

extern GameHeap g_default_heap; /* DAT_08ccd490 */
extern GameHeap g_fast_heap;    /* DAT_08cce978 */

/* operator new / new[] (FUN_08b20b50 / FUN_08b20b98): default heap, 16-aligned,
 * tagged "DEFAULT_NEW" / "DEFAULT_VEC_NEW". */
void *game_new(uint32_t size);
void *game_new_array(uint32_t size);

#endif
