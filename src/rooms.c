/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Daniel Sandner
 *
 * Rooms as faces in a true perspective: CLIo's engine/mesh.py (Mesh.spans, Mesh.draw, Mesh.flat, Mesh.cover) with the
 * same operations in the same order, so that the same pixels and the same distances come out. */
#define CLIO_PICTURE_BUILD
#include "clio_picture.h"
#include "internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define R(c) ((int)(((c) >> 16) & 255u))
#define G(c) ((int)(((c) >> 8) & 255u))
#define B(c) ((int)((c) & 255u))
#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define NONE CLIO_CLEAR

#define FLOOR_A RGB(68, 61, 53)
#define FLOOR_B RGB(58, 53, 47)
#define FLOOR_LINE RGB(94, 86, 72)
#define CEIL RGB(30, 30, 36)
#define CEIL_LINE RGB(24, 24, 29)
#define LIGHT RGB(168, 162, 122)
#define TOXIC RGB(36, 140, 54)
#define TOXIC_HI RGB(130, 240, 110)
#define HAZE RGB(16, 16, 20)

static double noise(int64_t i, int64_t j, int64_t k)
{
    uint64_t h = ((uint64_t)i * 374761393u + (uint64_t)j * 668265263u + (uint64_t)k * 2246822519u) & 0xFFFFFFFFu;
    h = ((h ^ (h >> 13)) * 1274126177u) & 0xFFFFFFFFu;
    return (double)((h ^ (h >> 16)) & 0xFFFFu) / 65536.0;
}

static int64_t pymod(int64_t a, int64_t m) { int64_t r = a % m; return r < 0 ? r + m : r; }

static int part(int a, int b, double k) { return (int)(a + (b - a) * k); }
static uint32_t mix(uint32_t a, uint32_t b, double k)
{
    return RGB(part(R(a), R(b), k), part(G(a), G(b), k), part(B(a), B(b), k));
}
static int lit(int c, double f) { int v = (int)(c * f); return v > 255 ? 255 : v; }
static uint32_t colour(uint32_t c, int q) { double f = q / 256.0; return RGB(lit(R(c), f), lit(G(c), f), lit(B(c), f)); }

typedef struct { double a, d, h; } pt3;
typedef struct { double x, y; } pt2;

/* mesh.fog_of: the fog and the fade z ahead */
static void fog_of(const clio_rooms *r, double z, double *fog, double *fade)
{
    double t = r->z0 / (z + r->z0), f;
    *fog = 0.18 + 0.82 * clio__pow(t < 1.0 ? t : 1.0, 0.75);
    f = (r->far - z) / r->fade;
    *fade = f < 0.0 ? 0.0 : f > 1.0 ? 1.0 : f;
}

/* Face.corners */
static void corners(const clio_rooms *r, int i, double c[4][3])
{
    const int32_t *n = r->ints + (size_t)i * 9;
    const double *g = r->geo + (size_t)i * 7;
    const int64_t *xy = r->xy + (size_t)i * 4;
    double at = g[0], lo = g[1], hi = g[2], h0 = g[3], h1 = g[4], opened = g[6];
    if (n[0] == CLIO_FACE_FLOOR || n[0] == CLIO_FACE_CEILING) {
        double x = (double)xy[0], y = (double)xy[1];
        c[0][0] = x - 0.5; c[0][1] = y - 0.5; c[0][2] = at;
        c[1][0] = x + 0.5; c[1][1] = y - 0.5; c[1][2] = at;
        c[2][0] = x + 0.5; c[2][1] = y + 0.5; c[2][2] = at;
        c[3][0] = x - 0.5; c[3][1] = y + 0.5; c[3][2] = at;
        return;
    }
    if (opened != 0.0)                                       /* a door slides up */
        h0 = h0 + opened * (h1 - h0);
    if (n[0] == CLIO_FACE_XWALL) {
        c[0][0] = at; c[0][1] = lo; c[0][2] = h0;
        c[1][0] = at; c[1][1] = hi; c[1][2] = h0;
        c[2][0] = at; c[2][1] = hi; c[2][2] = h1;
        c[3][0] = at; c[3][1] = lo; c[3][2] = h1;
    } else {
        c[0][0] = lo; c[0][1] = at; c[0][2] = h0;
        c[1][0] = hi; c[1][1] = at; c[1][2] = h0;
        c[2][0] = hi; c[2][1] = at; c[2][2] = h1;
        c[3][0] = lo; c[3][1] = at; c[3][2] = h1;
    }
}

