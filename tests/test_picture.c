/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2024-2026 Daniel Sandner
 *
 * The library against values CLIo's Python gives (engine/land.py, engine/firstperson.py), compared to the
 * last bit. cases.h is written by make_cases.py. */
#include "clio_picture.h"

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

#include "cases.h"

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
    dots32_case();
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
