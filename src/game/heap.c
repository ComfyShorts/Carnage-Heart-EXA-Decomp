/* Block allocator stand-in. See heap.h.
 *
 * The region is tiled by blocks, each starting with a header; a block's successor
 * starts right after it, so only the predecessor needs a link. */
#include "heap.h"

#include <string.h>

#include <SDL3/SDL.h>

struct HeapBlock {
    uint32_t size; /* whole block, header included */
    uint32_t used;
    const char *tag;
    HeapBlock *prev;
};

#define HDR ((uint32_t)((sizeof(HeapBlock) + 15) & ~15u))
#define MIN_SPLIT (HDR + 16) /* smallest block worth splitting off */

GameHeap g_default_heap;
GameHeap g_fast_heap;

static uintptr_t align_up(uintptr_t v, uintptr_t a)
{
    return (v + a - 1) & ~(a - 1);
}

static HeapBlock *next_of(const GameHeap *h, HeapBlock *b)
{
    uint8_t *n = (uint8_t *)b + b->size;
    return n < h->base + h->size ? (HeapBlock *)n : NULL;
}

void heap_init(GameHeap *heap, void *mem, uint32_t size, const char *name)
{
    memset(heap, 0, sizeof *heap);
    uintptr_t start = align_up((uintptr_t)mem, 16);
    uint32_t lost = (uint32_t)(start - (uintptr_t)mem);
    if (size < lost + 0x24) {
        SDL_Log("[heap] %s: %u bytes is too small", name, (unsigned)size);
        return;
    }
    heap->name = name;
    heap->base = (uint8_t *)start;
    heap->size = (size - lost) & ~15u;
    memset(heap->base, 0xCB, heap->size);
    heap->first = (HeapBlock *)heap->base;
    *heap->first = (HeapBlock){heap->size, 0, NULL, NULL};
    SDL_Log("[heap] \"%s\": %u KiB at %p", name, (unsigned)(heap->size / 1024), (void *)heap->base);
}

/* Splits `b` so a block starts at `at` (inside b); returns the new block. */
static HeapBlock *split_at(GameHeap *h, HeapBlock *b, uint8_t *at)
{
    HeapBlock *n = (HeapBlock *)at;
    uint32_t front = (uint32_t)(at - (uint8_t *)b);
    *n = (HeapBlock){b->size - front, 0, NULL, b};
    b->size = front;
    HeapBlock *after = next_of(h, n);
    if (after)
        after->prev = n;
    return n;
}

/* Where an allocation's payload would go in free block b, or NULL if it doesn't fit.
 * Any gap left in front of the header must be 0 or big enough to stay a free block. */
static uint8_t *place(const HeapBlock *b, uint32_t size, uint32_t align, int from_top)
{
    uint8_t *start = (uint8_t *)b, *end = start + b->size;
    if (b->size < HDR + size)
        return NULL;
    if (!from_top) {
        uint8_t *p = (uint8_t *)align_up((uintptr_t)start + HDR, align);
        while (p - HDR != start && (uint32_t)(p - HDR - start) < MIN_SPLIT)
            p += align;
        return p + size <= end ? p : NULL;
    }
    uint8_t *p = (uint8_t *)(((uintptr_t)end - size) & ~(uintptr_t)(align - 1));
    while (p - HDR > start && (uint32_t)(p - HDR - start) < MIN_SPLIT)
        p -= align;
    return p - HDR >= start ? p : NULL;
}

void *heap_alloc(GameHeap *heap, uint32_t size, uint32_t align, const char *tag, int from_top)
{
    if (!heap->base || size == 0)
        return NULL;
    if (align < 16)
        align = 16;
    if (align & (align - 1))
        align = (uint32_t)align_up(align, 16);
    size = (uint32_t)align_up(size, 16);

    HeapBlock *b = heap->first, *pick = NULL;
    uint8_t *payload = NULL;
    if (from_top) {
        HeapBlock *last = b;
        for (; b; b = next_of(heap, b))
            last = b;
        for (b = last; b; b = b->prev)
            if (!b->used && (payload = place(b, size, align, 1))) {
                pick = b;
                break;
            }
    } else {
        for (; b; b = next_of(heap, b))
            if (!b->used && (payload = place(b, size, align, 0))) {
                pick = b;
                break;
            }
    }
    if (!pick) {
        SDL_Log("[heap] \"%s\": out of memory for %u bytes (align %u, tag %s); largest free %u",
                heap->name, (unsigned)size, (unsigned)align, tag ? tag : "-",
                (unsigned)heap_largest_free(heap));
        return NULL;
    }
    if (payload - HDR != (uint8_t *)pick)
        pick = split_at(heap, pick, payload - HDR);
    uint8_t *end = payload + size;
    if ((uint32_t)((uint8_t *)pick + pick->size - end) >= MIN_SPLIT)
        split_at(heap, pick, end);
    pick->used = 1;
    pick->tag = tag;
    heap->used += pick->size;
    heap->blocks++;
    if (heap->used > heap->peak)
        heap->peak = heap->used;
    return payload;
}

void heap_free(GameHeap *heap, void *p)
{
    if (!p)
        return;
    HeapBlock *b = (HeapBlock *)((uint8_t *)p - HDR);
    if ((uint8_t *)b < heap->base || (uint8_t *)b >= heap->base + heap->size || !b->used) {
        SDL_Log("[heap] \"%s\": bad free of %p", heap->name, p);
        return;
    }
    heap->used -= b->size;
    heap->blocks--;
    b->used = 0;
    b->tag = NULL;
    HeapBlock *n = next_of(heap, b);
    if (n && !n->used) {
        b->size += n->size;
        HeapBlock *nn = next_of(heap, b);
        if (nn)
            nn->prev = b;
    }
    if (b->prev && !b->prev->used) {
        HeapBlock *pv = b->prev;
        pv->size += b->size;
        HeapBlock *nn = next_of(heap, pv);
        if (nn)
            nn->prev = pv;
    }
}

uint32_t heap_largest_free(const GameHeap *heap)
{
    uint32_t best = 0;
    for (HeapBlock *b = heap->first; b; b = next_of(heap, b))
        if (!b->used && b->size > best)
            best = b->size;
    return best > HDR ? best - HDR : 0;
}

void *game_new(uint32_t size)
{
    return heap_alloc(&g_default_heap, size, 16, "DEFAULT_NEW", 0);
}

void *game_new_array(uint32_t size)
{
    return heap_alloc(&g_default_heap, size, 16, "DEFAULT_VEC_NEW", 0);
}
