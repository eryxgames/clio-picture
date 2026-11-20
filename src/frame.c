/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2024-2026 Daniel Sandner
 *
 * A frame kept here from the floor to the cells: CLIo's View draws walls, pictures and models into it by
 * these calls, each the same operations in the same order as its Python (engine/firstperson.py, model.py),
 * so the same pixels come out. What needs a hypotenuse, a power or a logarithm stays with the caller. */
#define CLIO_PICTURE_BUILD
#include "clio_picture.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define R(c) ((int)(((c) >> 16) & 255u))
#define G(c) ((int)(((c) >> 8) & 255u))
#define B(c) ((int)((c) & 255u))
#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define FAR_AWAY 1e9

static int part(int a, int b, double k) { return (int)(a + (b - a) * k); }
static uint32_t mix(uint32_t a, uint32_t b, double k)
{
    return RGB(part(R(a), R(b), k), part(G(a), G(b), k), part(B(a), B(b), k));
}
static int lit(int c, double f) { int v = (int)(c * f); return v > 255 ? 255 : v; }
static uint32_t shade(uint32_t c, double f) { return RGB(lit(R(c), f), lit(G(c), f), lit(B(c), f)); }

static double noise(int64_t i, int64_t j, int64_t k)
{
    uint64_t h = ((uint64_t)i * 374761393u + (uint64_t)j * 668265263u + (uint64_t)k * 2246822519u) & 0xFFFFFFFFu;
    h = ((h ^ (h >> 13)) * 1274126177u) & 0xFFFFFFFFu;
    return (double)((h ^ (h >> 16)) & 0xFFFFu) / 65536.0;
}

static int64_t floordiv(int64_t a, int64_t b) { int64_t q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }
static int64_t pymod(int64_t a, int64_t m) { int64_t r = a % m; return r < 0 ? r + m : r; }

/* int(x) of Python, kept within lo..hi first (the callers clamp to those anyway) */
static int64_t trunc_in(double x, int64_t lo, int64_t hi)
{
    if (x <= (double)lo)
        return lo;
    if (x >= (double)hi)
        return hi;
    return (int64_t)x;
}

/* min(n - 1, max(0, int(v))) */
static int index_in(double v, int n)
{
    if (v >= n)
        return n - 1;
    if (v <= 0)
        return 0;
    return (int)v < n - 1 ? (int)v : n - 1;
}

/* a pixel, as DepthRow writes it: where the land there is further off than what is drawn now */
static void put(clio_frame *f, size_t i, uint32_t c)
{
    if (!f->depth || f->depth[i] > f->cur_z)
        f->rgb[i] = c;
}

/* View._mark_cover */
static void mark_cover(clio_frame *f, int64_t x0, int64_t x1, int64_t y0, int64_t y1, double dist)
{
    int64_t W = f->pw / f->sx, yy, xx, ya, yb, xa, xb;
    if (!f->cover)
        return;
    ya = floordiv(y0 > 0 ? y0 : 0, f->sy);
    yb = floordiv((y1 < f->ph ? y1 : f->ph) - 1, f->sy) + 1;
    xa = floordiv(x0 > 0 ? x0 : 0, f->sx);
    xb = floordiv((x1 < f->pw ? x1 : f->pw) - 1, f->sx) + 1;
    for (yy = ya; yy < yb; yy++)
        for (xx = xa; xx < xb; xx++) {
            double *c = f->cover + yy * W + xx;
            if (dist < *c)
                *c = dist;
        }
}

void clio_frame_land_cover(clio_frame *f)
{
    int x, y;
    int64_t W;
    if (!f || !f->depth || !f->cover)
        return;
    W = f->pw / f->sx;
    for (y = 0; y < f->ph; y++) {
        int64_t cy = y / f->sy;
        for (x = 0; x < f->pw; x += f->sx * 2) {
            double d = f->depth[(size_t)y * f->pw + x];
            if (d < FAR_AWAY) {
                double *c = f->cover + cy * W + x / f->sx;
                if (d < *c)
                    *c = d;
            }
        }
    }
}

