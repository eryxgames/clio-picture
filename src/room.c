/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2024-2026 Daniel Sandner
 *
 * The rooms' floor and ceiling, as CLIo's engine/firstperson.py View._backdrop draws them: for each pixel
 * row the distance it sees, for each pixel the cell under it - its tile, what is set in it (plates, lifts,
 * belts, oil...), toxic water, fire and the marks of fire to come - then the row's fog. The same
 * operations in the same order as the Python, so the same pixels come out. */
#define CLIO_PICTURE_BUILD
#include "clio_picture.h"

#include <math.h>
#include <stdlib.h>

#define R(c) ((int)(((c) >> 16) & 255u))
#define G(c) ((int)(((c) >> 8) & 255u))
#define B(c) ((int)((c) & 255u))
#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define NONE CLIO_CLEAR

/* engine/firstperson.py's colours */
#define FLOOR_A RGB(68, 61, 53)
#define FLOOR_B RGB(58, 53, 47)
#define FLOOR_LINE RGB(94, 86, 72)
#define CEIL RGB(30, 30, 36)
#define CEIL_LINE RGB(24, 24, 29)
#define LIGHT RGB(168, 162, 122)
#define TOXIC RGB(36, 140, 54)
#define TOXIC_HI RGB(130, 240, 110)
#define HAZE RGB(16, 16, 20)

static const int DIRS[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};

static double noise(int64_t i, int64_t j, int64_t k)
{
    uint64_t h = ((uint64_t)i * 374761393u + (uint64_t)j * 668265263u + (uint64_t)k * 2246822519u) & 0xFFFFFFFFu;
    h = ((h ^ (h >> 13)) * 1274126177u) & 0xFFFFFFFFu;
    return (double)((h ^ (h >> 16)) & 0xFFFFu) / 65536.0;
}

static int64_t pymod(int64_t a, int64_t m) { int64_t r = a % m; return r < 0 ? r + m : r; }
static int64_t pydiv(int64_t a, int64_t m) { return (a - pymod(a, m)) / m; }

static int part(int a, int b, double k) { return (int)(a + (b - a) * k); }
static uint32_t mix(uint32_t a, uint32_t b, double k)
{
    return RGB(part(R(a), R(b), k), part(G(a), G(b), k), part(B(a), B(b), k));
}

static int lit(int c, double f) { int v = (int)(c * f); return v > 255 ? 255 : v; }
static uint32_t shade(uint32_t c, double f) { return RGB(lit(R(c), f), lit(G(c), f), lit(B(c), f)); }

/* ---- the cells seen, each asked for once --------------------------------------------------------------- */
typedef struct {
    int64_t x, y;
    int used;
    clio_room_cell c;
} cslot;

typedef struct {
    cslot *s;
    size_t cap, used;
} ctable;

static size_t hash_xy(int64_t x, int64_t y, size_t cap)
{
    uint64_t h = (uint64_t)x * 0x9E3779B97F4A7C15u ^ (uint64_t)y * 0xC2B2AE3D27D4EB4Fu;
    h ^= h >> 29;
    return (size_t)(h & (cap - 1));
}

static int ctable_grow(ctable *t)
{
    size_t cap = t->cap ? t->cap * 2 : 512, i;
    cslot *s = (cslot *)calloc(cap, sizeof(cslot));
    if (!s)
        return 0;
    for (i = 0; i < t->cap; i++)
        if (t->s[i].used) {
            size_t j = hash_xy(t->s[i].x, t->s[i].y, cap);
            while (s[j].used)
                j = (j + 1) & (cap - 1);
            s[j] = t->s[i];
        }
    free(t->s);
    t->s = s;
    t->cap = cap;
    return 1;
}

/* the cell's floor or ceiling, asked of the caller the first time; NULL: stopped (*err set) */
static const clio_room_cell *cell_at(ctable *t, clio_room_cell_fn fn, void *user, int what, int64_t x, int64_t y,
                                     int *err)
{
    size_t j = hash_xy(x, y, t->cap);
    while (t->s[j].used) {
        if (t->s[j].x == x && t->s[j].y == y)
            return &t->s[j].c;
        j = (j + 1) & (t->cap - 1);
    }
    if ((t->used + 1) * 2 > t->cap) {
        if (!ctable_grow(t)) {
            *err = -3;
            return NULL;
        }
        j = hash_xy(x, y, t->cap);
        while (t->s[j].used)
            j = (j + 1) & (t->cap - 1);
    }
    t->s[j].c.tex = NULL;
    t->s[j].c.tex_w = t->s[j].c.tex_h = 0;
    t->s[j].c.kind = t->s[j].c.belt = t->s[j].c.toxic = t->s[j].c.fire = t->s[j].c.mark = 0;
    t->s[j].c.x = x;
    t->s[j].c.y = y;
    if (fn(user, what, x, y, &t->s[j].c) != 0) {
        *err = -2;
        return NULL;
    }
    t->s[j].x = x;
    t->s[j].y = y;
    t->s[j].used = 1;
    t->used++;
    return &t->s[j].c;
}

