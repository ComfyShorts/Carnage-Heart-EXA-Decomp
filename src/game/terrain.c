/* See terrain.h and docs/battle.md ("Terrain").
 *
 * The parameter file land/param/mapNN_a.mpp ("CHLG") holds the CLandGenerateParam
 * blocks the generator reads through FUN_08924eb0 .. FUN_08924f10:
 *
 *   0x40 block 0  u32 seed, f32 min x, f32 ?, f32 min z, f32 size x, f32 height,
 *                 f32 size z, u8 cells x, u8 cells z, u8 border height
 *   0x60 block 1  f32 roughness, u8 random corners, u8 corners[4], s8 merge weight
 *   0x70 block 2  u8 Voronoi feature points, s8 Voronoi merge weight
 *   0x80 block 3  u8 smoothing passes
 *   0x90 block 4  u8 erosion threshold, u8 erosion passes
 *   0xA0 block 5  u8 thermal threshold, u8 thermal passes
 *   0xB0 block 6  u8 terrace step, u8 stretch
 *   0x110        light direction (f32 x3), fog range at 0x138
 */
#include "terrain.h"

#include <math.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "fileio.h"
#include "gu.h"
#include "heap.h"
#include "picture.h"

/* ---- the random source: MT19937 (FUN_08b52254 seeds, FUN_08b469c0 steps) ---- */

static struct {
    uint32_t mt[624];
    int i;
} g_mt;

static void mt_seed(uint32_t s)
{
    g_mt.mt[0] = s;
    for (int i = 1; i < 624; i++)
        g_mt.mt[i] = (g_mt.mt[i - 1] ^ (g_mt.mt[i - 1] >> 30)) * 0x6C078965u + (uint32_t)i;
    g_mt.i = 0;
}

/* The game's step tempers the current word and then regenerates it in place; after
 * the 624 discarded steps of seeding that's the standard sequence. */
static uint32_t mt_next(void)
{
    int i = g_mt.i, j = i + 1 < 624 ? i + 1 : 0, k = i + 397 < 624 ? i + 397 : i - 227;
    uint32_t y = g_mt.mt[i];
    uint32_t t = y ^ (y >> 11);
    t ^= (t << 7) & 0x9D2C5680u;
    t ^= (t << 15) & 0xEFC60000u;
    t ^= t >> 18;
    uint32_t x = (y & 0x80000000u) | (g_mt.mt[j] & 0x7FFFFFFFu);
    g_mt.mt[i] = g_mt.mt[k] ^ (x >> 1) ^ ((g_mt.mt[j] & 1) ? 0x9908B0DFu : 0);
    g_mt.i = j;
    return t;
}

static void mt_start(uint32_t seed)
{
    mt_seed(seed);
    for (int n = 0; n < 624; n++)
        mt_next();
}

/* FUN_0891fcbc: a non-negative int. */
static int rnd(void)
{
    return (int)(mt_next() >> 1);
}

/* FUN_08b7f764: 0..r (r >= 1), else rnd % (r - 1). */
static int rnd_range(int r)
{
    return r < 1 ? rnd() % (r - 1) : rnd() % (r + 1);
}

