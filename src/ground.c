/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2024-2026 Daniel Sandner
 *
 * The open land's ground caster, as CLIo's engine/land.py Land.draw has it: rays from near to far, one a
 * pixel column (or every other), each filling the column up from the last row it reached, with the haze
 * mixed in by distance and each pixel's distance kept. The same operations in the same order as the
 * Python, so the same pixels come out. */
#define CLIO_PICTURE_BUILD
#include "clio_picture.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>

#define FAR 1e9

/* floor(v / 16) and v mod 16 (0..15), for negative v too - Python's v >> 4 and v & 15 */
static int64_t chunk_of(int64_t v) { return v >= 0 ? v / 16 : -((15 - v) / 16); }
static int cell_in(int64_t v) { return (int)(v - chunk_of(v) * 16); }

static int64_t py_mod2(int64_t v) { int64_t m = v % 2; return m < 0 ? m + 2 : m; }

/* ---- chunks: asked for once a picture, then kept in a small table ------------------------------- */
typedef struct {
    int64_t kx, ky;
    const clio_chunk *ch;
} slot;

typedef struct {
    slot *slots;
    size_t cap, used;
    clio_chunk_fn fn;
    void *user;
    int64_t last_kx, last_ky;
    const clio_chunk *last;
    int failed;   /* -2: the caller gave no chunk, -3: no memory */
    /* 0.8: terraces - map cells standing higher than the pad (their own heights over base) */
    int64_t *txy;
    double *th;
    size_t tcap;
    double base;
} chunks;

static size_t hash_key(int64_t kx, int64_t ky, size_t cap)
{
    uint64_t h = (uint64_t)kx * 0x9E3779B97F4A7C15u ^ (uint64_t)ky * 0xC2B2AE3D27D4EB4Fu;
    h ^= h >> 29;
    return (size_t)(h & (cap - 1));
}

static int grow(chunks *t)
{
    size_t cap = t->cap ? t->cap * 2 : 256, i;
    slot *s = (slot *)calloc(cap, sizeof(slot));
    if (!s)
        return 0;
    for (i = 0; i < t->cap; i++)
        if (t->slots[i].ch) {
            size_t j = hash_key(t->slots[i].kx, t->slots[i].ky, cap);
            while (s[j].ch)
                j = (j + 1) & (cap - 1);
            s[j] = t->slots[i];
        }
    free(t->slots);
    t->slots = s;
    t->cap = cap;
    return 1;
}

static const clio_chunk *chunk_at(chunks *t, int64_t kx, int64_t ky)
{
    size_t j;
    const clio_chunk *ch;
    if (t->last && kx == t->last_kx && ky == t->last_ky)
        return t->last;
    if (t->failed)
        return NULL;
    j = hash_key(kx, ky, t->cap);
    while (t->slots[j].ch) {
        if (t->slots[j].kx == kx && t->slots[j].ky == ky) {
            ch = t->slots[j].ch;
            goto found;
        }
        j = (j + 1) & (t->cap - 1);
    }
    ch = t->fn(t->user, kx, ky);
    if (!ch) {
        t->failed = -2;
        return NULL;
    }
    if ((t->used + 1) * 2 > t->cap) {
        if (!grow(t)) {
            t->failed = -3;
            return NULL;
        }
        j = hash_key(kx, ky, t->cap);
        while (t->slots[j].ch)
            j = (j + 1) & (t->cap - 1);
    }
    t->slots[j].kx = kx;
    t->slots[j].ky = ky;
    t->slots[j].ch = ch;
    t->used++;
found:
    t->last_kx = kx;
    t->last_ky = ky;
    t->last = ch;
    return ch;
}

/* a terrace's height over the base at (x, y): 1 and *h, or 0 for none */
static int terrace_at(const chunks *t, int64_t x, int64_t y, double *h)
{
    size_t j;
    if (!t->tcap)
        return 0;
    j = hash_key(x, y, t->tcap);
    while (t->th[j] == t->th[j]) {                            /* NaN marks an empty slot */
        if (t->txy[2 * j] == x && t->txy[2 * j + 1] == y) {
            *h = t->th[j];
            return 1;
        }
        j = (j + 1) & (t->tcap - 1);
    }
    return 0;
}

