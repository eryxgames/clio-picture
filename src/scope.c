/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Daniel Sandner
 *
 * The vector display: CLIo's engine/vector.py Scope - dots lit by lines, faces filled into a depth
 * buffer so that lines behind them do not show, and the characters read from the dots. Every float
 * is worked out in the order the Python works it out, with + - * / and floor and ceil only. */
#define CLIO_PICTURE_BUILD
#include "clio_picture.h"

#include <math.h>
#include <stddef.h>

static const uint8_t BIT[4][2] = {{0x01, 0x08}, {0x02, 0x10}, {0x04, 0x20}, {0x40, 0x80}};   /* [row][column] */

#define TAN_22_5 0.41421356237309503      /* a line runs flat while it rises less than this for each dot across */

static void see(const clio_eye *e, const double *p, double *out)
{
    double dx = p[0] - e->eye[0], dy = p[1] - e->eye[1], dz = p[2] - e->eye[2];
    out[0] = dx * e->right[0] + dy * e->right[1] + dz * e->right[2];
    out[1] = dx * e->up[0] + dy * e->up[1] + dz * e->up[2];
    out[2] = dx * e->ahead[0] + dy * e->ahead[1] + dz * e->ahead[2];
}

void clio_scope_clear(clio_scope *s, double afterglow)
{
    int n = s->cols * 2 * s->rows * 4, cells = s->cols * s->rows, i;
    if (afterglow > 0.0) {
        for (i = 0; i < n; i++) {
            double v = s->light[i] * afterglow;
            s->light[i] = v > 0.07 ? v : 0.0;
        }
    } else {
        for (i = 0; i < n; i++)
            s->light[i] = 0.0;
    }
    for (i = 0; i < cells; i++) {
        s->best[i] = 0.0;
        s->way[i] = -1;
    }
}

void clio_scope_dot(clio_scope *s, int x, int y, double bright, int32_t ink, int way)
{
    int W = s->cols * 2, H = s->rows * 4, i, c;
    double v;
    if (x < 0 || x >= W || y < 0 || y >= H)
        return;
    i = y * W + x;
    v = s->light[i];
    if (v <= 0.0) {
        s->light[i] = bright;
    } else {
        double hi = v > bright ? v : bright, lo = v < bright ? v : bright;
        double sum = hi + 0.35 * lo;
        s->light[i] = sum < 2.0 ? sum : 2.0;
    }
    c = (y >> 2) * s->cols + (x >> 1);
    if (bright >= s->best[c]) {
        s->best[c] = bright;
        s->ink[c] = ink;
    }
    if (way >= 0) {
        int32_t w = s->way[c];
        s->way[c] = (w < 0 || w == way) ? way : 4;
    }
}

/* Is a point of a line behind the faces all round its dot? */
static int behind(const clio_scope *s, int x, int y, double w)
{
    int W = s->cols * 2, H = s->rows * 4;
    const double *depth = s->depth;
    if (x - 1 >= 0 && w >= depth[y * W + x - 1])
        return 0;
    if (x + 1 < W && w >= depth[y * W + x + 1])
        return 0;
    if (y - 1 >= 0 && w >= depth[(y - 1) * W + x])
        return 0;
    if (y + 1 < H && w >= depth[(y + 1) * W + x])
        return 0;
    return 1;
}

