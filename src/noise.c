/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2024-2026 Daniel Sandner
 *
 * The land's noise, as CLIo's engine/land.py has it - the same operations in the same order, so the
 * same doubles come out. */
#define CLIO_PICTURE_BUILD
#include "clio_picture.h"

#include <math.h>

const char *clio_picture_version(void) { return CLIO_PICTURE_VERSION; }

double clio_hash(int64_t ix, int64_t iy, int64_t seed)
{
    /* Python works with whole numbers of any size and keeps the low 32 bits; unsigned 64-bit sums
     * wrap, and their low 32 bits are the same. */
    uint64_t n = ((uint64_t)ix * 374761393u + (uint64_t)iy * 668265263u + (uint64_t)seed * 2246822519u)
                 & 0xFFFFFFFFu;
    n = ((n ^ (n >> 13)) * 1274126177u) & 0xFFFFFFFFu;
    return (double)((n ^ (n >> 16)) & 0xFFFFu) / 65535.0;
}

double clio_value(double x, double y, int64_t seed)
{
    double fx0 = floor(x), fy0 = floor(y);
    int64_t ix = (int64_t)fx0, iy = (int64_t)fy0;
    double fx = x - fx0, fy = y - fy0;
    double ux = fx * fx * (3.0 - 2.0 * fx), uy = fy * fy * (3.0 - 2.0 * fy);
    double a = clio_hash(ix, iy, seed), b = clio_hash(ix + 1, iy, seed);
    double c = clio_hash(ix, iy + 1, seed), d = clio_hash(ix + 1, iy + 1, seed);
    return a + (b - a) * ux + (c - a) * uy + (a - b - c + d) * ux * uy;
}

double clio_fbm(double x, double y, int64_t seed, int octaves, double rough)
{
    double total = 0.0, amp = 1.0, norm = 0.0;
    int o;
    for (o = 0; o < octaves; o++) {
        total += amp * clio_value(x, y, seed + (int64_t)o * 1013);
        norm += amp;
        amp *= rough;
        x = x * 2.03 + 17.1;
        y = y * 2.03 - 9.7;
    }
    return total / norm;
}

static double raw(const clio_land *L, double x, double y)
{
    int64_t s = L->seed;
    double u = x / L->scale, v = y / L->scale, base;
    if (L->warp != 0.0) {
        double w = L->warp * 0.9;
        double nu = u + w * (clio_fbm(u * 0.5, v * 0.5, s + 31, 3, 0.5) - 0.5);
        double nv = v + w * (clio_fbm(u * 0.5 + 5.2, v * 0.5, s + 47, 3, 0.5) - 0.5);
        u = nu;
        v = nv;
    }
    base = clio_fbm(u, v, s, 5, L->rough);
    if (L->ridges != 0.0) {
        double r = 1.0 - fabs(2.0 * clio_fbm(u * 0.8 + 3.1, v * 0.8, s + 91, 4, 0.5) - 1.0);
        base = base * (1.0 - L->ridges) + (r * r) * L->ridges;
    }
    base = (base - 0.22) / 0.56;
    return base < 0.0 ? 0.0 : (base > 1.0 ? 1.0 : base);
}

int clio_land_raw(const clio_land *land, int64_t x0, int64_t y0, int w, int h, double *out)
{
    int i, j;
    if (w <= 0 || h <= 0 || !land || !out)
        return -1;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            out[j * w + i] = raw(land, (double)(x0 + i), (double)(y0 + j));
    return 0;
}