static int terraces_make(chunks *t, int n, const int64_t *xy, const double *h)
{
    size_t cap = 16;
    int i;
    if (n <= 0)
        return 1;
    while (cap < (size_t)n * 2)
        cap *= 2;
    t->txy = (int64_t *)malloc(cap * 2 * sizeof(int64_t));
    t->th = (double *)malloc(cap * sizeof(double));
    if (!t->txy || !t->th)
        return 0;
    t->tcap = cap;
    for (i = 0; i < (int)cap; i++)
        t->th[i] = NAN;
    for (i = 0; i < n; i++) {
        size_t j = hash_key(xy[2 * i], xy[2 * i + 1], cap);
        while (t->th[j] == t->th[j] && !(t->txy[2 * j] == xy[2 * i] && t->txy[2 * j + 1] == xy[2 * i + 1]))
            j = (j + 1) & (cap - 1);
        t->txy[2 * j] = xy[2 * i];
        t->txy[2 * j + 1] = xy[2 * i + 1];
        t->th[j] = h[i];
    }
    return 1;
}

static int height_at(chunks *t, int64_t x, int64_t y, double *h)
{
    const clio_chunk *ch;
    double tr;
    if (terrace_at(t, x, y, &tr)) {                           /* Land.ground: a terrace's cell at its own height */
        *h = t->base + tr;
        return 1;
    }
    ch = chunk_at(t, chunk_of(x), chunk_of(y));
    if (!ch)
        return 0;
    *h = ch->height[cell_in(y) * 16 + cell_in(x)];
    return 1;
}

/* Land.ground: the ground under a point between cells, blended from the four round it */
static int ground_at(chunks *t, double x, double y, double *out)
{
    double x0f = floor(x), y0f = floor(y), fx, fy, h00, h10, h01, h11;
    int64_t x0 = (int64_t)x0f, y0 = (int64_t)y0f;
    fx = x - x0f;
    fy = y - y0f;
    if (!height_at(t, x0, y0, &h00) || !height_at(t, x0 + 1, y0, &h10) ||
        !height_at(t, x0, y0 + 1, &h01) || !height_at(t, x0 + 1, y0 + 1, &h11))
        return 0;
    *out = (h00 * (1 - fx) + h10 * fx) * (1 - fy) + (h01 * (1 - fx) + h11 * fx) * fy;
    return 1;
}

/* ---- the cells a map stands on ---------------------------------------------------------------------- */
typedef struct {
    int64_t *xy;     /* 2 a slot; tex[i] == -3: empty */
    int32_t *tex;
    size_t cap;
} stamps;

static int stamps_make(stamps *s, const clio_ground *g)
{
    int i;
    size_t cap = 16;
    s->xy = NULL;
    s->tex = NULL;
    s->cap = 0;
    if (g->stamps <= 0)
        return 1;
    while (cap < (size_t)g->stamps * 2)
        cap *= 2;
    s->xy = (int64_t *)malloc(cap * 2 * sizeof(int64_t));
    s->tex = (int32_t *)malloc(cap * sizeof(int32_t));
    if (!s->xy || !s->tex)
        return 0;
    s->cap = cap;
    for (i = 0; i < (int)cap; i++)
        s->tex[i] = -3;
    for (i = 0; i < g->stamps; i++) {
        int64_t x = g->stamp_xy[2 * i], y = g->stamp_xy[2 * i + 1];
        size_t j = hash_key(x, y, cap);
        while (s->tex[j] != -3 && !(s->xy[2 * j] == x && s->xy[2 * j + 1] == y))
            j = (j + 1) & (cap - 1);
        s->xy[2 * j] = x;
        s->xy[2 * j + 1] = y;
        s->tex[j] = g->stamp_tex[i];
    }
    return 1;
}