void clio_scope_line2(clio_scope *s, double x0, double y0, double x1, double y1, double bright, int32_t ink,
                      int has_w, double w0, double w1)
{
    int W = s->cols * 2, H = s->rows * 4, n, i, way, k, lx = 0, ly = 0, has_last = 0;
    double dx = x1 - x0, dy = y1 - y0, t0 = 0.0, t1 = 1.0, sx, sy, sw, ax, ay, big;
    double p[4], q[4];
    const double *depth = s->depth;
    p[0] = -dx; q[0] = x0 + 1.0;                 /* keep the part of it that is on the display */
    p[1] = dx;  q[1] = W - x0;
    p[2] = -dy; q[2] = y0 + 1.0;
    p[3] = dy;  q[3] = H - y0;
    for (k = 0; k < 4; k++) {
        if (fabs(p[k]) < 1e-12) {
            if (q[k] < 0)
                return;
        } else {
            double r = q[k] / p[k];
            if (p[k] < 0)
                t0 = r > t0 ? r : t0;
            else
                t1 = r < t1 ? r : t1;
        }
    }
    if (t0 > t1)
        return;
    if (t0 > 0.0 || t1 < 1.0) {
        double nx0, ny0, nx1, ny1;
        if (has_w) {
            double a = w0 + (w1 - w0) * t0, b = w0 + (w1 - w0) * t1;
            w0 = a;
            w1 = b;
        }
        nx0 = x0 + dx * t0;
        ny0 = y0 + dy * t0;
        nx1 = x0 + dx * t1;
        ny1 = y0 + dy * t1;
        x0 = nx0; y0 = ny0; x1 = nx1; y1 = ny1;
        dx = x1 - x0;
        dy = y1 - y0;
    }
    ax = fabs(dx);
    ay = fabs(dy);
    big = ax > ay ? ax : ay;
    n = (int)(big + 0.5);
    if (n < 1)
        n = 1;
    if (ay <= ax * TAN_22_5)                     /* the way it runs: flat, down to the right, upright, up to the right */
        way = 0;
    else if (ax <= ay * TAN_22_5)
        way = 2;
    else
        way = ((dx > 0) == (dy > 0)) ? 1 : 3;
    sx = dx / n;
    sy = dy / n;
    sw = has_w ? (w1 - w0) / n : 0.0;
    for (i = 0; i <= n; i++) {
        int x = (int)(x0 + sx * i + 0.5), y = (int)(y0 + sy * i + 0.5);
        double b = bright;
        if (x < 0 || x >= W || y < 0 || y >= H || (has_last && x == lx && y == ly))   /* a dot is lit once by a line */
            continue;
        lx = x;
        ly = y;
        has_last = 1;
        if (has_w) {
            double w = w0 + sw * i;
            if (depth != NULL && w < depth[y * W + x] && behind(s, x, y, w))
                continue;
            if (s->has_cue) {
                double d = 1.0 / w;
                double kk = (d - s->cue_near) / (s->cue_far - s->cue_near);
                b *= 1.0 - (1.0 - s->dim) * (kk < 0 ? 0.0 : kk > 1 ? 1.0 : kk);
            }
        }
        clio_scope_dot(s, x, y, b, ink, way);
    }
}

/* A line between two points in space: cut where it passes behind the eye, then drawn. */
static void line3(clio_scope *s, const clio_eye *e, const double *a, const double *b, double bright, int32_t ink)
{
    double pa[3], pb[3], nearz = e->near_z, f = e->f, cx, cy;
    see(e, a, pa);
    see(e, b, pb);
    if (pa[2] < nearz && pb[2] < nearz)
        return;
    if (pa[2] < nearz) {
        double k = (nearz - pa[2]) / (pb[2] - pa[2]);
        pa[0] = pa[0] + (pb[0] - pa[0]) * k;
        pa[1] = pa[1] + (pb[1] - pa[1]) * k;
        pa[2] = nearz;
    } else if (pb[2] < nearz) {
        double k = (nearz - pb[2]) / (pa[2] - pb[2]);
        pb[0] = pb[0] + (pa[0] - pb[0]) * k;
        pb[1] = pb[1] + (pa[1] - pb[1]) * k;
        pb[2] = nearz;
    }
    cx = s->cols * 2 / 2.0;
    cy = s->rows * 4 / 2.0;
    clio_scope_line2(s, cx + pa[0] / pa[2] * f, cy - pa[1] / pa[2] * f, cx + pb[0] / pb[2] * f, cy - pb[1] / pb[2] * f,
                     bright, ink, 1, 1.0 / pa[2], 1.0 / pb[2]);
}

int clio_scope_wire(clio_scope *s, const clio_eye *e, const double *verts, int nverts, const int32_t *edges,
                    int nedges, const uint8_t *skip, double bright, int32_t ink)
{
    int i;
    if (nverts < 0 || nedges < 0)
        return -1;
    for (i = 0; i < nedges; i++) {
        int32_t a = edges[2 * i], b = edges[2 * i + 1];
        if (skip != NULL && skip[i])
            continue;
        if (a < 0 || a >= nverts || b < 0 || b >= nverts)
            return -1;
        line3(s, e, verts + 3 * a, verts + 3 * b, bright, ink);
    }
    return 0;
}

/* max(0, ceil(v - 0.5)) and min(n - 1, floor(v - 0.5)) as whole numbers, whatever the size of v */
static int first(double v, int n)
{
    double c = ceil(v - 0.5);
    return c < 0 ? 0 : c > n ? n : (int)c;
}

