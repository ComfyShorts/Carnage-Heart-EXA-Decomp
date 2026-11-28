/* See picture.h.
 *
 * GIM layout (little-endian): a 16-byte signature ("MIG.00.1PSP"), then nested
 * blocks. Each block starts with id (u16), unused (u16), size (u32, the whole block),
 * a u32 that is the offset of the next block on the same level for leaves, and the
 * offset of the block's own data (u32, 16). Ids: 2 file, 3 picture, 4 image,
 * 5 palette, 0xFF file info. Image and palette data begin with a 48-byte header:
 *
 *   +0x00 u16 header size      +0x02 u16 reference   +0x04 u16 format
 *   +0x06 u16 order (0 linear, 1 swizzled "faststorage")
 *   +0x08 u16 width            +0x0A u16 height      +0x0C u16 bits per pixel
 *   +0x0E u16 pitch alignment (bytes)                +0x10 u16 height alignment
 *   +0x18 u32 offset of the level/frame offset table +0x1C u32 pixels start
 *   +0x20 u32 pixels end       +0x2A u16 level count +0x2E u16 frame count
 *
 * all offsets relative to the header. Formats: 0 5650, 1 5551, 2 4444, 3 8888,
 * 4 index4, 5 index8, 6 index16, 7 index32 - the same numbers as the GE's. */
#include "picture.h"

#include <string.h>

#include <SDL3/SDL.h>

#include "fileio.h"
#include "gu.h"
#include "heap.h"

typedef struct GimImage {
    int format, swizzled, width, height, bpp, pitch_px;
    int frames;
    const uint8_t *frame_data[16];
} GimImage;

struct Picture {
    char name[64];
    int refs;
    uint8_t *file;
    GimImage image, palette;
    int has_palette;
    int frame;
};

#define MAX_PICTURES 256
static Picture *g_pictures[MAX_PICTURES];

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int parse_image(const uint8_t *hdr, const uint8_t *end, GimImage *out)
{
    if (hdr + 0x30 > end)
        return 0;
    out->format = rd16(hdr + 0x04);
    out->swizzled = rd16(hdr + 0x06) == 1;
    out->width = rd16(hdr + 0x08);
    out->height = rd16(hdr + 0x0A);
    out->bpp = rd16(hdr + 0x0C);
    int pitch_align = rd16(hdr + 0x0E);
    if (out->bpp <= 0 || out->width <= 0 || pitch_align <= 0)
        return 0;
    int row_bytes = (out->width * out->bpp / 8 + pitch_align - 1) / pitch_align * pitch_align;
    out->pitch_px = row_bytes * 8 / out->bpp;
    uint32_t table = rd32(hdr + 0x18);
    int levels = rd16(hdr + 0x2A);
    out->frames = rd16(hdr + 0x2E);
    if (out->frames < 1)
        out->frames = 1;
    if (out->frames > 16)
        out->frames = 16;
    if (levels < 1)
        levels = 1;
    for (int f = 0; f < out->frames; f++) {
        /* The offset table lists level 0..n-1 of frame 0, then of frame 1, ... */
        const uint8_t *e = hdr + table + 4u * (uint32_t)(f * levels);
        if (e + 4 > end)
            return 0;
        out->frame_data[f] = hdr + rd32(e);
        if (out->frame_data[f] >= end)
            return 0;
    }
    return 1;
}

static void parse_blocks(Picture *p, const uint8_t *b, const uint8_t *end)
{
    while (b + 16 <= end) {
        uint16_t id = rd16(b);
        uint32_t size = rd32(b + 4), data = rd32(b + 12);
        if (size < 16 || b + size > end)
            return;
        if (id == 2 || id == 3)
            parse_blocks(p, b + data, b + size);
        else if (id == 4 && !p->image.width)
            parse_image(b + data, b + size, &p->image);
        else if (id == 5 && !p->has_palette)
            p->has_palette = parse_image(b + data, b + size, &p->palette);
        b += size;
    }
}

static Picture *picture_from_file(const char *name, uint8_t *file, int32_t size)
{
    if (size < 0x20 || memcmp(file, "MIG.00.1PSP", 11) != 0) {
        SDL_Log("[picture] %s is not a GIM file", name);
        return NULL;
    }
    Picture *p = SDL_calloc(1, sizeof *p);
    SDL_strlcpy(p->name, name, sizeof p->name);
    p->file = file;
    p->refs = 1;
    parse_blocks(p, file + 16, file + size);
    if (!p->image.width) {
        SDL_Log("[picture] %s: no image block", name);
        SDL_free(p);
        return NULL;
    }
    if (p->image.format >= 4 && !p->has_palette) {
        SDL_Log("[picture] %s: indexed image without a palette", name);
        SDL_free(p);
        return NULL;
    }
    return p;
}

