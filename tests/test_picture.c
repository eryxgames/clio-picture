/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2024-2026 Daniel Sandner
 *
 * The library against values CLIo's Python gives (engine/land.py, engine/firstperson.py), compared to the
 * last bit. cases.h is written by make_cases.py. */
#include "clio_picture.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* CLIo: Land with seed 4207, scale 30, warp 0.6, rough 0.5, ridges 0.25; raw(10, 20) and raw(12, 21) */
#define LAND_10_20 0.12434607170673907
#define LAND_12_21 0.16313118904503462

/* CLIo: View.braille of 6 x 8 pixels (3 x 2 cells), in the dots and dots-fill styles; steps from
 * engine.picture.steps(dot_level) */
static const double STEPS[8] = {12.002967629918455, 31.201586888453544, 48.650649057530366, 65.18632472776656, 81.10819377127072, 96.57116965332946, 111.66961740449618, 126.46684557946332};
static const uint8_t PX[144] = {60, 60, 60, 60, 60, 60, 0, 0, 0, 2, 1, 1, 212, 172, 59, 9, 13, 72, 60, 60, 60, 60, 60, 60, 2, 1, 1, 0, 0, 0, 241, 183, 130, 38, 24, 143, 60, 60, 60, 60, 60, 60, 0, 0, 0, 0, 0, 0, 14, 194, 201, 67, 35, 214, 60, 60, 60, 60, 60, 60, 0, 0, 0, 2, 1, 1, 43, 205, 16, 96, 46, 29, 116, 84, 35, 169, 181, 48, 222, 22, 61, 19, 119, 74, 72, 216, 87, 125, 57, 100, 145, 95, 106, 198, 192, 119, 251, 33, 132, 48, 130, 145, 101, 227, 158, 154, 68, 171, 174, 106, 177, 227, 203, 190, 24, 44, 203, 77, 141, 216, 130, 238, 229, 183, 79, 242, 203, 117, 248, 0, 214, 5, 53, 55, 18, 106, 152, 31, 159, 249, 44, 212, 90, 57};
static const uint8_t WANT_DOTS[48] = {21, 2, 114, 114, 114, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 71, 2, 145, 215, 115, 0, 0, 0, 253, 2, 213, 215, 161, 0, 0, 0, 189, 2, 145, 179, 215, 0, 0, 0, 247, 2, 186, 214, 182, 0, 0, 0};
static const uint8_t WANT_FILL[48] = {0, 1, 0, 0, 0, 60, 60, 60, 0, 1, 0, 0, 0, 0, 0, 0, 71, 3, 127, 188, 101, 52, 29, 114, 184, 3, 148, 197, 90, 159, 100, 141, 184, 3, 62, 135, 116, 137, 38, 103, 71, 3, 115, 232, 129, 168, 73, 142};

static int frame_model_failed = 0;

#include "cases.h"
#include "scope_cases.h"

static int bad = 0;

/* ---- the made-up land of make_cases.py: a chunk from formulas -------------------------------------- */
static int64_t pmod(int64_t a, int64_t m) { int64_t r = a % m; return r < 0 ? r + m : r; }

static clio_chunk made[64];
static int64_t made_key[64][2];
static int made_n = 0, asked = 0;

static const clio_chunk *made_chunk(void *user, int64_t kx, int64_t ky)
{
    int i, x, y;
    clio_chunk *ch;
    (void)user;
    asked++;
    for (i = 0; i < made_n; i++)
        if (made_key[i][0] == kx && made_key[i][1] == ky)
            return &made[i];
    if (made_n == 64)
        return NULL;
    ch = &made[made_n];
    made_key[made_n][0] = kx;
    made_key[made_n][1] = ky;
    made_n++;
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) {
            int64_t X = kx * 16 + x, Y = ky * 16 + y;
            int k = y * 16 + x;
            ch->height[k] = 0.25 * (double)pmod(X * 3 + Y * 5, 17);
            ch->kind[k] = (int16_t)(pmod(X * X + Y, 23) == 0 ? -2 : pmod(X + 2 * Y, 19) == 0 ? -1 : pmod(X + Y, 3));
            ch->rgb[k] = ((uint32_t)((X * 37) & 255) << 16) | ((uint32_t)((Y * 53) & 255) << 8) |
                         (uint32_t)(((X + Y) * 11) & 255);
        }
    return ch;
}

static const clio_chunk *no_chunk(void *user, int64_t kx, int64_t ky)
{
    (void)user;
    (void)kx;
    (void)ky;
    return NULL;
}