/* FUN_08b7f708: a byte in 0..max. */
static int rnd_byte(int max)
{
    max &= 0xFF;
    return (max == 0 ? rnd() % -1 : rnd() % (max + 1)) & 0xFF;
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* ---- the game's qsort (FUN_08b41288): Bentley-McIlroy, as in 4.4BSD ----
 * Its exact comparison order matters because the generator sorts with a random
 * comparator to shuffle. Only int arrays are needed here. */

typedef int (*IntCmp)(const int *a, const int *b);

static void iswap(int *a, int *b)
{
    int t = *a;
    *a = *b;
    *b = t;
}

static void ivecswap(int *a, int *b, int n)
{
    while (n-- > 0)
        iswap(a++, b++);
}

static int *med3(int *a, int *b, int *c, IntCmp cmp)
{
    return cmp(a, b) < 0 ? (cmp(b, c) < 0 ? b : (cmp(a, c) < 0 ? c : a))
                         : (cmp(b, c) > 0 ? b : (cmp(a, c) < 0 ? a : c));
}

static void game_qsort(int *a, int n, IntCmp cmp)
{
    for (;;) {
        int swapped = 0;
        if (n < 7) {
            for (int *m = a + 1; m < a + n; m++)
                for (int *l = m; l > a && cmp(l - 1, l) > 0; l--)
                    iswap(l, l - 1);
            return;
        }
        int *pm = a + n / 2;
        if (n > 7) {
            int *pl = a, *pn = a + n - 1;
            if (n > 40) {
                int d = n / 8;
                pl = med3(pl, pl + d, pl + 2 * d, cmp);
                pm = med3(pm - d, pm, pm + d, cmp);
                pn = med3(pn - 2 * d, pn - d, pn, cmp);
            }
            pm = med3(pl, pm, pn, cmp);
        }
        iswap(a, pm);
        int *pa = a + 1, *pb = a + 1, *pc = a + n - 1, *pd = a + n - 1;
        for (;;) {
            int r;
            while (pb <= pc && (r = cmp(pb, a)) <= 0) {
                if (r == 0) {
                    swapped = 1;
                    iswap(pa++, pb);
                }
                pb++;
            }
            while (pb <= pc && (r = cmp(pc, a)) >= 0) {
                if (r == 0) {
                    swapped = 1;
                    iswap(pc, pd--);
                }
                pc--;
            }
            if (pb > pc)
                break;
            iswap(pb++, pc--);
            swapped = 1;
        }
        if (!swapped) {
            for (int *m = a + 1; m < a + n; m++)
                for (int *l = m; l > a && cmp(l - 1, l) > 0; l--)
                    iswap(l, l - 1);
            return;
        }
        int *pn = a + n;
        int r = (int)SDL_min(pa - a, pb - pa);
        ivecswap(a, pb - r, r);
        r = (int)SDL_min(pd - pc, pn - pd - 1);
        ivecswap(pb, pn - r, r);
        if ((r = (int)(pb - pa)) > 1)
            game_qsort(a, r, cmp);
        if ((r = (int)(pd - pc)) > 1) {
            a = pn - r;
            n = r;
            continue;
        }
        return;
    }
}

static int cmp_random(const int *a, const int *b) /* FUN_0891fcfc */
{
    (void)a;
    (void)b;
    return rnd() % 3 - 1;
}

static int cmp_int(const int *a, const int *b)
{
    return *a < *b ? -1 : *a > *b;
}

/* ---- the LANDGEN passes (edge-constrained variants; edge 0 = free cell) ---- */

/* GenerateRandomMidpoint, FUN_08922e30. */
static void pass_midpoint(float coef, int n, int max, const uint8_t *edge, uint8_t *d, int corners)
{
    float factor = powf(2.0f, -coef);
    int last = n - 1;
    int range = max, half = max / 2;
    if (corners) {
        const int idx[4] = {0, last, n * last, n * n - 1};
        for (int k = 0; k < 4; k++)
            d[idx[k]] = edge[idx[k]] ? edge[idx[k]] : (uint8_t)rnd_byte(max);
    }
    float rf = (float)max;
    for (int step = last; step > 1; step /= 2) {
        int h = step / 2;
        for (int y = h; y < n; y += step)
            for (int x = h; x < n; x += step) {
                int i = y * n + x;
                if (edge[i]) {
                    d[i] = edge[i];
                    continue;
                }
                int s = (d[(y - h) * n + x - h] + d[(y - h) * n + x + h] + d[(y + h) * n + x - h] +
                         d[(y + h) * n + x + h]) >> 2;
                d[i] = (uint8_t)clampi(s + rnd_range(range) - half, 0, max);
            }
        for (int y = 0; y < n; y += step) {
            int vr = y < h ? y + h : y - h;
            for (int x = h; x < n; x += step) {
                int i = y * n + x;
                if (edge[i]) {
                    d[i] = edge[i];
                    continue;
                }
                int s = (d[vr * n + x] * 2 + d[y * n + x - h] + d[y * n + x + h]) >> 2;
                d[i] = (uint8_t)clampi(s + rnd_range(range) - half, 0, max);
            }
        }
        for (int y = h; y < n; y += step)
            for (int x = 0; x < n; x += step) {
                int i = y * n + x;
                if (edge[i]) {
                    d[i] = edge[i];
                    continue;
                }
                int s = d[(y - h) * n + x] * 2;
                if (x < h)
                    s = (s + d[y * n + x + h] * 2) >> 2;
                else if (x > last - h)
                    s = (s + d[y * n + x - h] * 2) >> 2;
                else
                    s = (s + d[y * n + x - h] + d[y * n + x + h]) >> 2;
                d[i] = (uint8_t)clampi(s + rnd_range(range) - half, 0, max);
            }
        rf *= factor;
        range = (int)(unsigned)rf;
        half = range / 2;
    }
}

/* FUN_08b7f81c: distance on a wrapping axis of length n. */
static int wrap_dist(int a, int b, int n)
{
    int d;
    if (a < b) {
        d = b - a;
        if (n <= d * 2)
            d = a + n - b;
        return d;
    }
    d = a - b;
    if (n <= d * 2)
        d = b + n - a;
    return d;
}

/* GenerateVoronoiDiagram, FUN_08920218 with FUN_0891fd28 for the points. */
static void pass_voronoi(int sx, int sy, int max, int points, const uint8_t *edge, uint8_t *d)
{
    int count = SDL_min(points, SDL_min(sx, sy));
    int *tmp = SDL_malloc(sizeof(int) * (size_t)SDL_max(sx, sy));
    int *px = SDL_malloc(sizeof(int) * (size_t)SDL_max(count, 1) * 2);
    int *dist = SDL_malloc(sizeof(int) * (size_t)SDL_max(count, 1));
    int *f = SDL_malloc(sizeof(int) * (size_t)(sx * sy));
    if (count >= 2) {
        for (int i = 0; i < sx; i++)
            tmp[i] = i;
        game_qsort(tmp, sx, cmp_random);
        for (int i = 0; i < count; i++)
            px[2 * i] = tmp[i];
        for (int i = 0; i < sy; i++)
            tmp[i] = i;
        game_qsort(tmp, sy, cmp_random);
        for (int i = 0; i < count; i++)
            px[2 * i + 1] = tmp[i];
    }
    if (count < 2) {
        for (int i = 0; i < sx * sy; i++)
            d[i] = edge[i] ? edge[i] : (uint8_t)((max & 0xFF) / 2);
    } else {
        int fmax = 0;
        for (int y = 0; y < sy; y++)
            for (int x = 0; x < sx; x++) {
                int i = y * sx + x;
                f[i] = 0;
                if (edge[i])
                    continue;
                for (int k = 0; k < count; k++) {
                    int dx = wrap_dist(x, px[2 * k], sx), dy = wrap_dist(y, px[2 * k + 1], sy);
                    dist[k] = dx * dx + dy * dy;
                }
                game_qsort(dist, count, cmp_int);
                f[i] = dist[1] - dist[0];
                if (fmax < f[i])
                    fmax = f[i];
            }
        for (int i = 0; i < sx * sy; i++)
            d[i] = edge[i] ? edge[i] : (uint8_t)(fmax ? (max & 0xFF) * f[i] / fmax : 0);
    }
    SDL_free(tmp);
    SDL_free(px);
    SDL_free(dist);
    SDL_free(f);
}

/* CLandGenerateData::Merge, FUN_0891d988: a weighted average; a negative weight
 * uses the inverted heights of that side. */
static void pass_merge(uint8_t *dst, const uint8_t *src, int count, int max, int wd, int ws)
{
    for (int i = 0; i < count; i++) {
        int a = wd < 0 ? max - dst[i] : dst[i];
        int b = ws < 0 ? max - src[i] : src[i];
        int fa = wd < 0 ? -wd : wd, fb = ws < 0 ? -ws : ws;
        if (fa + fb)
            dst[i] = (uint8_t)((a * fa + b * fb) / (fa + fb));
    }
}

/* GlobalSmoothing, FUN_0892347c: blends 2x2 cells over shrinking random offsets. */
static void pass_smoothing(int sx, int sy, int passes, const uint8_t *edge, uint8_t *d)
{
#define H(i) (edge[i] ? edge[i] : d[i])
    for (; passes > 0; passes--) {
        int ox = 0, oy = 0;
        for (int w = sx, hgt = sy; w > 0 && hgt > 0; hgt /= 2) {
            int rx = w / 8, ry = hgt / 8;
            for (int x = ox; x < sx;) {
                int x1 = (x + (w - 1) / 4) % sx;
                int xe = (x + w - 1) % sx;
                int x2 = (x1 + (w - 1) / 2) % sx;
                for (int y = oy; y < sy;) {
                    int y1 = (y + (hgt - 1) / 4) % sy;
                    int a = H(y * sx + x), b = H(y * sx + xe);
                    int ye = (y + hgt - 1) % sy;
                    int c = H(ye * sx + x), e = H(ye * sx + xe);
                    int i = y1 * sx + x1;
                    d[i] = edge[i] ? edge[i] : (uint8_t)((a * 9 + b * 3 + c * 3 + e) >> 4);
                    i = y1 * sx + x2;
                    d[i] = edge[i] ? edge[i] : (uint8_t)((a * 3 + b * 9 + c + e * 3) >> 4);
                    int y2 = (y1 + (hgt - 1) / 2) % sy;
                    i = y2 * sx + x1;
                    d[i] = edge[i] ? edge[i] : (uint8_t)((a * 3 + b + c * 9 + e * 3) >> 4);
                    i = y2 * sx + x2;
                    d[i] = edge[i] ? edge[i] : (uint8_t)((a + b * 3 + c * 3 + e * 9) >> 4);
                    y += hgt;
                }
                x += w;
            }
            ox = rx + rnd_range(rx) + (passes & 1);
            oy = ry + rnd_range(ry) + (passes & 1);
            w /= 2;
        }
    }
#undef H
}

/* HydraulicErosion, FUN_0892391c: material slides to the steepest lower neighbour
 * when the drop is below the threshold. Neighbour sets alternate per pass. */
static void pass_erosion(int sx, int sy, int threshold, int passes, const uint8_t *edge, uint8_t *d)
{
    static const int k_nb[2][4][2] = {{{-1, 0}, {0, -1}, {1, 0}, {0, 1}}, {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}}};
    int best_i = 0;
    for (; passes > 0; passes--) {
        const int (*nb)[2] = k_nb[passes & 1];
        for (int half = 0; half < 2; half++)
            for (int y = 0; y < sy; y++)
                for (int x = (y + half) & 1; x < sx; x++) {
                    int i = y * sx + x;
                    int h = edge[i] ? edge[i] : d[i];
                    int best = 0;
                    for (int k = 0; k < 4; k++) {
                        int j = (sx + nb[k][0] + x) % sx + ((sy + nb[k][1] + y) % sy) * sx;
                        int hn = edge[j] ? edge[j] : d[j];
                        if (best < h - hn) {
                            best = h - hn;
                            best_i = j;
                        }
                    }
                    if (best > 0 && best < threshold) {
                        d[i] = edge[i] ? edge[i] : (uint8_t)(h - best / 2);
                        d[best_i] = edge[best_i] ? edge[best_i] : (uint8_t)(d[best_i] + best / 2);
                    }
                }
    }
}