/* -3: no stamp here; -1: a plain one; else its texture */
static int32_t stamp_at(const stamps *s, int64_t x, int64_t y)
{
    size_t j;
    if (!s->cap)
        return -3;
    j = hash_key(x, y, s->cap);
    while (s->tex[j] != -3) {
        if (s->xy[2 * j] == x && s->xy[2 * j + 1] == y)
            return s->tex[j];
        j = (j + 1) & (s->cap - 1);
    }
    return -3;
}

/* ---- colours: frame.py's mix and the haze ---------------------------------------------------------- */
#define R(c) ((int)(((c) >> 16) & 255u))
#define G(c) ((int)(((c) >> 8) & 255u))
#define B(c) ((int)((c) & 255u))
#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

static int part(int a, int b, double k) { return (int)(a + (b - a) * k); }

static uint32_t mix(uint32_t a, uint32_t b, double k)
{
    return RGB(part(R(a), R(b), k), part(G(a), G(b), k), part(B(a), B(b), k));
}

static int grain(int c, double g)
{
    int v = (int)(c * g);
    return v > 255 ? 255 : v;
}

int clio_land_draw(const clio_ground *g, clio_chunk_fn chunk, void *user, const uint8_t *sky, int sky_rows,
                   uint32_t *rgb, double *depth)
{
    return clio_land_terraced(g, 0, NULL, NULL, chunk, user, sky, sky_rows, rgb, depth);
}