Picture *picture_from_memory(const char *name, const void *data, int32_t size)
{
    uint8_t *file = heap_alloc(&g_default_heap, (uint32_t)size, 16, "texture", 0);
    if (!file)
        return NULL;
    memcpy(file, data, (size_t)size);
    Picture *p = picture_from_file(name, file, size);
    if (!p)
        heap_free(&g_default_heap, file);
    return p; /* not in the by-name cache: the owner releases it */
}

static int indexed_sizes(int format, int w, int h, int *image, int *palette)
{
    if ((format != 4 && format != 5) || w <= 0 || h <= 0 || (w * (format == 4 ? 4 : 8)) % 128)
        return 0; /* rows of 16 bytes, as the GE wants */
    *image = w * h * (format == 4 ? 4 : 8) / 8;
    *palette = (format == 4 ? 16 : 256) * 4;
    return 1;
}

Picture *picture_from_pixels(const char *name, int format, int w, int h, const void *pixels, const void *palette)
{
    int ib, pb;
    if (!indexed_sizes(format, w, h, &ib, &pb))
        return NULL;
    uint8_t *buf = heap_alloc(&g_default_heap, (uint32_t)(ib + pb), 16, "texture", 0);
    if (!buf)
        return NULL;
    Picture *p = SDL_calloc(1, sizeof *p);
    SDL_strlcpy(p->name, name, sizeof p->name);
    p->file = buf;
    p->refs = 1;
    p->image = (GimImage){.format = format, .width = w, .height = h, .bpp = format == 4 ? 4 : 8, .pitch_px = w,
                          .frames = 1, .frame_data = {buf}};
    p->palette = (GimImage){.format = 3, .width = format == 4 ? 16 : 256, .height = 1, .bpp = 32,
                            .pitch_px = format == 4 ? 16 : 256, .frames = 1, .frame_data = {buf + ib}};
    p->has_palette = 1;
    picture_set_pixels(p, pixels, palette);
    return p;
}

void picture_set_pixels(Picture *p, const void *pixels, const void *palette)
{
    int ib, pb;
    if (!p || !indexed_sizes(p->image.format, p->image.width, p->image.height, &ib, &pb))
        return;
    memcpy(p->file, pixels, (size_t)ib);
    memcpy(p->file + ib, palette, (size_t)pb);
}

Picture *picture_load(const char *name)
{
    for (int i = 0; i < MAX_PICTURES; i++)
        if (g_pictures[i] && !SDL_strcasecmp(g_pictures[i]->name, name)) {
            g_pictures[i]->refs++;
            return g_pictures[i];
        }
    int slot = -1;
    for (int i = 0; i < MAX_PICTURES && slot < 0; i++)
        if (!g_pictures[i])
            slot = i;
    int32_t size = fileio_file_size(name);
    if (size <= 0 || slot < 0) {
        SDL_Log("[picture] can't load %s%s", name, slot < 0 ? " (cache full)" : " (not in an open file set)");
        return NULL;
    }
    uint8_t *file = heap_alloc(&g_default_heap, (uint32_t)size, 16, "texture", 0);
    if (!file || fileio_read_file(name, file, (uint32_t)size) != size) {
        SDL_Log("[picture] can't read %s", name);
        if (file)
            heap_free(&g_default_heap, file);
        return NULL;
    }
    Picture *p = picture_from_file(name, file, size);
    if (!p) {
        heap_free(&g_default_heap, file);
        return NULL;
    }
    g_pictures[slot] = p;
    SDL_Log("[picture] %s: %dx%d, format %d%s, %d frame(s)", name, p->image.width, p->image.height,
            p->image.format, p->image.swizzled ? " swizzled" : "", p->image.frames);
    return p;
}

void picture_release(Picture *p)
{
    if (!p || --p->refs > 0)
        return;
    for (int i = 0; i < MAX_PICTURES; i++)
        if (g_pictures[i] == p)
            g_pictures[i] = NULL;
    heap_free(&g_default_heap, p->file);
    SDL_free(p);
}