/* Aliasing, FUN_08921180: rounds heights down to multiples of the step (terraces). */
static void pass_aliasing(int count, int step, const uint8_t *edge, uint8_t *d)
{
    for (int i = 0; i < count; i++)
        d[i] = edge[i] ? edge[i] : (uint8_t)(step * (d[i] / step));
}

/* ---- the terrain ---- */

typedef struct TVertex {
    float u, v;
    uint32_t color;
    float x, y, z;
} TVertex;

struct Terrain {
    int map;
    int size, off_x, off_z, cells_x, cells_z;
    uint8_t *h;
    float min_x, min_z, size_x, size_z, height;
    float light[3];
    TVertex *verts; /* (cells_x + 1) * 2 per row strip, cells_z strips */
    Picture *tex;
};

static float rdf(const uint8_t *p)
{
    float f;
    memcpy(&f, p, 4);
    return f;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* CStageGenerator, FUN_08aa610c. */
static bool generate(Terrain *t, const uint8_t *p)
{
    const uint8_t *b0 = p + 0x40, *b1 = p + 0x60, *b2 = p + 0x70, *b3 = p + 0x80, *b4 = p + 0x90,
                  *b5 = p + 0xA0, *b6 = p + 0xB0;
    mt_start(rd32(b0));
    int cx = b0[0x1C], cz = b0[0x1D], cmax = SDL_max(cx, cz), n = 0;
    if (cmax < 2)
        n = 2;
    else
        for (int s = 2; s < 0x401; s *= 2)
            if (cmax <= s) {
                n = s + 1;
                break;
            }
    if (!n)
        return false;
    t->size = n;
    t->cells_x = cx;
    t->cells_z = cz;
    t->off_x = (n - cx) / 2;
    t->off_z = (n - cz) / 2;
    uint8_t *edge = SDL_calloc(1, (size_t)(n * n)), *vor = SDL_calloc(1, (size_t)(n * n));
    t->h = SDL_calloc(1, (size_t)(n * n));
    if (b0[0x1E]) {
        /* FUN_0891d838: four border strips at the border height. */
        const int r[4][4] = {{0, 0, t->off_x + 2, n},
                             {t->off_x + cx - 1, 0, n, n},
                             {0, 0, n, t->off_z + 2},
                             {0, t->off_z + cz - 1, n, n}};
        for (int k = 0; k < 4; k++) {
            int x0 = clampi(r[k][0], 0, n - 1), y0 = clampi(r[k][1], 0, n - 1);
            int x1 = clampi(r[k][2], x0, n), y1 = clampi(r[k][3], y0, n);
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++)
                    edge[y * n + x] = b0[0x1E];
        }
    }
    uint8_t *d = t->h;
    if (!b1[4]) { /* fixed corners: (0,0), (0,last), (last,0), (last,last) as (x,y) */
        d[0] = b1[5];
        d[(n - 1) * n] = b1[6];
        d[n - 1] = b1[7];
        d[n * n - 1] = b1[8];
    }
    pass_midpoint(rdf(b1), n, 0xFF, edge, d, b1[4]);
    pass_voronoi(n, n, 0xFF, b2[0], edge, vor);
    if (b1[9] || b2[1])
        pass_merge(d, vor, n * n, 0xFF, (int8_t)b1[9], (int8_t)b2[1]);
    if (b3[0])
        pass_smoothing(n, n, b3[0], edge, d);
    if (b4[1])
        pass_erosion(n, n, b4[0], b4[1], edge, d);
    if (b5[1])
        SDL_Log("[terrain] map %02d: thermal erosion (%d, %d) not ported, skipped", t->map, b5[0], b5[1]);
    if (b6[0] > 1)
        pass_aliasing(n * n, b6[0], edge, d);
    if (b6[1] > 1) {
        /* CLandGenerateData::StretchCopy, FUN_0891dc64: magnify the middle of the
         * map by the factor, then put the border back. */
        int k = b6[1], o = (n - n / k) / 2;
        uint8_t *copy = SDL_malloc((size_t)(n * n));
        memcpy(copy, d, (size_t)(n * n));
        for (int y = 0; y < n; y++)
            for (int x = 0; x < n; x++)
                d[y * n + x] = copy[clampi(o + y / k, 0, n - 1) * n + clampi(o + x / k, 0, n - 1)];
        SDL_free(copy);
        for (int i = 0; i < n * n; i++)
            if (edge[i])
                d[i] = edge[i];
    }
    SDL_free(edge);
    SDL_free(vor);

    t->min_x = rdf(b0 + 4);
    t->min_z = rdf(b0 + 12);
    t->size_x = rdf(b0 + 16);
    t->height = rdf(b0 + 20);
    t->size_z = rdf(b0 + 24);
    for (int k = 0; k < 3; k++)
        t->light[k] = rdf(p + 0x110 + 4 * k);
    return true;
}