static int last(double v, int n)
{
    double f = floor(v - 0.5);
    return f > n - 1 ? n - 1 : f < -1 ? -1 : (int)f;
}

/* One triangle of a face into the depth buffer (x, y in dots, w = 1 / distance): the nearest stays. */
static void fill(clio_scope *s, const double *a, const double *b, const double *c)
{
    int W = s->cols * 2, H = s->rows * 4, ys, ye, y;
    const double *p0 = a, *p1 = b, *p2 = c, *t;
    double x0, y0, w0, x1, y1, w1, x2, y2, w2;
    double *depth = s->depth;
    if (p1[1] < p0[1]) { t = p0; p0 = p1; p1 = t; }      /* by how far down, the first of two equals first */
    if (p2[1] < p1[1]) {
        t = p1; p1 = p2; p2 = t;
        if (p1[1] < p0[1]) { t = p0; p0 = p1; p1 = t; }
    }
    x0 = p0[0]; y0 = p0[1]; w0 = p0[2];
    x1 = p1[0]; y1 = p1[1]; w1 = p1[2];
    x2 = p2[0]; y2 = p2[1]; w2 = p2[2];
    if (y2 - y0 < 1e-9)
        return;
    ys = first(y0, H);
    ye = last(y2, H);
    for (y = ys; y <= ye; y++) {
        double yc = y + 0.5, k = (yc - y0) / (y2 - y0), xa, wa, xb, wb, dw, w;
        int xs, xe, x;
        double *row;
        xa = x0 + (x2 - x0) * k;
        wa = w0 + (w2 - w0) * k;
        if (yc < y1) {
            k = (y1 - y0 > 1e-9) ? (yc - y0) / (y1 - y0) : 0.0;
            xb = x0 + (x1 - x0) * k;
            wb = w0 + (w1 - w0) * k;
        } else {
            k = (y2 - y1 > 1e-9) ? (yc - y1) / (y2 - y1) : 0.0;
            xb = x1 + (x2 - x1) * k;
            wb = w1 + (w2 - w1) * k;
        }
        if (xa > xb) {
            double tx = xa, tw = wa;
            xa = xb; xb = tx;
            wa = wb; wb = tw;
        }
        xs = first(xa, W);
        xe = last(xb, W);
        if (xs > xe)
            continue;
        dw = (xb - xa > 1e-9) ? (wb - wa) / (xb - xa) : 0.0;
        w = wa + (xs + 0.5 - xa) * dw;
        row = depth + (size_t)y * W;
        for (x = xs; x <= xe; x++) {
            if (w > row[x])
                row[x] = w;
            w += dw;
        }
    }
}

#define FACE_MOST 16                          /* corners of a face at most (cut at the eye's nearz plane: one more) */

int clio_scope_hide(clio_scope *s, const clio_eye *e, const double *verts, int nverts, const int32_t *corners,
                    const int32_t *sizes, int nfaces, double back)
{
    int W = s->cols * 2, H = s->rows * 4, fi, at = 0;
    double nearz = e->near_z, f = e->f, cx = W / 2.0, cy = H / 2.0;
    if (s->depth == NULL || nverts < 0 || nfaces < 0)
        return -1;
    for (fi = 0; fi < nfaces; fi++) {
        int n = sizes[fi], i, cut = 0, m;
        double pts[FACE_MOST][3], use[FACE_MOST + 1][3], scr[FACE_MOST + 1][3];
        if (n < 3 || n > FACE_MOST)
            return -1;
        for (i = 0; i < n; i++) {
            int32_t v = corners[at + i];
            if (v < 0 || v >= nverts)
                return -1;
            see(e, verts + 3 * v, pts[i]);
            if (pts[i][2] < nearz)
                cut = 1;
        }
        at += n;
        m = 0;
        if (cut) {                                /* cut the face where it passes behind the eye */
            for (i = 0; i < n; i++) {
                const double *p = pts[i], *q = pts[(i + 1) % n];
                if (p[2] >= nearz) {
                    if (m > FACE_MOST)
                        return -1;
                    use[m][0] = p[0]; use[m][1] = p[1]; use[m][2] = p[2];
                    m++;
                }
                if ((p[2] >= nearz) != (q[2] >= nearz)) {
                    double k = (nearz - p[2]) / (q[2] - p[2]);
                    if (m > FACE_MOST)
                        return -1;
                    use[m][0] = p[0] + (q[0] - p[0]) * k;
                    use[m][1] = p[1] + (q[1] - p[1]) * k;
                    use[m][2] = nearz;
                    m++;
                }
            }
            if (m < 3)
                continue;
        } else {
            for (i = 0; i < n; i++) {
                use[i][0] = pts[i][0]; use[i][1] = pts[i][1]; use[i][2] = pts[i][2];
            }
            m = n;
        }
        for (i = 0; i < m; i++) {
            scr[i][0] = cx + use[i][0] / use[i][2] * f;
            scr[i][1] = cy - use[i][1] / use[i][2] * f;
            scr[i][2] = back / use[i][2];
        }
        for (i = 1; i < m - 1; i++)
            fill(s, scr[0], scr[i], scr[i + 1]);
    }
    return 0;
}