int clio_land_terraced(const clio_ground *g, int nterraces, const int64_t *terrace_xy, const double *terrace_h,
                       clio_chunk_fn chunk, void *user, const uint8_t *sky, int sky_rows, uint32_t *rgb,
                       double *depth)
{
    int PW, PH, yh, step, ncols, alive, i, s, x, y;
    int *ybuf = NULL, *cols = NULL;
    double *offs = NULL;
    chunks t = {0};
    stamps st = {0};
    int result = 0;
    uint32_t haze;
    int hr, hg, hb;
    double ox, oy;

    if (!g || !chunk || !rgb || !depth || g->pw <= 0 || g->ph <= 0 || g->step < 1 || g->step > 2 ||
        g->steps < 0 || (g->steps && (!g->d || !g->fog)) || sky_rows < 0 || (sky_rows && !sky) ||
        (g->stamps > 0 && (!g->stamp_xy || !g->stamp_tex)) || nterraces < 0 ||
        (nterraces > 0 && (!terrace_xy || !terrace_h)))
        return -1;
    PW = g->pw;
    PH = g->ph;
    yh = g->yh;
    step = g->step;
    haze = g->haze;
    hr = R(haze);
    hg = G(haze);
    hb = B(haze);

    for (y = 0; y < PH; y++)                    /* the sky, then haze, and nothing near yet */
        for (x = 0; x < PW; x++) {
            size_t p = (size_t)y * PW + x;
            if (y < sky_rows) {
                const uint8_t *q = sky + p * 3;
                rgb[p] = RGB(q[0], q[1], q[2]);
            } else
                rgb[p] = haze;
            depth[p] = FAR;
        }

    ncols = (PW + step - 1) / step;
    ybuf = (int *)malloc(sizeof(int) * PW);
    cols = (int *)malloc(sizeof(int) * ncols);
    offs = (double *)malloc(sizeof(double) * ncols);
    t.fn = chunk;
    t.user = user;
    t.base = g->base;
    if (!ybuf || !cols || !offs || !grow(&t) || !stamps_make(&st, g) ||
        !terraces_make(&t, nterraces, terrace_xy, terrace_h)) {
        result = -3;
        goto done;
    }
    for (x = 0; x < PW; x++)
        ybuf[x] = PH;
    for (i = 0; i < ncols; i++) {
        cols[i] = i * step;
        offs[i] = (double)cols[i] + step * 0.5 - g->half;
    }
    ox = g->cx - g->fx * g->z0;
    oy = g->cy - g->fy * g->z0;
    alive = ncols;

    for (s = 0; s < g->steps && alive; s++) {
        double D = g->d[s];
        double tD = g->z0 / D;
        double syk = g->tall * tD;
        double inv = 1.0 / (g->lw * tD);
        double bx = ox + g->fx * D, by = oy + g->fy * D;
        double z = D - g->z0;
        double fogk = g->fog[s];
        double keep = 1.0 - fogk;
        int near = D < 9.0;
        for (i = 0; i < ncols; i++) {
            int yb, it, top, yy;
            double o, wx, wy, h, ytop;
            int64_t ix, iy;
            const clio_chunk *ch;
            int k, kind;
            int32_t tex;
            uint32_t col, c;
            x = cols[i];
            yb = ybuf[x];
            if (yb <= 0)
                continue;
            o = offs[i] * inv;
            wx = bx + g->rx * o;
            wy = by + g->ry * o;
            ix = (int64_t)(wx + 0.5);                        /* cells' middles are whole numbers */
            if (wx < -0.5)
                ix -= 1;
            iy = (int64_t)(wy + 0.5);
            if (wy < -0.5)
                iy -= 1;
            ch = chunk_at(&t, chunk_of(ix), chunk_of(iy));
            if (!ch) {
                result = t.failed;
                goto done;
            }
            k = cell_in(iy) * 16 + cell_in(ix);
            kind = ch->kind[k];
            tex = stamp_at(&st, ix, iy);
            if (tex != -3) {                                 /* a map stands here: its floor */
                double tr;
                h = g->base;
                if (terrace_at(&t, ix, iy, &tr))               /* a terrace: higher */
                    h = g->base + tr;
                if (tex >= 0) {
                    double u = wx + 0.5 - ix, v = wy + 0.5 - iy;
                    int tw = g->tex_w[tex], th = g->tex_h[tex];
                    int ty = (int)(v * th), tx = (int)(u * tw);
                    if (ty > th - 1)
                        ty = th - 1;
                    if (tx > tw - 1)
                        tx = tw - 1;
                    col = g->texels[g->tex_at[tex] + ty * tw + tx];
                } else
                    col = py_mod2(ix + iy) ? RGB(70, 70, 74) : RGB(62, 62, 66);
            } else {
                if (near && kind >= 0) {
                    double gr;
                    if (!ground_at(&t, wx + 0.5, wy + 0.5, &h)) {
                        result = t.failed;
                        goto done;
                    }
                    col = ch->rgb[k];
                    gr = 0.9 + 0.2 * clio_hash((int64_t)(wx * 4), (int64_t)(wy * 4), 3);   /* the near ground's grain */
                    col = RGB(grain(R(col), gr), grain(G(col), gr), grain(B(col), gr));
                } else {
                    h = ch->height[k];
                    col = ch->rgb[k];
                }
            }
            ytop = yh + (g->eye - h) * syk;
            it = ytop >= 0 ? (int)ytop : -1;
            if (it < yb) {
                if (kind < 0) {                              /* water: the sky in it, and a glint */
                    int ym = 2 * yh - it;
                    if (ym >= 0 && ym < yh)
                        col = mix(col, rgb[(size_t)ym * PW + x], 0.35);
                    if (clio_hash(ix, iy, g->phase) < 0.05)
                        col = mix(col, RGB(230, 230, 220), 0.4);
                }
                c = RGB((int)(R(col) * keep + hr * fogk), (int)(G(col) * keep + hg * fogk),
                        (int)(B(col) * keep + hb * fogk));
                top = it > 0 ? it : 0;
                for (yy = top; yy < yb; yy++) {
                    size_t p = (size_t)yy * PW + x;
                    rgb[p] = c;
                    depth[p] = z;
                    if (step == 2 && x + 1 < PW) {
                        rgb[p + 1] = c;
                        depth[p + 1] = z;
                    }
                }
                ybuf[x] = top;
                if (top <= 0)
                    alive--;
            }
        }
    }
done:
    free(ybuf);
    free(cols);
    free(offs);
    free(t.slots);
    free(st.xy);
    free(st.tex);
    free(t.txy);
    free(t.th);
    return result;
}