void clio_frame_front(clio_frame *f, const uint32_t *tex, int tw, int th, int X0, int X1, int Y0, int Y1, double x0,
                      double w, double y0, double h, double opened, double fog, double cover_dist)
{
    int x, y;
    int ncols = X1 - X0;
    int *us;
    if (!f || !tex || tw <= 0 || th <= 0 || X0 >= X1 || Y0 >= Y1)
        return;
    us = (int *)malloc(sizeof(int) * (size_t)ncols);
    if (!us)
        return;
    for (x = 0; x < ncols; x++)
        us[x] = index_in((X0 + x + 0.5 - x0) / w * tw, tw);
    for (y = Y0; y < Y1; y++) {
        double v = (y + 0.5 - y0) / h + opened;
        const uint32_t *trow = tex + (size_t)index_in(v * th, th) * tw;
        size_t base = (size_t)y * f->pw;
        for (x = X0; x < X0 + ncols; x++) {
            uint32_t c = trow[us[x - X0]];
            if (c != CLIO_CLEAR)
                put(f, base + x, shade(c, fog));
        }
    }
    free(us);
    mark_cover(f, X0, X1, Y0, Y1, cover_dist);
}

void clio_frame_side(clio_frame *f, const uint32_t *tex, int tw, int th, int n, const int32_t *xs, const double *zc,
                     const double *fog, double half, double span, double yh, double ch, double fs, double opened,
                     double z)
{
    int i, y;
    if (!f || !tex || tw <= 0 || th <= 0 || n <= 0 || !xs || !zc || !fog)
        return;
    for (i = 0; i < n; i++) {
        int x = xs[i], u;
        double t = (x + 0.5 - half) / span;
        double v = zc[i] - (z - 0.5);
        double y0 = yh - ch * t, y1 = yh + fs * t, hgt;
        int64_t Y0, Y1;
        if (v < 0)
            u = (int)pymod(trunc_in(v * tw, -((int64_t)1 << 40), 0), tw);
        else
            u = index_in(v * tw, tw);
        Y0 = trunc_in(ceil(y0 - 0.5), 0, (int64_t)1 << 40);
        Y1 = trunc_in(ceil(y1 - 0.5), -((int64_t)1 << 40), f->ph);
        if (opened != 0.0) {
            int64_t cut = trunc_in(y0 + (y1 - y0) * (1 - opened), -1, (int64_t)1 << 40);
            if (cut < Y1)
                Y1 = cut;
        }
        hgt = y1 - y0;
        f->cur_z = zc[i];
        for (y = (int)Y0; y < Y1; y++) {
            double vv = (y + 0.5 - y0) / hgt + opened;
            uint32_t c = tex[(size_t)index_in(vv * th, th) * tw + u];
            if (c != CLIO_CLEAR)
                put(f, (size_t)y * f->pw + x, shade(c, fog[i]));
        }
        mark_cover(f, x, x + 1, Y0, Y1, zc[i]);
    }
}

void clio_frame_blit(clio_frame *f, const uint32_t *pic, int nw, int nh, int x0, int y0, double s, int sw, int sh,
                     const clio_tone *tone, int mark, double dist)
{
    int i, j;
    int64_t W;
    if (!f || !pic || nw <= 0 || nh <= 0 || !(s > 0))
        return;
    W = f->pw / f->sx;
    for (j = 0; j < sh; j++) {
        int yy = y0 + j;
        const uint32_t *src;
        if (yy < 0 || yy >= f->ph)
            continue;
        src = pic + (size_t)index_in(j / s, nh) * nw;
        for (i = 0; i < sw; i++) {
            int xx = x0 + i;
            uint32_t c;
            if (xx < 0 || xx >= f->pw)
                continue;
            c = src[index_in(i / s, nw)];
            if (c == CLIO_CLEAR)
                continue;
            if (tone) {
                if (tone->gone != 0.0 && noise((int64_t)(i / tone->step), (int64_t)(j / tone->step), tone->id) < tone->gone)
                    continue;
                if (tone->flash)
                    c = RGB(255, 255, 255);
                else {
                    if (tone->tinted)
                        c = mix(c, tone->tint, 0.6);
                    if (tone->kind == 1)
                        c = shade(c, tone->fog);
                    else if (tone->kind == 2)
                        c = mix(c, tone->haze, tone->k);
                }
            }
            put(f, (size_t)yy * f->pw + xx, c);
            if (mark && f->cover) {
                double *cv = f->cover + (int64_t)(yy / f->sy) * W + xx / f->sx;
                if (dist < *cv)
                    *cv = dist;
            }
        }
    }
}

