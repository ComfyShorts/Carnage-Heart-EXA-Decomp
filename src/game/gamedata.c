/* See gamedata.h. Only the program headers are used: each PT_LOAD segment maps file
 * bytes to an address range, and pointers inside the data are plain addresses in
 * that same space (the module is relocatable, loaded at 0x08804000 on the PSP). */
#include "gamedata.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

typedef struct Segment {
    uint32_t vaddr, size, offset;
} Segment;

static uint8_t *g_elf;
static size_t g_elf_size;
static Segment g_segs[4];
static int g_seg_count;

/* CDesignString's table: 0x13B pointers at Ghidra 0x08c7a868. */
#define DESIGN_STRING_TABLE 0x00476868u
#define DESIGN_STRING_COUNT 0x13B

/* The text segment ends at the data segment's start; a known string checks the file. */
#define CHECK_ADDR 0x00444098u /* "font/gothic.tbl" */

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static const uint8_t *map(uint32_t addr, uint32_t len)
{
    for (int i = 0; i < g_seg_count; i++) {
        const Segment *s = &g_segs[i];
        if (addr >= s->vaddr && addr - s->vaddr + len <= s->size && s->offset + (addr - s->vaddr) + len <= g_elf_size)
            return g_elf + s->offset + (addr - s->vaddr);
    }
    return NULL;
}

bool gamedata_open(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        SDL_Log("[gamedata] %s not found: interface text won't be drawn (pass --elf FILE)", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = size > 0x40 ? malloc((size_t)size) : NULL;
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(buf);
        SDL_Log("[gamedata] can't read %s", path);
        return false;
    }
    fclose(f);
    if (memcmp(buf, "\x7f" "ELF\x01\x01", 6) != 0) {
        free(buf);
        SDL_Log("[gamedata] %s is not a decrypted 32-bit ELF", path);
        return false;
    }
    uint32_t phoff = rd32(buf + 0x1C);
    unsigned phnum = buf[0x2C] | buf[0x2D] << 8;
    g_seg_count = 0;
    for (unsigned i = 0; i < phnum && g_seg_count < 4; i++) {
        const uint8_t *ph = buf + phoff + 32 * i;
        if (phoff + 32 * (i + 1) > (uint32_t)size)
            break;
        if (rd32(ph) != 1) /* PT_LOAD */
            continue;
        g_segs[g_seg_count++] = (Segment){rd32(ph + 8), rd32(ph + 16), rd32(ph + 4)};
    }
    g_elf = buf;
    g_elf_size = (size_t)size;
    const uint8_t *check = map(CHECK_ADDR, 16);
    if (!check || memcmp(check, "font/gothic.tbl", 16) != 0) {
        SDL_Log("[gamedata] %s doesn't look like this game's executable", path);
        free(g_elf);
        g_elf = NULL;
        g_seg_count = 0;
        return false;
    }
    SDL_Log("[gamedata] using %s for interface text", path);
    return true;
}

bool gamedata_available(void)
{
    return g_elf != NULL;
}

uint32_t gamedata_word(uint32_t addr, uint32_t fallback)
{
    const uint8_t *p = g_elf ? map(addr, 4) : NULL;
    return p ? rd32(p) : fallback;
}

const char *gamedata_string(uint32_t addr)
{
    const uint8_t *p = g_elf ? map(addr, 1) : NULL;
    if (!p)
        return NULL;
    /* Must end inside the segment. */
    for (uint32_t n = 0; map(addr + n, 1); n++)
        if (!p[n])
            return (const char *)p;
    return NULL;
}

const char *design_string(int id, int allow_fallback)
{
    if (id < 0 || id >= DESIGN_STRING_COUNT) {
        if (!allow_fallback)
            return NULL;
        id = 1;
    }
    uint32_t ptr = gamedata_word(DESIGN_STRING_TABLE + 4u * (uint32_t)id, 0);
    return ptr ? gamedata_string(ptr) : NULL;
}