/* Mesh.spans: the face's rows (y, first column, column after the last) into spans (3 a row); their number, or -1 */
static int spans_of(const clio_rooms *r, int i, double ex, double ey, int *spans)
{
    const int32_t *n = r->ints + (size_t)i * 9;
    double c[4][3];
    pt3 pts[4], poly[8];
    pt2 scr[8];
    int k, m = 0, behind = 1, side_on = 0, y, y0, y1, out = 0;
    double lo, hi, ymin, ymax;
    if (n[0] == CLIO_FACE_XWALL || n[0] == CLIO_FACE_YWALL) {
        double s = (n[0] == CLIO_FACE_XWALL ? n[1] : 0) * r->fx + (n[0] == CLIO_FACE_YWALL ? n[1] : 0) * r->fy;
        side_on = fabs(s) < 0.5;
    }
    corners(r, i, c);
    for (k = 0; k < 4; k++) {                                /* into the camera's space: across, ahead, height */
        double dx = c[k][0] - ex, dy = c[k][1] - ey;
        double d = dx * r->fx + dy * r->fy;
        if (d - r->z0 > -0.5)
            behind = 0;
        pts[k].a = dx * r->rx + dy * r->ry;
        pts[k].d = d;
        pts[k].h = c[k][2];
    }
    if (behind && !side_on)
        return 0;
    for (k = 0; k < 4; k++) {                                /* clipped at near ahead of the camera */
        pt3 a = pts[k], b = pts[(k + 1) % 4];
        int ina = a.d >= r->near, inb = b.d >= r->near;
        if (ina)
            poly[m++] = a;
        if (ina != inb) {
            pt3 p = a.d < b.d ? a : b, q = a.d < b.d ? b : a;
            double t = (r->near - p.d) / (q.d - p.d);
            poly[m].a = p.a + (q.a - p.a) * t;
            poly[m].d = r->near;
            poly[m].h = p.h + (q.h - p.h) * t;
            m++;
        }
    }
    if (m < 3)
        return 0;
    for (k = 0; k < m; k++) {
        scr[k].x = r->half + poly[k].a * r->lx / poly[k].d;
        scr[k].y = r->yh + (r->eye - poly[k].h) * r->ly / poly[k].d;
    }
    ymin = ymax = scr[0].y;
    for (k = 1; k < m; k++) {
        if (scr[k].y < ymin)
            ymin = scr[k].y;
        if (scr[k].y > ymax)
            ymax = scr[k].y;
    }
    {
        double a = ceil(ymin - 0.5), b = ceil(ymax - 0.5);
        y0 = a < 0.0 ? 0 : a > (double)r->ph ? r->ph : (int)a;
        y1 = b > (double)r->ph ? r->ph : b < 0.0 ? 0 : (int)b;
    }
    for (y = y0; y < y1; y++) {
        double yc = y + 0.5, xa, xb;
        int have = 0, ia, ib;
        for (k = 0; k < m; k++) {
            pt2 p = scr[k], q = scr[(k + 1) % m];
            if (p.y > q.y) {
                pt2 t = p;
                p = q;
                q = t;
            }
            if (p.y <= yc && yc < q.y) {
                double xc = p.x + (yc - p.y) * (q.x - p.x) / (q.y - p.y);
                if (!have || xc < lo)
                    lo = xc;
                if (!have || xc > hi)
                    hi = xc;
                have = 1;
            }
        }
        if (!have)
            continue;
        xa = ceil(lo - 0.5);
        xb = ceil(hi - 0.5);
        ia = xa < 0.0 ? 0 : xa > (double)r->pw ? r->pw : (int)xa;
        ib = xb > (double)r->pw ? r->pw : xb < 0.0 ? 0 : (int)xb;
        if (ia < ib) {
            spans[out * 3] = y;
            spans[out * 3 + 1] = ia;
            spans[out * 3 + 2] = ib;
            out++;
        }
    }
    return out;
}