static uint64_t fnv(uint64_t h, const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t i;
    for (i = 0; i < n; i++)
        h = (h ^ p[i]) * 0x100000001B3ULL;
    return h;
}

/* little-endian bytes of a 32-bit and a 64-bit value, as Python's sums take them */
static uint64_t fnv32(uint64_t h, uint32_t v)
{
    uint8_t b[4];
    b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24);
    return fnv(h, b, 4);
}

static uint64_t fnv_double(uint64_t h, double d)
{
    uint64_t v;
    uint8_t b[8];
    int i;
    memcpy(&v, &d, 8);
    for (i = 0; i < 8; i++)
        b[i] = (uint8_t)(v >> (8 * i));
    return fnv(h, b, 8);
}

static void land_case(const char *what, const clio_ground *g, int sky_rows, uint64_t want)
{
    size_t n = (size_t)g->pw * g->ph, i;
    uint32_t *rgb = (uint32_t *)malloc(n * 4);
    double *depth = (double *)malloc(n * 8);
    uint8_t *sky = (uint8_t *)malloc((size_t)g->pw * (sky_rows ? sky_rows : 1) * 3);
    uint64_t h = 0xCBF29CE484222325ULL;
    int x, y, r;
    for (y = 0; y < sky_rows; y++)                   /* make_cases.sky_rows */
        for (x = 0; x < g->pw; x++) {
            uint8_t *p = sky + ((size_t)y * g->pw + x) * 3;
            p[0] = (uint8_t)((x * 5 + y) & 255);
            p[1] = (uint8_t)((y * 9) & 255);
            p[2] = (uint8_t)((x * 3 + 100) & 255);
        }
    made_n = asked = 0;
    r = clio_land_draw(g, made_chunk, NULL, sky, sky_rows, rgb, depth);
    if (r != 0) {
        printf("FAIL %s: %d\n", what, r);
        bad++;
    } else {
        for (i = 0; i < n; i++)
            h = fnv32(h, rgb[i]);
        for (i = 0; i < n; i++)
            h = fnv_double(h, depth[i]);
        if (h != want) {
            printf("FAIL %s: the picture differs from CLIo's\n", what);
            bad++;
        }
        if (asked != made_n) {
            printf("FAIL %s: a chunk asked for %d times over %d chunks\n", what, asked, made_n);
            bad++;
        }
    }
    if (clio_land_draw(g, no_chunk, NULL, sky, sky_rows, rgb, depth) != -2) {
        printf("FAIL %s: no chunk is not stopped at\n", what);
        bad++;
    }
    free(rgb);
    free(depth);
    free(sky);
}

/* ---- the made-up room of make_cases.py --------------------------------------------------------------- */
#define CLR CLIO_CLEAR
static const uint32_t TILE_A[6] = {0xC82828u, CLR, 0x28C828u, CLR, 0x2828C8u, 0x5A5A5Au};
static const uint32_t TILE_B[4] = {0x0A0A0Au, 0xFAFA00u, 0x00FAFAu, CLR};

static int room_cell(void *user, int what, int64_t ix, int64_t iy, clio_room_cell *out)
{
    int64_t k, m;
    (void)user;
    m = pmod(ix * 3 + iy, 19);
    if (pmod(ix * 5 + iy * 3, 31) == 0) {               /* no cell: the plain floor, the plain ceiling */
        if (what == 0)
            out->mark = m == 1 ? 1 : m == 2 ? 2 : 0;
        return 0;
    }
    if (what == 1) {
        if (pmod(ix * iy, 17) == 5)
            out->kind = CLIO_CEILING_SKY;
        else if (pmod(ix + iy, 7) == 0) {
            out->tex = TILE_B;
            out->tex_w = out->tex_h = 2;
        }
        return 0;
    }
    k = pmod(ix * 7 + iy * 11, 23);
    out->kind = k == 0 ? ((ix & 1) ? CLIO_FLOOR_PLATE_DOWN : CLIO_FLOOR_PLATE) : k == 1 ? CLIO_FLOOR_SUNK :
                k == 2 ? CLIO_FLOOR_TELEPORT : k == 3 ? CLIO_FLOOR_LIFT : k == 4 ? CLIO_FLOOR_BELT :
                k == 5 ? CLIO_FLOOR_OIL : k == 6 ? CLIO_FLOOR_SLICK : k == 7 ? CLIO_FLOOR_SCORCHED : CLIO_FLOOR_PLAIN;
    out->belt = (int32_t)pmod(ix + iy, 4);
    out->toxic = pmod(ix + 2 * iy, 13) == 0;
    out->fire = pmod(ix + iy * 5, 11) == 0;
    out->mark = m == 1 ? 1 : m == 2 ? 2 : 0;
    if (pmod(ix - iy, 9) == 0) {
        out->tex = TILE_A;
        out->tex_w = 3;
        out->tex_h = 2;
    }
    return 0;
}