static uint32_t texel(const clio_room_cell *c, double ux, double uy)
{
    int th = c->tex_h, tw = c->tex_w;
    int ty = (int)((uy + 0.5) * th), tx = (int)((ux + 0.5) * tw);
    if (ty > th - 1)
        ty = th - 1;
    if (tx > tw - 1)
        tx = tw - 1;
    return c->tex[ty * tw + tx];
}

/* View.fixture, but for teleporters: NONE where nothing is set there */
static uint32_t fixture(const clio_room_cell *c, double ux, double uy, int64_t phase)
{
    double m = fabs(ux) > fabs(uy) ? fabs(ux) : fabs(uy);
    switch (c->kind) {
    case CLIO_FLOOR_PLATE:
    case CLIO_FLOOR_PLATE_DOWN: {
        int down = c->kind == CLIO_FLOOR_PLATE_DOWN;
        if (m < 0.34) {
            uint32_t col = !down ? RGB(150, 128, 70) : RGB(96, 82, 46);
            return (m > 0.3 && !down) ? shade(col, 1.15) : col;
        }
        return m < 0.38 ? RGB(34, 32, 30) : NONE;
    }
    case CLIO_FLOOR_SUNK:
        return ((int64_t)((uy + 0.5) * 6) % 2) ? RGB(104, 78, 50) : RGB(88, 64, 40);
    case CLIO_FLOOR_LIFT:
        if (m > 0.42)
            return pymod((int64_t)((ux + uy + 2) * 8), 2) ? RGB(215, 175, 40) : RGB(34, 32, 30);
        return pymod((int64_t)((ux + 0.5) * 8), 4) ? RGB(70, 74, 84) : RGB(52, 55, 62);
    case CLIO_FLOOR_BELT: {
        int dx = DIRS[c->belt & 3][0], dy = DIRS[c->belt & 3][1];
        double along = ux * dx + uy * dy, across = ux * -dy + uy * dx, chevron;
        if (fabs(across) > 0.4)
            return RGB(60, 62, 68);
        chevron = along * 3.0 - fabs(across) * 2.0 - phase * 0.34;
        chevron = chevron - floor(chevron);                /* Python's % 1.0 */
        return chevron < 0.3 ? RGB(214, 170, 44) : RGB(42, 42, 46);
    }
    case CLIO_FLOOR_OIL: {
        double sheen = noise((int64_t)((ux + 0.5) * 10) + c->x * 10, (int64_t)((uy + 0.5) * 10) + c->y * 10,
                             pydiv(phase, 2));
        return sheen < 0.06 ? RGB(70, 56, 96) : sheen < 0.5 ? RGB(24, 22, 20) : RGB(32, 28, 24);
    }
    case CLIO_FLOOR_SLICK:
        return pymod((int64_t)((ux - uy + 2) * 7), 5) == 0 ? RGB(128, 160, 176) : RGB(62, 84, 98);
    case CLIO_FLOOR_SCORCHED:
        return noise((int64_t)(ux * 9) + c->x * 9, (int64_t)(uy * 9) + c->y * 9, 1) < 0.6 ? RGB(26, 24, 22)
                                                                                           : RGB(44, 38, 34);
    default:
        return NONE;
    }
}