typedef struct {
    const clio_rooms *r;
    clio_frame *f;
    const double *dirx, *diry, *sb;
    double ex, ey;
    uint8_t *walls;
    int32_t *later;
    int nlater;
    const clio_more *g;     /* the G-buffer (0.8), or NULL */
    int step;               /* 2: floors and ceilings at half width */
} ctx;

/* a face's material, and whether colour c glows in it */
static int mat_of(const ctx *x_, int i)
{
    const clio_more *g = x_->g;
    int m = (g->face_mat && i < g->nfaces) ? g->face_mat[i] : 0;
    return (m < 0 || m >= g->nmat) ? 0 : m;
}

static int glows(const ctx *x_, int m, uint32_t c)
{
    const clio_more *g = x_->g;
    int a, b;
    if (!g->glow_at || !g->glow_rgb || m >= g->nmat)
        return 0;
    for (a = g->glow_at[m], b = g->glow_at[m + 1]; a < b; a++)
        if (g->glow_rgb[a] == c)
            return 1;
    return 0;
}

/* the G-buffer at pixel k: kind, material (glowing or not), the light it was drawn with */
static void mark(const ctx *x_, size_t k, int kind, int m, int hot, int q)
{
    x_->g->kinds[k] = (uint8_t)kind;
    x_->g->mats[k] = (uint8_t)(hot ? (m | CLIO_GLOW) : m);
    x_->g->own[k] = (uint8_t)(q < 0 ? 0 : q < 256 ? q : 255);
}