bool picture_index4_data(const Picture *p, int w, int h, const uint8_t **pixels, const uint8_t **palette,
                         int *palette_format)
{
    /* rows of w / 2 bytes; a swizzled image is the same when a row is one 16-byte block */
    if (!p || p->image.format != 4 || p->image.width != w || p->image.height != h || !p->has_palette ||
        p->image.pitch_px != w || (p->image.swizzled && w != 32))
        return false;
    *pixels = p->image.frame_data[0];
    *palette = p->palette.frame_data[0];
    *palette_format = p->palette.format;
    return true;
}

int picture_width(const Picture *p)
{
    return p ? p->image.width : 0;
}

int picture_height(const Picture *p)
{
    return p ? p->image.height : 0;
}

int picture_frames(const Picture *p)
{
    return p ? p->image.frames : 0;
}

void picture_set_frame(Picture *p, int frame)
{
    if (p)
        p->frame = frame;
}

static int pow2_at_least(int v)
{
    int r = 1;
    while (r < v && r < 512)
        r <<= 1;
    return r;
}

void picture_bind(const Picture *p)
{
    if (!p)
        return;
    const GimImage *im = &p->image;
    int f = p->frame < 0 ? 0 : p->frame >= im->frames ? im->frames - 1 : p->frame;
    sceGuTexMode(im->format, 0, 0, im->swizzled);
    sceGuTexImage(0, pow2_at_least(im->width), pow2_at_least(im->height), im->pitch_px, im->frame_data[f]);
    if (im->format >= 4) {
        const GimImage *pal = &p->palette;
        int pf = f < pal->frames ? f : pal->frames - 1;
        int entries = pal->width * pal->height;
        int bytes = entries * pal->bpp / 8;
        sceGuClutMode(pal->format, 0, 0xFF, 0);
        sceGuClutLoad((bytes + 31) / 32, pal->frame_data[pf]);
    }
}

/* DAT_08c8d01c: {op, src, dst, fixb} per mode; fixa is always white. */
static const struct { int op, src, dst; uint32_t fixb; } k_blend[7] = {
    {0, 10, 10, 0x00000000}, /* replace: 1*src + 0*dst */
    {0, 2, 3, 0xFFFFFFFF},   /* alpha */
    {0, 2, 10, 0xFFFFFFFF},  /* add: src*alpha + dst */
    {2, 2, 10, 0xFFFFFFFF},  /* subtract: dst - src*alpha */
    {3, 2, 10, 0xFFFFFFFF},  /* min */
    {4, 2, 10, 0xFFFFFFFF},  /* max */
    {5, 2, 10, 0xFFFFFFFF},  /* difference */
};

void picture_set_blend(int mode)
{
    if (mode < 0 || mode > 6) {
        SDL_Log("[picture] CPicture::SetBlend: bad mode %d", mode);
        return;
    }
    sceGuBlendFunc(k_blend[mode].op, k_blend[mode].src, k_blend[mode].dst, 0xFFFFFFFF, k_blend[mode].fixb);
}

void picture_draw_ex(const Picture *p, int x, int y, int z, int dstw, int dsth, int u, int v, int srcw,
                     int srch, int blend, uint32_t color, int wrapu, int wrapv, int flip, int corner)
{
    if (!p)
        return;
    sceGuEnable(GU_TEXTURE_2D);
    sceGuColor(color);
    picture_bind(p);
    if (blend != BLEND_ALPHA)
        picture_set_blend(blend);
    if (wrapu || wrapv)
        sceGuTexWrap(wrapu, wrapv);
    sceGuSpriteSize(srcw, srch, dstw, dsth);
    sceGuSprite(x, y, z, u, v, flip, corner);
    if (wrapu || wrapv)
        sceGuTexWrap(0, 0);
    if (blend != BLEND_ALPHA)
        picture_set_blend(BLEND_ALPHA);
}

void picture_draw(const Picture *p, int x, int y, int z, int blend, uint32_t color, int flip, int corner)
{
    int w = picture_width(p), h = picture_height(p);
    bool turned = corner == 1 || corner == 3;
    picture_draw_ex(p, x, y, z, turned ? h : w, turned ? w : h, 0, 0, w, h, blend, color, 1, 1, flip, corner);
}

void picture_draw_rect(const Picture *p, int x, int y, int z, int u, int v, int w, int h, int blend,
                       uint32_t color, int flip, int corner)
{
    picture_draw_ex(p, x, y, z, w, h, u, v, w, h, blend, color, 1, 1, flip, corner);
}