void clio_frame_darken(clio_frame *f, int x_from, int x_to, int y_from, int y_to, double k)
{
    int x, y;
    if (!f)
        return;
    for (y = y_from; y <= y_to; y++) {
        if (y < 0 || y >= f->ph)
            continue;
        for (x = x_from; x <= x_to; x++)
            if (x >= 0 && x < f->pw) {
                size_t i = (size_t)y * f->pw + x;
                put(f, i, shade(f->rgb[i], k));
            }
    }
}

void clio_frame_model(clio_frame *f)
{
    if (!f || !f->zgen)
        return;
    if (++f->gen == 0) {                                  /* round the clock: start the marks again */
        memset(f->zgen, 0, sizeof(uint32_t) * (size_t)f->pw * f->ph);
        f->gen = 1;
    }
}

void clio_frame_triangle(clio_frame *f, const double *pts, const double *cols, int smooth)
{
    int order[3] = {0, 1, 2}, a, b, y;
    double x0, y0, z0, x1, y1, z1, x2, y2, z2;
    const double *c0, *c1, *c2;
    double ys_d, ye_d;
    int64_t ys, ye;
    if (!f || !pts || !cols || !f->zbuf || !f->zgen)
        return;
    for (a = 1; a < 3; a++)                               /* sorted by y, keeping the order of equals */
        for (b = a; b > 0 && pts[order[b] * 3 + 1] < pts[order[b - 1] * 3 + 1]; b--) {
            int t = order[b];
            order[b] = order[b - 1];
            order[b - 1] = t;
        }
    x0 = pts[order[0] * 3]; y0 = pts[order[0] * 3 + 1]; z0 = pts[order[0] * 3 + 2];
    x1 = pts[order[1] * 3]; y1 = pts[order[1] * 3 + 1]; z1 = pts[order[1] * 3 + 2];
    x2 = pts[order[2] * 3]; y2 = pts[order[2] * 3 + 1]; z2 = pts[order[2] * 3 + 2];
    c0 = cols + (smooth ? order[0] * 3 : 0);
    c1 = cols + (smooth ? order[1] * 3 : 0);
    c2 = cols + (smooth ? order[2] * 3 : 0);
    if (y2 - y0 < 1e-6)
        return;
    ys_d = ceil(y0 - 0.5);
    ye_d = floor(y2 - 0.5);
    ys = ys_d < 0 ? 0 : (ys_d > f->ph ? f->ph : (int64_t)ys_d);
    ye = ye_d > f->ph - 1 ? f->ph - 1 : (ye_d < -1 ? -1 : (int64_t)ye_d);
    for (y = (int)ys; y <= ye; y++) {
        double yc = y + 0.5, k1, k2 = (yc - y0) / (y2 - y0);
        double xa = x0 + (x2 - x0) * k2, za = z0 + (z2 - z0) * k2, xb, zb, span, xs_d, xe_d;
        double ca[3] = {0, 0, 0}, cb[3] = {0, 0, 0};
        int64_t xs, xe, x;
        int i;
        if (yc < y1) {
            if (y1 - y0 < 1e-6)
                continue;
            k1 = (yc - y0) / (y1 - y0);
            xb = x0 + (x1 - x0) * k1;
            zb = z0 + (z1 - z0) * k1;
            if (smooth)
                for (i = 0; i < 3; i++) {
                    ca[i] = c0[i] + (c2[i] - c0[i]) * k2;
                    cb[i] = c0[i] + (c1[i] - c0[i]) * k1;
                }
        } else {
            if (y2 - y1 < 1e-6)
                continue;
            k1 = (yc - y1) / (y2 - y1);
            xb = x1 + (x2 - x1) * k1;
            zb = z1 + (z2 - z1) * k1;
            if (smooth)
                for (i = 0; i < 3; i++) {
                    ca[i] = c0[i] + (c2[i] - c0[i]) * k2;
                    cb[i] = c1[i] + (c2[i] - c1[i]) * k1;
                }
        }
        if (xa > xb) {
            double t = xa;
            xa = xb;
            xb = t;
            t = za;
            za = zb;
            zb = t;
            for (i = 0; i < 3; i++) {
                t = ca[i];
                ca[i] = cb[i];
                cb[i] = t;
            }
        }
        xs_d = ceil(xa - 0.5);
        xe_d = floor(xb - 0.5);
        xs = xs_d < 0 ? 0 : (xs_d > f->pw ? f->pw : (int64_t)xs_d);
        xe = xe_d > f->pw - 1 ? f->pw - 1 : (xe_d < -1 ? -1 : (int64_t)xe_d);
        if (xe < xs)
            continue;
        span = (xb - xa) != 0.0 ? (xb - xa) : 1.0;
        for (x = xs; x <= xe; x++) {
            double u = (x + 0.5 - xa) / span;
            double d = za + (zb - za) * u;
            size_t key = (size_t)y * f->pw + (size_t)x;
            double zhere = f->zgen[key] == f->gen ? f->zbuf[key] : 1e9;
            if (d < zhere) {
                uint32_t c;
                f->zbuf[key] = d;
                f->zgen[key] = f->gen;
                if (smooth)
                    c = RGB((int)(ca[0] + (cb[0] - ca[0]) * u), (int)(ca[1] + (cb[1] - ca[1]) * u),
                            (int)(ca[2] + (cb[2] - ca[2]) * u));
                else
                    c = RGB((int)c0[0], (int)c0[1], (int)c0[2]);
                put(f, key, c);
            }
        }
    }
}