static float cell_h(const Terrain *t, int u, int v)
{
    u = clampi(u, 0, t->size - 1);
    v = clampi(v, 0, t->size - 1);
    return t->h[v * t->size + u] * t->height / 255.0f;
}

static void build_mesh(Terrain *t)
{
    int w = t->cells_x + 1;
    float dx = t->size_x / (float)t->cells_x, dz = t->size_z / (float)t->cells_z;
    float ll = sqrtf(t->light[0] * t->light[0] + t->light[1] * t->light[1] + t->light[2] * t->light[2]);
    if (ll < 1e-3f) {
        t->light[1] = 1.0f;
        ll = 1.0f;
    }
    /* In the PSP heap: the GE reads vertices through PSP addresses. */
    t->verts = heap_alloc(&g_default_heap, (uint32_t)(sizeof(TVertex) * (size_t)(w * 2 * t->cells_z)), 16, "land", 0);
    if (!t->verts)
        return;
    for (int row = 0; row < t->cells_z; row++)
        for (int i = 0; i < w; i++)
            for (int k = 0; k < 2; k++) {
                int u = t->off_x + i, v = t->off_z + row + k;
                TVertex *o = &t->verts[(row * w + i) * 2 + k];
                /* Shade from the map's light direction, as a stand-in for the GE's
                 * lighting the port doesn't do yet. */
                float nx = (cell_h(t, u - 1, v) - cell_h(t, u + 1, v)) / (2 * dx);
                float nz = (cell_h(t, u, v - 1) - cell_h(t, u, v + 1)) / (2 * dz);
                float nl = sqrtf(nx * nx + 1 + nz * nz);
                float lam = (nx * t->light[0] + t->light[1] + nz * t->light[2]) / (nl * ll);
                int c = clampi((int)(255 * (0.45f + 0.55f * SDL_max(lam, 0.0f))), 0, 255);
                o->color = 0xFF000000u | (uint32_t)c << 16 | (uint32_t)c << 8 | (uint32_t)c;
                o->u = (float)i;
                o->v = (float)(row + k);
                o->x = t->min_x + (float)i * dx;
                o->z = t->min_z + (float)(row + k) * dz;
                o->y = cell_h(t, u, v);
            }
}