/* Mesh.flat: a floor or a ceiling, one distance a row */
static void flat(ctx *x_, int i, const int *spans, int n)
{
    const clio_rooms *r = x_->r;
    const int32_t *ni = r->ints + (size_t)i * 9;
    const double *g = r->geo + (size_t)i * 7;
    const int64_t *xy = r->xy + (size_t)i * 4;
    int floor_ = ni[0] == CLIO_FACE_FLOOR, s, gm;
    double dh = r->eye - g[0];
    int64_t ix = xy[0], iy = xy[1];
    clio_room_cell cell;
    int PW = r->pw;
    cell.tex = NULL;
    cell.tex_w = cell.tex_h = 0;
    if (ni[2] >= 0) {
        cell.tex = r->texels[ni[2]];
        cell.tex_w = r->tw[ni[2]];
        cell.tex_h = r->th[ni[2]];
    }
    cell.kind = ni[4];
    cell.belt = ni[5];
    cell.toxic = ni[6];
    cell.fire = ni[7];
    cell.mark = ni[8];
    cell.x = xy[2];
    cell.y = xy[3];
    gm = x_->g ? mat_of(x_, i) : 0;
    for (s = 0; s < n; s++) {
        int y = spans[s * 3], x, x0 = spans[s * 3 + 1], x1 = spans[s * 3 + 2], step = x_->step;
        double sb = x_->sb[y], d, z, fog, fade;
        int q;
        uint32_t *row = x_->f->rgb + (size_t)y * PW;
        double *zbuf = x_->f->depth + (size_t)y * PW;
        if (sb == 0.0)
            continue;
        d = dh / sb;
        if (d <= r->near)
            continue;
        z = d - r->z0;
        fog_of(r, z, &fog, &fade);
        q = (int)(fog * fade * 256.0);
        for (x = step == 2 ? x0 - (x0 & 1) : x0; x < x1; x += step) {   /* low: pairs from an even column */
            double ux, uy;
            uint32_t c = NONE, v;
            int one = x >= x0 && z < zbuf[x], two = step == 2 && x + 1 < x1 && z < zbuf[x + 1], xs, qq = q, hot = 0;
            if (!(one || two))
                continue;
            xs = x >= x0 ? x : x + 1;                         /* the pixel whose ray gives the colour */
            ux = x_->ex + d * x_->dirx[xs] - (double)ix;
            uy = x_->ey + d * x_->diry[xs] - (double)iy;
            ux = ux < -0.5 ? -0.5 : ux > 0.4999 ? 0.4999 : ux;
            uy = uy < -0.5 ? -0.5 : uy > 0.4999 ? 0.4999 : uy;
            if (!floor_) {
                if (cell.kind == CLIO_CEILING_SKY) {
                    if (r->sky && y < r->sky_rows) {
                        const uint8_t *p = r->sky + ((size_t)y * PW + xs) * 3;
                        v = RGB(p[0], p[1], p[2]);
                    } else
                        v = HAZE;
                    qq = -1;
                } else {
                    if (cell.tex)
                        c = clio__room_texel(&cell, ux, uy);
                    if (c == NONE) {
                        c = CEIL;
                        if (fabs(ux) > 0.47 || fabs(uy) > 0.47)
                            c = CEIL_LINE;
                        else if (pymod(ix * 7 + iy * 3, 4) == 0 && fabs(ux) < 0.24 && fabs(uy) < 0.24)
                            c = LIGHT;
                    }
                    v = x_->g ? c : colour(c, q);
                }
            } else {
                if (cell.tex)                                /* the floor: as View._backdrop draws it */
                    c = clio__room_texel(&cell, ux, uy);
                if (c == NONE && cell.kind != CLIO_FLOOR_PLAIN) {
                    if (cell.kind == CLIO_FLOOR_TELEPORT) {  /* its rings: the caller's */
                        if (one) {
                            zbuf[x] = z;
                            row[x] = HAZE;
                            x_->later[x_->nlater * 2] = y * PW + x;
                            x_->later[x_->nlater * 2 + 1] = i;
                            x_->nlater++;
                        }
                        if (two) {
                            zbuf[x + 1] = z;
                            row[x + 1] = HAZE;
                            x_->later[x_->nlater * 2] = y * PW + x + 1;
                            x_->later[x_->nlater * 2 + 1] = i;
                            x_->nlater++;
                        }
                        continue;
                    }
                    c = clio__room_fixture(&cell, ux, uy, r->phase);
                }
                if (c == NONE && cell.toxic) {
                    c = noise((int64_t)(ux * 8) + ix * 8, (int64_t)(uy * 8) + iy * 8, r->phase) < 0.07 ? TOXIC_HI : TOXIC;
                    qq = (int)((0.45 + 0.55 * fog) * fade * 256.0);
                    hot = c == TOXIC_HI;
                } else {
                    if (c == NONE) {
                        c = ((ix + iy) & 1) ? FLOOR_A : FLOOR_B;
                        if (fabs(ux) > 0.46 || fabs(uy) > 0.46)
                            c = FLOOR_LINE;
                    }
                    if (cell.fire)
                        c = mix(c, RGB(255, 120, 30),
                                0.3 + 0.25 * noise((int64_t)(ux * 6) + ix * 6, (int64_t)(uy * 6) + iy * 6, r->phase));
                    if (cell.mark == 1) {                    /* fire is coming here: hatched and glowing */
                        int hatch = pymod((int64_t)((ux + uy + 1.0) * 6), 2) == 0;
                        c = mix(c, hatch ? RGB(255, 90, 30) : RGB(150, 30, 20), 0.55 + 0.2 * pymod(r->phase, 2));
                        qq = (int)((0.6 + 0.4 * fog) * fade * 256.0);
                        hot = 1;
                    } else if (cell.mark == 2) {
                        double k = noise((int64_t)(ux * 10) + ix * 10, (int64_t)(uy * 10) + iy * 10, r->phase + 5);
                        c = k < 0.25 ? RGB(255, 230, 120) : k < 0.7 ? RGB(255, 140, 40) : RGB(200, 50, 20);
                        qq = (int)(fade * 256.0);
                        hot = 1;
                    } else
                        hot = cell.fire != 0;
                }
                v = x_->g ? c : colour(c, qq);
            }
            if (one) {
                zbuf[x] = z;
                row[x] = v;
            }
            if (two) {
                zbuf[x + 1] = z;
                row[x + 1] = v;
            }
            if (x_->g) {                                     /* what it is: the sky through an open roof is none */
                int kind = qq < 0 ? CLIO_SURF_SKY : floor_ ? CLIO_SURF_FLOOR : CLIO_SURF_CEILING;
                int h = hot || (qq >= 0 && glows(x_, gm, c));
                size_t k0 = (size_t)y * PW + x;
                if (one)
                    mark(x_, k0, kind, gm, h, qq < 0 ? 0 : qq);
                if (two)
                    mark(x_, k0 + 1, kind, gm, h, qq < 0 ? 0 : qq);
            }
        }
    }
}