static double lum(uint32_t c) { return 0.2126 * R(c) + 0.7152 * G(c) + 0.0722 * B(c); }

void clio_frame_recolour(clio_frame *f, int how, double k, uint32_t colour)
{
    size_t i, n;
    if (!f)
        return;
    n = (size_t)f->pw * f->ph;
    for (i = 0; i < n; i++) {
        uint32_t c = f->rgb[i];
        switch (how) {
        case 1:
            c = shade(c, k);
            break;
        case 2: {
            int g = 255 - (int)lum(c);
            c = mix(c, RGB(g, g, g), 0.85);
            break;
        }
        case 3:
            if (!(B(c) > G(c) + 40 && R(c) > G(c))) {
                double v = lum(c);
                int gg = (int)(v * 1.25) + 18;
                c = RGB((int)(v * 0.35), gg > 255 ? 255 : gg, (int)(v * 0.45));
            }
            break;
        case 4:
            c = mix(c, colour, k);
            break;
        default:
            break;
        }
        f->rgb[i] = c;
    }
}

void clio_frame_vignette(clio_frame *f, double hurt, int where)
{
    int x, y, PW, PH;
    if (!f)
        return;
    PW = f->pw;
    PH = f->ph;
    for (y = 0; y < PH; y++) {
        double ey = (y < PH - 1 - y ? y : PH - 1 - y) / (PH * 0.25);
        if (where == 3)
            ey = (PH - 1 - y) / (PH * 0.3);
        for (x = 0; x < PW; x++) {
            double e, m, k;
            size_t i = (size_t)y * PW + x;
            if (where == 1)
                e = x / (PW * 0.3);
            else if (where == 2)
                e = (PW - 1 - x) / (PW * 0.3);
            else if (where == 3)
                e = ey;
            else {
                double side = (x < PW - 1 - x ? x : PW - 1 - x) / (PW * 0.2);
                e = ey < side ? ey : side;
            }
            if (e > 1.0)
                e = 1.0;
            m = hurt * (0.08 + 0.75 * (1 - e));
            k = m < 1.0 ? m : 1.0;
            put(f, i, mix(f->rgb[i], RGB(190, 0, 0), k));
        }
    }
}

void clio_frame_shake(clio_frame *f, int s)
{
    int y, x;
    if (!f || s == 0 || s >= f->pw || -s >= f->pw)
        return;
    for (y = 0; y < f->ph; y++) {
        uint32_t *row = f->rgb + (size_t)y * f->pw;
        if (s > 0) {
            memmove(row + s, row, sizeof(uint32_t) * (size_t)(f->pw - s));
            for (x = 0; x < s; x++)
                row[x] = 0;
        } else {
            memmove(row, row - s, sizeof(uint32_t) * (size_t)(f->pw + s));
            for (x = f->pw + s; x < f->pw; x++)
                row[x] = 0;
        }
    }
}

static int imax(int a, int b) { return a > b ? a : b; }
static int imin(int a, int b) { return a < b ? a : b; }