Terrain *terrain_load(int map)
{
    char path[64];
    SDL_snprintf(path, sizeof path, "land/param/map%02d_a.mpp", map);
    uint8_t p[0x140];
    if (fileio_file_size(path) < (int32_t)sizeof p || fileio_read_file(path, p, sizeof p) < (int32_t)sizeof p ||
        memcmp(p, "CHLG", 4)) {
        SDL_Log("[terrain] %s: missing or not a CHLG file", path);
        return NULL;
    }
    Terrain *t = SDL_calloc(1, sizeof *t);
    t->map = map;
    if (!generate(t, p)) {
        terrain_free(t);
        return NULL;
    }
    int lo = 255, hi = 0;
    long sum = 0;
    for (int v = 0; v <= t->cells_z; v++)
        for (int u = 0; u <= t->cells_x; u++) {
            int h = t->h[(t->off_z + v) * t->size + t->off_x + u];
            lo = SDL_min(lo, h);
            hi = SDL_max(hi, h);
            sum += h;
        }
    SDL_Log("[terrain] map %02d heights: %d..%d, mean %ld", map, lo, hi, sum / ((t->cells_x + 1) * (t->cells_z + 1)));
    build_mesh(t);
    SDL_snprintf(path, sizeof path, "land/map%02d/map%02d_a00.gim", map, map);
    t->tex = picture_load(path);
    SDL_Log("[terrain] map %02d: %dx%d grid (%dx%d cells), %.0fx%.0f units, height %.0f, texture %s", map, t->size,
            t->size, t->cells_x, t->cells_z, t->size_x, t->size_z, t->height, t->tex ? path : "none");
    return t;
}