static int room_stop(void *user, int what, int64_t ix, int64_t iy, clio_room_cell *out)
{
    (void)user;
    (void)what;
    (void)ix;
    (void)iy;
    (void)out;
    return 1;
}

static void room_case(const char *what, const clio_room *r, uint64_t want, int later, int first)
{
    size_t n = (size_t)r->pw * r->ph, i;
    uint32_t *rgb = (uint32_t *)malloc(n * 4);
    int32_t *px = (int32_t *)malloc(n * 4);
    char *mine = (char *)calloc(n, 1);
    int got = -1, k, res;
    uint64_t h = 0xCBF29CE484222325ULL;
    res = clio_room_draw(r, room_cell, NULL, rgb, px, &got);
    if (res != 0) {
        printf("FAIL %s: %d\n", what, res);
        bad++;
    } else {
        for (k = 0; k < got; k++) {                 /* each left pixel, and its twin at low detail */
            mine[px[k]] = 1;
            if (r->step == 2 && (px[k] % r->pw) + 1 < r->pw)
                mine[px[k] + 1] = 1;
        }
        for (k = 0, i = 0; i < n; i++)
            k += mine[i];
        if (k != later || (got && px[0] != first)) {
            printf("FAIL %s: %d pixels left for teleporters, CLIo has %d\n", what, k, later);
            bad++;
        }
        for (i = 0; i < n; i++)
            if (!mine[i])
                h = fnv32(h, rgb[i]);
        if (h != want) {
            printf("FAIL %s: the floor and ceiling differ from CLIo's\n", what);
            bad++;
        }
    }
    if (clio_room_draw(r, room_stop, NULL, rgb, px, &got) != -2) {
        printf("FAIL %s: a stop is not stopped at\n", what);
        bad++;
    }
    free(rgb);
    free(px);
    free(mine);
}

/* ---- the frame (0.4): CLIo's calls replayed on a made-up picture, against its Python ---------------- */
static void frame_case(const char *what, int pw, int ph, int sx, int sy, int land, void (*calls)(clio_frame *),
                       uint64_t want)
{
    size_t n = (size_t)pw * ph, i;
    int x, y, W = pw / sx, VR = ph / sy;
    clio_frame f;
    uint64_t h = 0xCBF29CE484222325ULL;
    f.pw = pw;
    f.ph = ph;
    f.sx = sx;
    f.sy = sy;
    f.rgb = (uint32_t *)malloc(n * 4);
    f.depth = land ? (double *)malloc(n * 8) : NULL;
    f.cur_z = -1e9;
    f.cover = (double *)malloc((size_t)W * VR * 8);
    f.zbuf = (double *)calloc(n, 8);
    f.zgen = (uint32_t *)calloc(n, 4);
    f.gen = 0;
    for (y = 0; y < ph; y++)                                /* make_cases.frame_pixel, frame_depth */
        for (x = 0; x < pw; x++) {
            f.rgb[(size_t)y * pw + x] = ((uint32_t)((x * 29 + y * 7) & 255) << 16) |
                                        ((uint32_t)((x * 3 + y * 41) & 255) << 8) | (uint32_t)((x * x + y) & 255);
            if (land)
                f.depth[(size_t)y * pw + x] = 0.5 + ((x * 7 + y * 3) % 11) * 0.4;
        }
    for (i = 0; i < (size_t)W * VR; i++)
        f.cover[i] = 99.0;
    calls(&f);
    for (i = 0; i < n; i++)
        h = fnv32(h, f.rgb[i]);
    for (i = 0; i < (size_t)W * VR; i++)
        h = fnv_double(h, f.cover[i]);
    if (h != want) {
        printf("FAIL %s: the picture differs from CLIo's\n", what);
        bad++;
    }
    free(f.rgb);
    free(f.depth);
    free(f.cover);
    free(f.zbuf);
    free(f.zgen);
}

/* The vector display: the calls of tests/scope_cases.h leave what CLIo's Python leaves - each dot's light, the
 * faces' depths, each character's brightest thing, its ink and the way lines run through it - and the characters
 * read from it are the Python's, with and without a glow and a background. */