/* Mesh.draw's upright faces: one distance a column */
static void upright(ctx *x_, int i, const int *spans, int n, double *col_d, double *col_z, int *col_u, int *col_q,
                    int8_t *col_have)
{
    const clio_rooms *r = x_->r;
    const int32_t *ni = r->ints + (size_t)i * 9;
    const double *g = r->geo + (size_t)i * 7;
    int kind = ni[0], side = ni[1], TW, TH, s, k, wk = 0, gm = 0;
    double nx = kind == CLIO_FACE_XWALL ? side : 0, ny = kind == CLIO_FACE_XWALL ? 0 : side;
    double kf = r->side + (1.0 - r->side) * fabs(nx * r->fx + ny * r->fy);
    double ux = ny, uy = -nx, left = 0.0, c[4][3];
    const uint32_t *tex;
    int PW = r->pw;
    if (ni[2] < 0)
        return;
    tex = r->texels[ni[2]];
    TW = r->tw[ni[2]];
    TH = r->th[ni[2]];
    if (x_->g) {
        wk = kind == CLIO_FACE_XWALL ? (side > 0 ? CLIO_SURF_WALL_PX : CLIO_SURF_WALL_NX)
                                     : (side > 0 ? CLIO_SURF_WALL_PY : CLIO_SURF_WALL_NY);
        gm = mat_of(x_, i);
    }
    corners(r, i, c);
    for (k = 0; k < 4; k++) {
        double v = ux * c[k][0] + uy * c[k][1];
        if (k == 0 || v < left)
            left = v;
    }
    for (k = 0; k < PW; k++)
        col_have[k] = 0;
    for (s = 0; s < n; s++) {
        int y = spans[s * 3], x;
        double sby = x_->sb[y];
        uint32_t *row = x_->f->rgb + (size_t)y * PW;
        double *zbuf = x_->f->depth + (size_t)y * PW;
        for (x = spans[s * 3 + 1]; x < spans[s * 3 + 2]; x++) {
            double d, z, v;
            uint32_t t;
            int u, q;
            if (!col_have[x]) {
                double dd = kind == CLIO_FACE_XWALL ? x_->dirx[x] : x_->diry[x];
                d = dd != 0.0 ? (kind == CLIO_FACE_XWALL ? (g[0] - x_->ex) / dd : (g[0] - x_->ey) / dd) : -1.0;
                if (d <= r->near) {
                    col_have[x] = 2;
                } else {
                    double wx = x_->ex + d * x_->dirx[x], wy = x_->ey + d * x_->diry[x], uu, fog, fade;
                    uu = ux * wx + uy * wy - left;
                    col_u[x] = uu < 0 ? 0 : uu >= 1 ? TW - 1 : (int)(uu * TW);
                    z = d - r->z0;
                    fog_of(r, z, &fog, &fade);
                    col_d[x] = d;
                    col_z[x] = z;
                    col_q[x] = (int)(fog * kf * fade * 256.0);
                    col_have[x] = 1;
                }
            }
            if (col_have[x] != 1)
                continue;
            d = col_d[x];
            z = col_z[x];
            u = col_u[x];
            q = col_q[x];
            if (z >= zbuf[x])
                continue;
            v = g[5] - (r->eye - sby * d) + g[6];
            v -= floor(v);
            {
                int ty = (int)(v * TH);
                if (ty > TH - 1)
                    ty = TH - 1;
                t = tex[(size_t)ty * TW + u];
            }
            if (t == NONE)
                continue;
            zbuf[x] = z;
            x_->walls[(size_t)y * PW + x] = 1;
            if (x_->g) {                                     /* its own colour, lit later */
                row[x] = t;
                mark(x_, (size_t)y * PW + x, wk, gm, glows(x_, gm, t), q);
            } else
                row[x] = colour(t, q);
        }
    }
}