void terrain_free(Terrain *t)
{
    if (!t)
        return;
    if (t->tex)
        picture_release(t->tex);
    SDL_free(t->h);
    if (t->verts)
        heap_free(&g_default_heap, t->verts);
    SDL_free(t);
}

void terrain_extent(const Terrain *t, float out[4])
{
    out[0] = t->min_x;
    out[1] = t->min_z;
    out[2] = t->size_x;
    out[3] = t->size_z;
}

float terrain_height(const Terrain *t, float x, float z)
{
    float fu = (x - t->min_x) * (float)t->cells_x / t->size_x;
    float fv = (z - t->min_z) * (float)t->cells_z / t->size_z;
    fu = SDL_clamp(fu, 0.0f, (float)t->cells_x - 0.001f);
    fv = SDL_clamp(fv, 0.0f, (float)t->cells_z - 0.001f);
    int iu = (int)fu, iv = (int)fv;
    float a = fu - (float)iu, b = fv - (float)iv;
    int u = t->off_x + iu, v = t->off_z + iv;
    float h00 = cell_h(t, u, v), h10 = cell_h(t, u + 1, v), h01 = cell_h(t, u, v + 1), h11 = cell_h(t, u + 1, v + 1);
    return (h00 * (1 - a) + h10 * a) * (1 - b) + (h01 * (1 - a) + h11 * a) * b;
}

void terrain_draw(const Terrain *t)
{
    if (!t || !t->verts)
        return;
    if (t->tex) {
        sceGuEnable(GU_TEXTURE_2D);
        picture_bind(t->tex);
        sceGuTexWrap(0, 0);
        sceGuTexScale(1.0f, 1.0f);
        sceGuTexOffset(0.0f, 0.0f);
        sceGuTexFunc(0, 1);
    } else {
        sceGuDisable(GU_TEXTURE_2D);
    }
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumUpdateMatrix();
    int w = t->cells_x + 1;
    for (int row = 0; row < t->cells_z; row++)
        sceGuDrawArray(GU_TRIANGLE_STRIP, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF, w * 2, NULL,
                       &t->verts[row * w * 2]);
}

const uint8_t *terrain_heights(const Terrain *t, int *size)
{
    *size = t ? t->size : 0;
    return t ? t->h : NULL;
}