/* A colour at a brightness: dim to full, and towards white where lines cross (Scope.tone). */
static void tone(uint32_t rgb, double bright, uint8_t *out)
{
    double k = 0.2 + 0.8 * (bright < 1.0 ? bright : 1.0);
    double over = bright - 1.0, hot;
    int i;
    if (over > 1.0)
        over = 1.0;
    if (over < 0.0)
        over = 0.0;
    hot = over * 0.7;
    for (i = 0; i < 3; i++) {
        double c = (double)((rgb >> (16 - 8 * i)) & 0xFF);
        double v = c * k + (255 - c * k) * hot;
        out[i] = (uint8_t)(int)(v < 255 ? v : 255);
    }
}

int clio_scope_cells(const clio_scope *s, const uint32_t *colours, int ncolours, int glow, int has_paper,
                     uint32_t paper, uint8_t *out)
{
    int W = s->cols * 2, cols = s->cols, r, c, k;
    const double *light = s->light;
    if (s->cols <= 0 || s->rows <= 0)
        return -1;
    for (r = 0; r < s->rows; r++) {
        for (c = 0; c < cols; c++) {
            const double *p = light + (size_t)(r * 4) * W + c * 2;
            double vals[8], most, floor_;
            uint8_t bits = 0, *o = out + ((size_t)r * cols + c) * 8;
            int ci = r * cols + c;
            uint32_t rgb;
            vals[0] = p[0];         vals[1] = p[1];
            vals[2] = p[W];         vals[3] = p[W + 1];
            vals[4] = p[2 * W];     vals[5] = p[2 * W + 1];
            vals[6] = p[3 * W];     vals[7] = p[3 * W + 1];
            most = vals[0];
            for (k = 1; k < 8; k++)
                if (vals[k] > most)
                    most = vals[k];
            if (most > 0.0) {
                floor_ = most * 0.55;             /* beside a fresh line its afterglow does not show */
                for (k = 0; k < 8; k++)
                    if (vals[k] >= floor_)
                        bits |= BIT[k >> 1][k & 1];
            }
            o[0] = bits;
            if (!bits) {
                o[1] = CLIO_BLANK;
                o[2] = o[3] = o[4] = o[5] = o[6] = o[7] = 0;
                continue;
            }
            if (s->ink[ci] < 0 || s->ink[ci] >= ncolours)
                return -1;
            rgb = colours[s->ink[ci]];
            tone(rgb, most, o + 2);
            if (glow && most > 0.5) {
                double over = most - 0.5;
                double g = 0.05 + 0.05 * (over < 1.0 ? over : 1.0);
                for (k = 0; k < 3; k++) {
                    double v = (double)((rgb >> (16 - 8 * k)) & 0xFF);
                    if (has_paper) {
                        double sum = (double)((paper >> (16 - 8 * k)) & 0xFF) + v * g;
                        o[5 + k] = (uint8_t)(int)(sum < 255 ? sum : 255);
                    } else {
                        o[5 + k] = (uint8_t)(int)(v * g);
                    }
                }
                o[1] = CLIO_TWO;
            } else if (has_paper) {
                o[5] = (uint8_t)(paper >> 16);
                o[6] = (uint8_t)(paper >> 8);
                o[7] = (uint8_t)paper;
                o[1] = CLIO_TWO;
            } else {
                o[5] = o[6] = o[7] = 0;
                o[1] = CLIO_DOTS;
            }
        }
    }
    return 0;
}