int clio_frame_rooms(clio_frame *f, const clio_rooms *r, int32_t *later, int *later_n)
{
    ctx c = {0};
    double *dirx = NULL, *diry = NULL, *sb = NULL, *col_d = NULL, *col_z = NULL;
    int *spans = NULL, *col_u = NULL, *col_q = NULL, i, err = 0, x, y, PW, PH;
    int8_t *col_have = NULL;
    uint8_t *walls = NULL;
    size_t npx;
    if (!f || !r || !later || !later_n || !f->rgb || !f->depth || r->pw != f->pw || r->ph != f->ph || r->pw <= 0 ||
        r->ph <= 0 || r->nfaces < 0 || (r->nfaces && (!r->ints || !r->geo || !r->xy)) || r->ntex < 0 ||
        (r->ntex && (!r->texels || !r->tw || !r->th)) || r->lx == 0.0 || r->ly == 0.0)
        return -1;
    for (i = 0; i < r->nfaces; i++) {
        int t = r->ints[(size_t)i * 9 + 2];
        if (t >= r->ntex || (t >= 0 && (!r->texels[t] || r->tw[t] <= 0 || r->th[t] <= 0)))
            return -1;
    }
    if (!clio__has_maths())
        return -2;
    PW = r->pw;
    PH = r->ph;
    npx = (size_t)PW * PH;
    dirx = malloc(sizeof(double) * PW);
    diry = malloc(sizeof(double) * PW);
    sb = malloc(sizeof(double) * PH);
    col_d = malloc(sizeof(double) * PW);
    col_z = malloc(sizeof(double) * PW);
    col_u = malloc(sizeof(int) * PW);
    col_q = malloc(sizeof(int) * PW);
    col_have = malloc((size_t)PW);
    spans = malloc(sizeof(int) * 3 * (size_t)PH);
    walls = calloc(npx, 1);
    if (!dirx || !diry || !sb || !col_d || !col_z || !col_u || !col_q || !col_have || !spans || !walls) {
        err = -3;
        goto done;
    }
    for (x = 0; x < PW; x++) {
        double sa = (x + 0.5 - r->half) / r->lx;
        dirx[x] = r->fx + sa * r->rx;
        diry[x] = r->fy + sa * r->ry;
    }
    for (y = 0; y < PH; y++)
        sb[y] = (y + 0.5 - r->yh) / r->ly;
    for (i = 0; i < (int)npx; i++) {
        f->rgb[i] = HAZE;
        f->depth[i] = r->none;
    }
    c.g = clio__more(f);
    if (c.g && (!c.g->kinds || !c.g->mats || !c.g->own))
        c.g = NULL;
    if (c.g) {
        memset(c.g->kinds, 0, npx);
        memset(c.g->mats, 0, npx);
        memset(c.g->own, 0, npx);
    }
    c.step = (clio__more(f) && clio__more(f)->low) ? 2 : 1;
    c.r = r;
    c.f = f;
    c.dirx = dirx;
    c.diry = diry;
    c.sb = sb;
    c.ex = r->cx - r->fx * r->z0;                             /* the camera */
    c.ey = r->cy - r->fy * r->z0;
    c.walls = walls;
    c.later = later;
    c.nlater = 0;
    for (i = 0; i < r->nfaces; i++) {
        int kind = r->ints[(size_t)i * 9], n = spans_of(r, i, c.ex, c.ey, spans);
        if (n <= 0)
            continue;
        if (kind == CLIO_FACE_FLOOR || kind == CLIO_FACE_CEILING)
            flat(&c, i, spans, n);
        else
            upright(&c, i, spans, n, col_d, col_z, col_u, col_q, col_have);
    }
    if (f->cover && f->sx > 0 && f->sy > 0) {                 /* Mesh.cover: the nearest wall over each cell */
        int W = PW / f->sx, VR = PH / f->sy, row, col;
        for (row = 0; row < VR; row++)
            for (col = 0; col < W; col++) {
                double best = r->none;
                int yy, xx;
                for (yy = row * f->sy; yy < row * f->sy + f->sy; yy++)
                    for (xx = col * f->sx; xx < col * f->sx + f->sx; xx++) {
                        size_t k = (size_t)yy * PW + xx;
                        if (walls[k] && f->depth[k] < best)
                            best = f->depth[k];
                    }
                if (best < r->none)
                    f->cover[row * W + col] = best;
            }
    }
done:
    *later_n = c.nlater;
    free(dirx);
    free(diry);
    free(sb);
    free(col_d);
    free(col_z);
    free(col_u);
    free(col_q);
    free(col_have);
    free(spans);
    free(walls);
    return err;
}