int clio_room_draw(const clio_room *r, clio_room_cell_fn cell, void *user, uint32_t *rgb, int32_t *later,
                   int *later_n)
{
    ctable below = {0}, above = {0};
    int PW, PH, step, x, y, n = 0, err = 0;
    if (!r || !cell || !rgb || !later || !later_n || r->pw <= 0 || r->ph <= 0 || r->step < 1 || r->step > 2 ||
        !r->row_kind || !r->row_z || !r->row_fog || !r->row_span)
        return -1;
    PW = r->pw;
    PH = r->ph;
    step = r->step;
    if (!ctable_grow(&below) || !ctable_grow(&above)) {
        err = -3;
        goto done;
    }
    for (y = 0; y < PH; y++) {
        uint32_t *row = rgb + (size_t)y * PW;
        int kind = r->row_kind[y];
        double z = r->row_z[y], fog = r->row_fog[y], span = r->row_span[y];
        double bx = r->cx + r->fx * z, by = r->cy + r->fy * z;
        const clio_room_cell *c = NULL;
        int64_t lx = 0, ly = 0;
        int have = 0;
        if (kind == 2) {                                   /* beyond the walls drawn: the dark far away */
            for (x = 0; x < PW; x++)
                row[x] = HAZE;
            continue;
        }
        for (x = 0; x < PW; x += step) {
            double a = (x + 0.5 * step - r->half) / span;
            double wx = bx + r->rx * a, wy = by + r->ry * a;
            double fwx = floor(wx + 0.5), fwy = floor(wy + 0.5);
            int64_t ix = (int64_t)fwx, iy = (int64_t)fwy;
            double ux = wx - fwx, uy = wy - fwy;
            uint32_t col = NONE;
            if (!have || ix != lx || iy != ly) {
                lx = ix;
                ly = iy;
                have = 1;
                c = cell_at(kind == 0 ? &below : &above, cell, user, kind, ix, iy, &err);
                if (!c)
                    goto done;
            }
            if (kind == 0) {                               /* the floor */
                if (c->tex)
                    col = texel(c, ux, uy);
                if (col == NONE && c->kind != CLIO_FLOOR_PLAIN) {
                    if (c->kind == CLIO_FLOOR_TELEPORT) {  /* the caller's */
                        later[n++] = y * PW + x;
                        row[x] = HAZE;
                        continue;
                    }
                    col = fixture(c, ux, uy, r->phase);
                }
                if (col == NONE && c->toxic) {
                    col = noise((int64_t)(ux * 8) + ix * 8, (int64_t)(uy * 8) + iy * 8, r->phase) < 0.07 ? TOXIC_HI
                                                                                                      : TOXIC;
                    row[x] = shade(col, 0.45 + 0.55 * fog);
                    continue;
                }
                if (col == NONE) {
                    col = ((ix + iy) & 1) ? FLOOR_A : FLOOR_B;
                    if (fabs(ux) > 0.46 || fabs(uy) > 0.46)
                        col = FLOOR_LINE;
                }
                if (c->fire)
                    col = mix(col, RGB(255, 120, 30),
                              0.3 + 0.25 * noise((int64_t)(ux * 6) + ix * 6, (int64_t)(uy * 6) + iy * 6, r->phase));
                if (c->mark == 1) {                        /* fire is scheduled here: hatched and glowing */
                    int hatch = pymod((int64_t)((ux + uy + 1.0) * 6), 2) == 0;
                    col = mix(col, hatch ? RGB(255, 90, 30) : RGB(150, 30, 20), 0.55 + 0.2 * pymod(r->phase, 2));
                    row[x] = shade(col, 0.6 + 0.4 * fog);
                    continue;
                } else if (c->mark == 2) {
                    double k = noise((int64_t)(ux * 10) + ix * 10, (int64_t)(uy * 10) + iy * 10, r->phase + 5);
                    row[x] = k < 0.25 ? RGB(255, 230, 120) : k < 0.7 ? RGB(255, 140, 40) : RGB(200, 50, 20);
                    continue;
                }
            } else {                                       /* the ceiling */
                if (c->kind == CLIO_CEILING_SKY && r->sky) {
                    const uint8_t *q = r->sky + ((size_t)y * PW + x) * 3;
                    row[x] = RGB(q[0], q[1], q[2]);
                    continue;
                }
                if (c->tex)
                    col = texel(c, ux, uy);
                if (col == NONE) {
                    col = CEIL;
                    if (fabs(ux) > 0.47 || fabs(uy) > 0.47)
                        col = CEIL_LINE;
                    else if (pymod(ix * 7 + iy * 3, 4) == 0 && fabs(ux) < 0.24 && fabs(uy) < 0.24)
                        col = LIGHT;
                }
            }
            row[x] = shade(col, fog);
        }
        if (step == 2)                                     /* row[1::2] = row[0:PW - 1:2] */
            for (x = 1; x < PW; x += 2)
                row[x] = row[x - 1];
    }
done:
    *later_n = n;
    free(below.s);
    free(above.s);
    return err;
}