void clio_frame_canopy(clio_frame *f, uint32_t colour)
{
    int PW, PH, bh, bw, x, y, j;
    uint32_t dark, lite;
    if (!f)
        return;
    PW = f->pw;
    PH = f->ph;
    dark = shade(colour, 0.55);
    lite = mix(colour, RGB(255, 255, 255), 0.25);
    bh = imax(2, PH / 14);
    bw = imax(2, PW / 40);
    for (y = 0; y < bh && y < PH; y++)
        for (x = 0; x < PW; x++)
            put(f, (size_t)y * PW + x, y == bh - 1 ? lite : (y ? colour : dark));
    for (y = bh; y < PH; y++) {
        double k = (y - bh) / (double)imax(1, PH - bh);
        int inset = (int)(PW * 0.16 * (1 - k));
        int x0s[2];
        x0s[0] = inset;
        x0s[1] = PW - inset - bw;
        for (j = 0; j < 2; j++) {
            int x0 = x0s[j];
            for (x = imax(0, x0); x < imin(PW, x0 + bw); x++)
                put(f, (size_t)y * PW + x, (x == x0 + bw - 1 || x == x0) ? lite : colour);
        }
        for (x = 0; x < imax(0, inset); x++) {
            size_t i = (size_t)y * PW + x;
            put(f, i, shade(f->rgb[i], 0.35));
        }
        for (x = imin(PW, PW - inset); x < PW; x++) {
            size_t i = (size_t)y * PW + x;
            put(f, i, shade(f->rgb[i], 0.35));
        }
    }
}

void clio_frame_dashboard(clio_frame *f, uint32_t colour, int h, int y0, int x0, int x1, int gx0, int gx1, int gy,
                          int lit, int low)
{
    int PW, PH, x, y, w, j;
    uint32_t dark, edge;
    int sxs[2];
    if (!f)
        return;
    PW = f->pw;
    PH = f->ph;
    dark = shade(colour, 0.4);
    edge = mix(colour, RGB(255, 255, 255), 0.2);
    for (y = y0; y < PH; y++)
        for (x = x0; x < x1; x++)
            if (y >= 0 && x >= 0 && x < PW)
                put(f, (size_t)y * PW + x, y > y0 ? dark : edge);
    w = x1 - x0;
    sxs[0] = x0 + w / 12;
    sxs[1] = x1 - w / 12 - w / 6;
    for (j = 0; j < 2; j++)
        for (y = y0 + 1; y < PH - 1; y++)
            for (x = sxs[j]; x < sxs[j] + w / 6; x++) {
                int on = (x + y * 3) % 5 < 3 && y == y0 + 1 + (x / 3) % imax(1, h - 2);
                if (y >= 0 && x >= 0 && x < PW)
                    put(f, (size_t)y * PW + x, on ? RGB(90, 230, 140) : RGB(10, 26, 18));
            }
    for (y = gy - imax(0, h / 4); y < gy + imax(1, h / 4); y++)
        if (y >= 0 && y < PH)
            for (x = gx0; x < gx1; x++)
                if (x >= 0 && x < PW)
                    put(f, (size_t)y * PW + x, x < lit ? (low ? RGB(230, 70, 40) : RGB(240, 170, 50)) : RGB(30, 24, 16));
}

void clio_frame_cage(clio_frame *f)
{
    int PW, PH, bw, bh, x, y;
    if (!f)
        return;
    PW = f->pw;
    PH = f->ph;
    bw = imax(1, PW / 70);
    bh = imax(1, PH / 26);
    for (y = 0; y < PH; y++) {
        uint32_t c = ((y / imax(2, bh * 2)) % 2) ? RGB(214, 168, 48) : RGB(34, 32, 30);
        for (x = 0; x < bw; x++)
            if (x < PW)
                put(f, (size_t)y * PW + x, c);
        for (x = PW - bw; x < PW; x++)
            if (x >= 0)
                put(f, (size_t)y * PW + x, c);
    }
    for (y = 0; y < bh && y < PH; y++)
        for (x = 0; x < PW; x++)
            put(f, (size_t)y * PW + x, ((x / imax(3, bw * 3)) % 2) ? RGB(214, 168, 48) : RGB(34, 32, 30));
}
