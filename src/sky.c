/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2024-2026 Daniel Sandner
 *
 * The sky over open cells, as CLIo's engine/firstperson.py sky_picture paints it: the gradient (each row's
 * colour from the caller, who works out its power), stars, clouds that drift, and the ridges. The sun's
 * disc and glow need a hypotenuse that Python works out its own way, so the pixels it may reach are handed
 * back for the caller to finish. */
#define CLIO_PICTURE_BUILD
#include "clio_picture.h"

#include <math.h>
#include <stdlib.h>

#define R(c) ((int)(((c) >> 16) & 255u))
#define G(c) ((int)(((c) >> 8) & 255u))
#define B(c) ((int)((c) & 255u))
#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

/* firstperson.noise: as clio_hash, over 65536 */
static double noise(int64_t i, int64_t j, int64_t k)
{
    uint64_t h = ((uint64_t)i * 374761393u + (uint64_t)j * 668265263u + (uint64_t)k * 2246822519u) & 0xFFFFFFFFu;
    h = ((h ^ (h >> 13)) * 1274126177u) & 0xFFFFFFFFu;
    return (double)((h ^ (h >> 16)) & 0xFFFFu) / 65536.0;
}

/* Python's float % 1.0 */
static double mod1(double t)
{
    double m = fmod(t, 1.0);
    if (m != 0.0) {
        if (m < 0)
            m += 1.0;
    } else
        m = 0.0;
    return m;
}

/* firstperson._wave: smooth noise round a circle, t in turns, n bumps a turn */
static double wave(double t, int64_t n, int64_t seed)
{
    double f = mod1(t) * n, k, a, b;
    int64_t i = (int64_t)f;
    k = f - i;
    k = k * k * (3 - 2 * k);
    a = noise(i % n, seed, 17);
    b = noise((i + 1) % n, seed, 17);
    return a + (b - a) * k;
}

static int part(int a, int b, double k) { return (int)(a + (b - a) * k); }

static uint32_t mix(uint32_t a, uint32_t b, double k)
{
    return RGB(part(R(a), R(b), k), part(G(a), G(b), k), part(B(a), B(b), k));
}

int clio_sky_draw(const clio_sky *s, uint32_t *rgb, double *ridge_rows, int32_t *sun_px, int *sun_n)
{
    int PW, yh, x, y, j, n = 0;
    double *hs = NULL, reach = 0.0, reach2 = 0.0;
    if (!s || !rgb || s->pw <= 0 || s->yh < 0 || (s->yh && !s->base) || s->ridges < 0 ||
        (s->ridges && (!s->ridge_rgb || !s->ridge_height || !s->ridge_near || !s->ridge_rough || !s->ridge_seed)) ||
        (s->sun && (!sun_px || !sun_n)))
        return -1;
    PW = s->pw;
    yh = s->yh;
    if (s->ridges) {
        hs = (double *)malloc(sizeof(double) * (size_t)s->ridges * PW);
        if (!hs)
            return -1;
    }
    for (j = 0; j < s->ridges; j++) {
        double shift = -s->lateral * s->ridge_near[j] * 0.006;
        int64_t seed = s->ridge_seed[j];
        for (x = 0; x < PW; x++) {
            double u = (s->f + (x + 0.5) / PW - 0.5) / 4.0 + shift;
            double h = 0.55 * wave(u, 9, seed) + 0.3 * wave(u, 23, seed + 1) +
                       0.15 * s->ridge_rough[j] * wave(u, 61, seed + 2);
            hs[(size_t)j * PW + x] = h * s->ridge_height[j] * yh;
        }
    }
    if (ridge_rows && s->ridges)
        for (x = 0; x < s->ridges * PW; x++)
            ridge_rows[x] = hs[x];
    if (s->sun) {                                   /* a little past the glow's edge: the caller decides */
        double r = s->sun_size * yh;
        reach = (s->sun_glow != 0.0 ? r * 3.5 : r);
        reach = reach * 1.000001 + 1e-9;
        reach2 = reach > 0 ? reach * reach : -1.0;
    }
    for (y = 0; y < yh; y++) {
        double e = (yh - y - 0.5) / (double)yh;     /* 0 at the horizon, 1 at the top */
        uint32_t base = s->base[y];
        for (x = 0; x < PW; x++) {
            uint32_t c = base;
            double u = (s->f + (x + 0.5) / PW - 0.5) / 4.0;
            int sunny = 0;
            if (s->stars != 0.0 && e > 0.3) {
                int64_t si = (int64_t)(u * 4 * PW);
                if (noise(si, y, 41) < s->stars)
                    c = mix(c, RGB(230, 230, 255), 0.5 + 0.4 * noise(si, y, s->phase + 5));
            }
            if (s->clouds && 0.18 < e && e < 0.85) {
                double k = wave(u * 3 + s->phase * 0.004, 7, 31) * wave(u * 5 - s->phase * 0.003 + e * 0.7, 13, 37);
                double band = 1 - fabs(e - 0.45) * 2.2;
                if (k * band > 1 - s->cloud_amount) {
                    double m = (k * band - (1 - s->cloud_amount)) * 3;
                    c = mix(c, s->cloud_rgb, m < 0.8 ? m : 0.8);
                }
            }
            if (s->sun && reach2 > 0) {
                double du = (mod1(u - s->sun_at + 0.5) - 0.5) * 4 * PW;
                double dv = (e - s->sun_height) * yh;
                if (du * du + dv * dv <= reach2)
                    sunny = 1;
            }
            if (sunny) {
                sun_px[n++] = y * PW + x;
            } else {
                for (j = 0; j < s->ridges; j++)
                    if ((yh - y - 0.5) < hs[(size_t)j * PW + x])
                        c = s->ridge_rgb[j];
            }
            rgb[(size_t)y * PW + x] = c;
        }
    }
    if (sun_n)
        *sun_n = n;
    free(hs);
    return 0;
}