static void scope_case(void)
{
    enum { N = SCOPE_COLS * 2 * SCOPE_ROWS * 4, CELLS = SCOPE_COLS * SCOPE_ROWS };
    static double light[N], depth[N], best[CELLS];
    static int32_t ink[CELLS], way[CELLS];
    static uint8_t cells[CELLS * 8];
    clio_scope s = {SCOPE_COLS, SCOPE_ROWS, light, NULL, best, ink, way, 1, 2.0, 9.0, 0.25};
    int frame, i, k;
    for (i = 0; i < CELLS; i++)
        way[i] = -1;
    for (frame = 0; frame < SCOPE_FRAMES; frame++) {
        uint64_t h = 0xCBF29CE484222325ULL;
        memset(depth, 0, sizeof depth);
        if (scope_steps(&s, depth, frame) != 0) {
            printf("FAIL the vector display, frame %d: a call was refused\n", frame);
            bad++;
            continue;
        }
        for (i = 0; i < N; i++)
            h = fnv_double(h, light[i]);
        if (s.depth != NULL)
            for (i = 0; i < N; i++)
                h = fnv_double(h, depth[i]);
        for (i = 0; i < CELLS; i++)
            h = fnv_double(h, best[i]);
        for (i = 0; i < CELLS; i++)
            h = fnv32(h, (uint32_t)ink[i]);
        for (i = 0; i < CELLS; i++)
            h = fnv32(h, (uint32_t)way[i]);
        if (h != SCOPE_SUM[frame]) {
            printf("FAIL the vector display, frame %d: its dots differ from CLIo's\n", frame);
            bad++;
        }
        for (k = 0; k < 4; k++) {
            uint64_t hc = 0xCBF29CE484222325ULL;
            if (clio_scope_cells(&s, SCOPE_COLOURS, (int)(sizeof SCOPE_COLOURS / sizeof SCOPE_COLOURS[0]), k % 2 == 0,
                                 k >= 2, SCOPE_PAPER, cells) != 0) {
                printf("FAIL the vector display, frame %d: no characters\n", frame);
                bad++;
                continue;
            }
            hc = fnv(hc, cells, sizeof cells);
            if (hc != SCOPE_CELLS[frame][k]) {
                printf("FAIL the vector display, frame %d: its characters differ from CLIo's (%s, %s)\n", frame,
                       k % 2 == 0 ? "glow" : "no glow", k >= 2 ? "a background" : "no background");
                bad++;
            }
        }
    }
    {
        static const double tri[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
        static const int32_t far_edge[2] = {0, 7}, corners[3] = {0, 1, 2}, two[1] = {2}, three[1] = {3};
        clio_scope none = {0, 0, light, depth, best, ink, way, 0, 0.0, 1.0, 0.3};
        s.depth = depth;
        if (clio_scope_wire(&s, &EYE0, tri, 3, far_edge, 1, NULL, 1.0, 0) != -1
            || clio_scope_hide(&s, &EYE0, tri, 3, corners, two, 1, 0.996) != -1
            || clio_scope_cells(&none, SCOPE_COLOURS, 1, 1, 0, 0, cells) != -1) {
            printf("FAIL the vector display: a line to no point, a face of two corners or no size is not refused\n");
            bad++;
        }
        s.depth = NULL;
        if (clio_scope_hide(&s, &EYE0, tri, 3, corners, three, 1, 0.996) != -1) {
            printf("FAIL the vector display: faces without a depth buffer are not refused\n");
            bad++;
        }
    }
}

/* clio_dots32 on 0xRRGGBB pixels is clio_dots on their bytes */
static void dots32_case(void)
{
    uint32_t px32[48];
    uint8_t a[48], b[48];
    int i, fill;
    clio_dots_style st = {0, {0}, 215.0, 1.9, 9.0};
    memcpy(st.steps, STEPS, sizeof STEPS);
    for (i = 0; i < 48; i++)
        px32[i] = ((uint32_t)PX[i * 3] << 16) | ((uint32_t)PX[i * 3 + 1] << 8) | PX[i * 3 + 2];
    for (fill = 0; fill < 2; fill++) {
        st.fill = fill;
        clio_dots(PX, 3, 2, &st, a);
        clio_dots32(px32, 3, 2, &st, b);
        if (memcmp(a, b, 48) != 0) {
            printf("FAIL dots32: differs from dots\n");
            bad++;
        }
    }
}

static void sky_case(const char *what, const clio_sky *s, uint64_t want, uint64_t want_ridges, int spots, int first)
{
    size_t n = (size_t)s->pw * s->yh, i;
    uint32_t *rgb = (uint32_t *)malloc(n * 4);
    double *hs = (double *)malloc((size_t)(s->ridges ? s->ridges : 1) * s->pw * 8);
    int32_t *px = (int32_t *)malloc(n * 4);
    char *sunny = (char *)calloc(n, 1);
    int got = -1, k;
    uint64_t h = 0xCBF29CE484222325ULL, hr = 0xCBF29CE484222325ULL;
    if (clio_sky_draw(s, rgb, hs, px, &got) != 0) {
        printf("FAIL %s: no sky\n", what);
        bad++;
    } else {
        if (got != spots || (spots && px[0] != first)) {
            printf("FAIL %s: %d pixels left for the sun, CLIo leaves %d\n", what, got, spots);
            bad++;
        }
        for (k = 0; k < got; k++)
            sunny[px[k]] = 1;
        for (i = 0; i < n; i++)
            if (!sunny[i])
                h = fnv32(h, rgb[i]);
        for (i = 0; i < (size_t)s->ridges * s->pw; i++)
            hr = fnv_double(hr, hs[i]);
        if (h != want) {
            printf("FAIL %s: the sky differs from CLIo's\n", what);
            bad++;
        }
        if (hr != want_ridges) {
            printf("FAIL %s: the ridges differ from CLIo's\n", what);
            bad++;
        }
    }
    free(rgb);
    free(hs);
    free(px);
    free(sunny);
}

static void cells(const char *what, const uint8_t *got, const uint8_t *want, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (got[i] != want[i]) {
            printf("FAIL %s: cell %d byte %d is %d, CLIo has %d\n", what, i / 8, i % 8, got[i], want[i]);
            bad++;
            return;
        }
}

static void same(const char *what, double got, double want)
{
    if (memcmp(&got, &want, sizeof got) != 0) {
        printf("FAIL %s: %.17g, Python has %.17g\n", what, got, want);
        bad++;
    }
}

/* CLIo's mesh view (0.7): the faces into a frame with a depth; its pixels, distances, cover and the pixels left */
static void mesh_case(const char *what, const clio_rooms *r, const double *slopes, int sx, int sy, int want_later,
                      uint64_t want)
{
    size_t n = (size_t)r->pw * r->ph, i;
    int W = r->pw / sx, VR = r->ph / sy, nl = -1, k;
    clio_frame f;
    int32_t *later = (int32_t *)malloc(n * 2 * 4);
    uint64_t h = 0xCBF29CE484222325ULL;
    f.pw = r->pw;
    f.ph = r->ph;
    f.sx = sx;
    f.sy = sy;
    f.rgb = (uint32_t *)malloc(n * 4);
    f.depth = (double *)malloc(n * 8);
    f.cur_z = -1e9;
    f.cover = (double *)malloc((size_t)W * VR * 8);
    f.zbuf = (double *)calloc(n, 8);
    f.zgen = (uint32_t *)calloc(n, 4);
    f.gen = 0;
    for (i = 0; i < (size_t)W * VR; i++)
        f.cover[i] = 99.0;
    if ((slopes ? clio_frame_rooms2(&f, r, slopes, later, &nl) : clio_frame_rooms(&f, r, later, &nl)) != 0) {
        printf("FAIL %s: not drawn\n", what);
        bad++;
    } else {
        for (i = 0; i < n; i++) {
            uint32_t c = f.rgb[i];
            uint8_t b4[4] = {(uint8_t)c, (uint8_t)(c >> 8), (uint8_t)(c >> 16), (uint8_t)(c >> 24)};
            h = fnv(h, b4, 4);
        }
        h = fnv(h, (const uint8_t *)f.depth, n * 8);
        h = fnv(h, (const uint8_t *)f.cover, (size_t)W * VR * 8);
        for (k = 0; k < nl * 2; k++) {
            int32_t v = later[k];
            uint8_t b4[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
            h = fnv(h, b4, 4);
        }
        if (nl != want_later || h != want) {
            printf("FAIL %s: %d pixels left (%d), sum %016llX (%016llX)\n", what, nl, want_later, (unsigned long long)h,
                   (unsigned long long)want);
            bad++;
        }
    }
    free(later);
    free(f.rgb);
    free(f.depth);
    free(f.cover);
    free(f.zbuf);
    free(f.zgen);
}

/* CLIo's engine/surface.py (0.8): a picture and its G-buffer lit; a sum of its pixels after */
static void surface_case(const char *what, const clio_surface *s, const uint32_t *rgb, const double *depth,
                         const uint8_t *kinds, const uint8_t *mats, const uint8_t *own, uint64_t want)
{
    size_t n = (size_t)s->pw * s->ph, i;
    clio_frame f = {0};
    clio_more m = {0};
    uint64_t h = 0xCBF29CE484222325ULL;
    f.pw = s->pw;
    f.ph = s->ph;
    f.sx = 1;
    f.sy = 2;
    f.rgb = (uint32_t *)malloc(n * 4);
    f.depth = (double *)malloc(n * 8);
    m.kinds = (uint8_t *)malloc(n);
    m.mats = (uint8_t *)malloc(n);
    m.own = (uint8_t *)malloc(n);
    memcpy(f.rgb, rgb, n * 4);
    memcpy(f.depth, depth, n * 8);
    memcpy(m.kinds, kinds, n);
    memcpy(m.mats, mats, n);
    memcpy(m.own, own, n);
    if (clio_frame_surface(&f, s) != -1) {                  /* nothing handed over beside the frame: refused */
        printf("FAIL %s: lit without a G-buffer\n", what);
        bad++;
    }
    clio_frame_more(&f, &m);
    if (clio_frame_surface(&f, s) != 0) {
        printf("FAIL %s: not lit\n", what);
        bad++;
    } else {
        for (i = 0; i < n; i++) {
            uint32_t c = f.rgb[i];
            uint8_t b4[4] = {(uint8_t)c, (uint8_t)(c >> 8), (uint8_t)(c >> 16), (uint8_t)(c >> 24)};
            h = fnv(h, b4, 4);
        }
        if (h != want) {
            printf("FAIL %s: sum %016llX (%016llX)\n", what, (unsigned long long)h, (unsigned long long)want);
            bad++;
        }
    }
    clio_frame_more(&f, NULL);
    free(f.rgb);
    free(f.depth);
    free(m.kinds);
    free(m.mats);
    free(m.own);
}

static void same_ggx(int k)
{
    char what[32];
    snprintf(what, sizeof what, "ggx %d", k);
    same(what, clio_ggx(GGX[k], GGX[k] + 3, GGX[k] + 6, GGX[k][9], GGX[k][10]), GGX[k][11]);
}

static uint32_t tilt_pixel(int x, int y)
{
    return ((uint32_t)((x * 37 + y * 11) & 255) << 16) | ((uint32_t)((x * 5 + y * 71) & 255) << 8) |
           (uint32_t)((x * y * 3 + 7) & 255);
}

/* firstperson.tilt (0.8), and the picture turned over */
static void turn_cases(void)
{
    uint32_t src[TILT_W * TILT_H], out[TILT_W * TILT_H];
    int x, y, k;
    for (y = 0; y < TILT_H; y++)
        for (x = 0; x < TILT_W; x++)
            src[y * TILT_W + x] = tilt_pixel(x, y);
    for (k = 0; k < (int)(sizeof TILT / sizeof TILT[0]); k++) {
        uint64_t h = 0xCBF29CE484222325ULL;
        if (clio_tilt(src, TILT_W, TILT_H, TILT[k][1], TILT[k][2], 0x0A0A0Eu, out) != 0) {
            printf("FAIL tilt %g: not turned\n", TILT[k][0]);
            bad++;
            continue;
        }
        for (y = 0; y < TILT_H; y += 2)                    /* as the Python sums cells: top then bottom of each */
            for (x = 0; x < TILT_W; x++) {
                uint32_t c[2] = {out[y * TILT_W + x], out[(y + 1) * TILT_W + x]};
                int j;
                for (j = 0; j < 2; j++) {
                    uint8_t b4[4] = {(uint8_t)c[j], (uint8_t)(c[j] >> 8), (uint8_t)(c[j] >> 16), (uint8_t)(c[j] >> 24)};
                    h = fnv(h, b4, 4);
                }
            }
        if (h != TILT_SUM[k]) {
            printf("FAIL tilt %g: sum %016llX (%016llX)\n", TILT[k][0], (unsigned long long)h,
                   (unsigned long long)TILT_SUM[k]);
            bad++;
        }
    }
    {
        uint32_t rgb[6] = {1, 2, 3, 4, 5, 6};
        double depth[6] = {1, 2, 3, 4, 5, 6}, cover[6] = {10, 20, 30, 40, 50, 60};
        clio_frame f = {0};
        f.pw = 3;
        f.ph = 2;
        f.sx = 1;
        f.sy = 1;
        f.rgb = rgb;
        f.depth = depth;
        f.cover = cover;
        clio_frame_flip(&f);
        if (rgb[0] != 6 || rgb[2] != 4 || rgb[3] != 3 || rgb[5] != 1 || depth[1] != 5 || cover[0] != 60 || cover[4] != 20) {
            printf("FAIL the picture turned over\n");
            bad++;
        }
    }
}

int main(void)
{
    double out[6];
    clio_land L = {4207, 30.0, 0.6, 0.5, 0.25};
    same("hash 0 0 0", clio_hash(0, 0, 0), 0.0);
    same("hash -3 7 12345", clio_hash(-3, 7, 12345), 0.5614557106889448);
    same("hash 2^20 -2^20 4207", clio_hash(1 << 20, -(1 << 20), 4207), 0.8097810330357824);
    same("hash 17 5 3e9", clio_hash(17, 5, 3000000000LL), 0.8747386892500191);
    same("value 0 0 1", clio_value(0.0, 0.0, 1), 0.212314030670634);
    same("fbm 0 0 1", clio_fbm(0.0, 0.0, 1, 5, 0.55), 0.32542451105170667);
    same("value 12.75 -3.5", clio_value(12.75, -3.5, 4207), 0.48909695201037606);
    same("fbm 12.75 -3.5", clio_fbm(12.75, -3.5, 4207, 5, 0.55), 0.5579639954563018);
    same("value far", clio_value(1048576.3, 1048575.9, 99), 0.6859796014532094);
    same("fbm far", clio_fbm(1048576.3, 1048575.9, 99, 5, 0.55), 0.5748358867528137);
    same("value -0.001 0.999", clio_value(-0.001, 0.999, 7), 0.29649874474606475);
    same("fbm -0.001 0.999", clio_fbm(-0.001, 0.999, 7, 5, 0.55), 0.41542260466394015);
    if (clio_land_raw(&L, 10, 20, 3, 2, out) != 0) {
        printf("FAIL land: no heights\n");
        bad++;
    }
    same("land 10 20", out[0], LAND_10_20);
    same("land 12 21", out[5], LAND_12_21);
    if (clio_land_raw(&L, 0, 0, 0, 1, out) != -1) {
        printf("FAIL land: an empty block is not refused\n");
        bad++;
    }
    {
        clio_dots_style dots = {0, {0}, 215.0, 1.9, 9.0};
        uint8_t got[48];
        memcpy(dots.steps, STEPS, sizeof STEPS);
        if (clio_dots(PX, 3, 2, &dots, got) != 0) {
            printf("FAIL dots: no cells\n");
            bad++;
        }
        cells("dots", got, WANT_DOTS, 48);
        dots.fill = 1;
        clio_dots(PX, 3, 2, &dots, got);
        cells("dots-fill", got, WANT_FILL, 48);
        if (clio_dots(PX, 0, 2, &dots, got) != -1) {
            printf("FAIL dots: no columns is not refused\n");
            bad++;
        }
    }
    land_case("land ahead", &LAND0, LAND0_SKY, LAND0_SUM);
    land_case("land sideways, low detail", &LAND1, LAND1_SKY, LAND1_SUM);
    frame_case("a room's frame", FRAME0_PW, FRAME0_PH, FRAME0_SX, FRAME0_SY, FRAME0_LAND, FRAME0_CALLS, FRAME0_SUM);
    frame_case("an open land's frame", FRAME1_PW, FRAME1_PH, FRAME1_SX, FRAME1_SY, FRAME1_LAND, FRAME1_CALLS,
               FRAME1_SUM);
    frame_case("colour changes, hurt and shaking", FRAME2_PW, FRAME2_PH, FRAME2_SX, FRAME2_SY, FRAME2_LAND,
               FRAME2_CALLS, FRAME2_SUM);
    clio_maths(pow, exp);
    {
        int same = 1, k;
        for (k = 0; k < 5; k++)
            same = same && pow(PROBE_POW[k][0], PROBE_POW[k][1]) == PROBE_POW[k][2];
        for (k = 0; k < 2; k++)
            same = same && exp(PROBE_EXP[k][0]) == PROBE_EXP[k][1];
        if (same) {
            frame_case("models in a room", FRAME3_PW, FRAME3_PH, FRAME3_SX, FRAME3_SY, FRAME3_LAND, FRAME3_CALLS,
                       FRAME3_SUM);
            frame_case("models in open land", FRAME4_PW, FRAME4_PH, FRAME4_SX, FRAME4_SY, FRAME4_LAND,
                       FRAME4_CALLS, FRAME4_SUM);
            frame_case("models kept as meshes, in a room", FRAME5_PW, FRAME5_PH, FRAME5_SX, FRAME5_SY, FRAME5_LAND,
                       FRAME5_CALLS, FRAME5_SUM);
            frame_case("models kept as meshes, in open land", FRAME6_PW, FRAME6_PH, FRAME6_SX, FRAME6_SY,
                       FRAME6_LAND, FRAME6_CALLS, FRAME6_SUM);
            if (frame_model_failed) {
                printf("FAIL models: %d not drawn\n", frame_model_failed);
                bad++;
            }
        } else
            printf("(models not compared: this machine's pow or exp differs from the one the cases were made with)\n");
    }
    {
        int same = 1, k;
        for (k = 0; k < 3; k++)
            same = same && pow(PROBE_POW75[k][0], PROBE_POW75[k][1]) == PROBE_POW75[k][2];
        if (same) {
            mesh_case("the mesh view, half blocks", &MESH0, MESH0_SLOPES, MESH0_SX, MESH0_SY, MESH0_LATER, MESH0_SUM);
            mesh_case("the mesh view, braille, a teleporter's rings left", &MESH1, MESH1_SLOPES, MESH1_SX, MESH1_SY,
                      MESH1_LATER, MESH1_SUM);
            mesh_case("the mesh view, faces clipped near the camera", &MESH2, MESH2_SLOPES, MESH2_SX, MESH2_SY,
                      MESH2_LATER, MESH2_SUM);
            mesh_case("slopes: a ramp up to a gallery, a tube with sloped sides", &MESH3, MESH3_SLOPES, MESH3_SX,
                      MESH3_SY, MESH3_LATER, MESH3_SUM);
            mesh_case("slopes: the ramp's sides from beside it", &MESH4, MESH4_SLOPES, MESH4_SX, MESH4_SY, MESH4_LATER,
                      MESH4_SUM);
        } else
            printf("(the mesh view not compared: this machine's pow differs from the one the cases were made with)\n");
    }
    {
        clio_frame f = {0};
        clio_rooms r = MESH0;
        int32_t later[2];
        int nl = 0;
        if (clio_frame_rooms(&f, &r, later, &nl) != -1) {   /* a frame without pixels */
            printf("FAIL the mesh view: a frame without pixels is not refused\n");
            bad++;
        }
    }
    {
        int same = 1, k;
        for (k = 0; k < 5; k++)
            same = same && pow(PROBE_POW[k][0], PROBE_POW[k][1]) == PROBE_POW[k][2];
        for (k = 0; k < 2; k++)
            same = same && exp(PROBE_EXP[k][0]) == PROBE_EXP[k][1];
        if (same) {
            for (k = 0; k < (int)(sizeof GGX / sizeof GGX[0]); k++)
                same_ggx(k);
            surface_case("surfaces: lamps, a model, chrome, glowing windows, a polished floor", &SURF0, SURF0_RGB,
                         SURF0_DEPTH, SURF0_KINDS, SURF0_MATS, SURF0_OWN, SURF0_SUM);
            surface_case("surfaces: a yard in the rain at night, wet, splashing", &SURF1, SURF1_RGB, SURF1_DEPTH,
                         SURF1_KINDS, SURF1_MATS, SURF1_OWN, SURF1_SUM);
        } else
            printf("(surfaces not compared: this machine's pow or exp differs from the one the cases were made with)\n");
    }
    turn_cases();
    dots32_case();
    scope_case();
    room_case("room ahead", &ROOM0, ROOM0_SUM, ROOM0_LATER, ROOM0_FIRST_LATER);
    room_case("room sideways, low detail", &ROOM1, ROOM1_SUM, ROOM1_LATER, ROOM1_FIRST_LATER);
    sky_case("sky with stars, clouds, ridges", &SKY0, SKY0_SUM, SKY0_RIDGES_SUM, SKY0_SPOTS, SKY0_FIRST_SPOT);
    sky_case("sky with a banded planet", &SKY1, SKY1_SUM, SKY1_RIDGES_SUM, SKY1_SPOTS, SKY1_FIRST_SPOT);
    if (strcmp(clio_picture_version(), CLIO_PICTURE_VERSION) != 0) {
        printf("FAIL version\n");
        bad++;
    }
    printf(bad ? "%d failed\n" : "all as CLIo's Python has them\n", bad);
    return bad ? 1 : 0;
}
