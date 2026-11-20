/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2024-2026 Daniel Sandner
 *
 * Models, as CLIo's engine/firstperson.py View.draw_model draws them: each triangle projected (with the
 * rooms' 0.64 ^ z, or open land's perspective), turned-away ones dropped, shaded by engine/model.py pbr at its
 * middle or at its corners, darker with distance or into the haze, flashed or tinted, and rastered into the
 * frame with the model's own depth. pow and exp are the C library's, through pointers. */
#define CLIO_PICTURE_BUILD
#include "clio_picture.h"

#include <math.h>
#include <stdlib.h>

#define R(c) ((int)(((c) >> 16) & 255u))
#define G(c) ((int)(((c) >> 8) & 255u))
#define B(c) ((int)((c) & 255u))
#define PI 3.141592653589793

/* pow and exp: the ones CLIo's Python uses, handed over by it (clio_maths) - so nothing here links the C
 * library's own (glibc versions them anew now and then) and the compiler cannot rewrite them */
static double (*volatile POW)(double, double) = NULL;
static double (*volatile EXP)(double) = NULL;

void clio_maths(double (*pow_fn)(double, double), double (*exp_fn)(double))
{
    POW = pow_fn;
    EXP = exp_fn;
}

/* Python's float ** float, for finite numbers (CPython float_pow): 1 for a zero power, a negative base taken
 * as positive and the result turned round for an odd power, 1 ** w = 1, 0 ** w = 0 */
static double py_pow(double iv, double iw)
{
    int negate = 0;
    if (iw == 0.0)
        return 1.0;
    if (iv == 0.0) {
        double a = fabs(iw);
        int odd = a == floor(a) && a - 2.0 * floor(a / 2.0) == 1.0;
        return odd ? iv : 0.0;
    }
    if (iv < 0.0) {
        double a = fabs(iw);
        iv = -iv;
        negate = a == floor(a) && a - 2.0 * floor(a / 2.0) == 1.0;
    }
    if (iv == 1.0)
        return negate ? -1.0 : 1.0;
    iv = POW(iv, iw);
    return negate ? -iv : iv;
}

static double maxd(double a, double b) { return b > a ? b : a; }   /* Python's max(a, b): a unless b is more */
static double mind(double a, double b) { return b < a ? b : a; }   /* min(a, b): a unless b is less */

double clio_room_scale(double z) { return POW ? py_pow(0.64, z) : 0.0; }

double clio_haze(double d, double vis) { return EXP ? 1.0 - EXP(-d / vis) : 0.0; }

static double srgb(double c)
{
    return c > 0.0031308 ? 1.055 * py_pow(c, 1 / 2.4) - 0.055 : 12.92 * c;
}

void clio_shade(const clio_material *mat, const double *rgb, const double *n, const double *l, const double *v,
                int *out)
{
    if (!POW) {
        out[0] = out[1] = out[2] = -1;
        return;
    }
    {
    double h[3], hn, ndl, ndv, ndh, vdh, rough, metal, a2, d, k, g, up, rx, ry, rz, lobe, surround, fres;
    static const double env_k[3] = {1.0, 0.97, 0.9};
    int i;
    h[0] = l[0] + v[0];
    h[1] = l[1] + v[1];
    h[2] = l[2] + v[2];
    hn = sqrt(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]);
    if (hn == 0.0)
        hn = 1.0;
    ndl = maxd(0.0, n[0] * l[0] + n[1] * l[1] + n[2] * l[2]);
    ndv = maxd(1e-3, n[0] * v[0] + n[1] * v[1] + n[2] * v[2]);
    ndh = maxd(0.0, (n[0] * h[0] + n[1] * h[1] + n[2] * h[2]) / hn);
    vdh = maxd(0.0, (v[0] * h[0] + v[1] * h[1] + v[2] * h[2]) / hn);
    rough = mat->rough;
    metal = mat->metal;
    a2 = py_pow(rough * rough, 2);
    d = a2 / (PI * py_pow(ndh * ndh * (a2 - 1) + 1, 2));
    k = py_pow(rough + 1, 2) / 8;
    g = (ndl / (ndl * (1 - k) + k)) * (ndv / (ndv * (1 - k) + k));
    up = 0.5 + 0.5 * n[2];
    rx = 2 * ndv * n[0] - v[0];
    ry = 2 * ndv * n[1] - v[1];
    rz = 2 * ndv * n[2] - v[2];
    lobe = maxd(0.0, rx * l[0] + ry * l[1] + rz * l[2]);
    surround = 0.15 + 1.0 * lobe * lobe + 0.2 * maxd(0.0, rz);
    fres = py_pow(1 - ndv, 5);
    for (i = 0; i < 3; i++) {
        double alb = rgb[i];
        double f0 = 0.04 * (1 - metal) + alb * metal;
        double fr = f0 + (1 - f0) * py_pow(1 - vdh, 5);
        double spec = d * g * fr / (4 * ndl * ndv + 1e-4);
        double c = ((1 - fr) * (1 - metal) * alb / PI + spec) * ndl * 6.0, s, env;
        c += alb * (0.08 + 0.16 * up) * (1 - 0.5 * metal);
        env = surround * env_k[i];
        c += env * (f0 + (1 - f0) * fres * 0.5) * (1 - 0.6 * rough);
        c = c / (1 + c);
        s = srgb(c);
        if (mat->glow != 0.0) {                            /* light of its own: towards its colour, not to white */
            g = mind(1.0, mat->glow / 2.5);                /* in model.pbr this is g too: the next colours' spec uses it */
            s = s * (1 - g) + srgb(alb) * g;
        }
        out[i] = (int)(255 * s);
    }
    }
}

/* firstperson._unit3 */
static void unit3(double x, double y, double z, double *o)
{
    double n = sqrt(x * x + y * y + z * z);
    if (n == 0.0)
        n = 1.0;
    o[0] = x / n;
    o[1] = y / n;
    o[2] = z / n;
}

/* firstperson._normal: the cross product turned round (the model-to-world turn is a mirror) */
static void normal(const double *p0, const double *p1, const double *p2, double *o)
{
    double ux = p1[0] - p0[0], uy = p1[1] - p0[1], uz = p1[2] - p0[2];
    double vx = p2[0] - p0[0], vy = p2[1] - p0[1], vz = p2[2] - p0[2];
    unit3(-(uy * vz - uz * vy), -(uz * vx - ux * vz), -(ux * vy - uy * vx), o);
}

static double tz(const clio_model_draw *m, double z)
{
    if (m->persp == 0.0)
        return py_pow(0.64, z);
    return m->persp / maxd(0.05, z + m->persp);
}

/* View.project, or draw_model's own near one; 0: behind the eye */
static int project(const clio_model_draw *m, const double *p, double *o)
{
    double dx = p[0] - m->cx, dy = p[1] - m->cy;
    double a = dx * m->rx + dy * m->ry, z = dx * m->fx + dy * m->fy, t;
    if (m->flat > 0) {
        t = tz(m, m->zm + (maxd(z, 0.0) - m->zm) * (1 - m->flat));
        o[2] = maxd(z, 0.05);
    } else {
        if (z < 0.12)
            return 0;
        t = tz(m, z);
        o[2] = z;
    }
    o[0] = m->half + a * m->lw * t;
    o[1] = m->yh + m->fs * t - p[2] * (m->fs + m->ch) * t;
    return 1;
}

static int part(int a, int b, double k) { return (int)(a + (b - a) * k); }

int clio_frame_model_draw(clio_frame *f, const clio_model_draw *m, double *out)
{
    double *normals = NULL;
    double top, xmin = 0, xmax = 0;
    int i, shown = 0, j;
    if (!POW || !EXP)
        return -2;
    if (!f || !m || !out || m->ntri < 0 || (m->ntri && (!m->pts || !m->mat || !m->own || !m->colour)) ||
        !m->mats || m->nmats <= 0 || (m->smooth && (!m->corner || m->nverts <= 0)))
        return -1;
    for (i = 0; i < m->ntri; i++)
        if (m->mat[i] < 0 || m->mat[i] >= m->nmats || (m->smooth && (m->corner[3 * i] < 0 ||
            m->corner[3 * i] >= m->nverts || m->corner[3 * i + 1] < 0 || m->corner[3 * i + 1] >= m->nverts ||
            m->corner[3 * i + 2] < 0 || m->corner[3 * i + 2] >= m->nverts)))
            return -1;
    if (m->smooth) {                                      /* a corner's normal: its triangles' together */
        normals = (double *)calloc((size_t)m->nverts * 3, sizeof(double));
        if (!normals)
            return -3;
        for (i = 0; i < m->ntri; i++) {
            const double *p = m->pts + 9 * i;
            double n[3];
            normal(p, p + 3, p + 6, n);
            for (j = 0; j < 3; j++) {
                double *s = normals + 3 * m->corner[3 * i + j];
                s[0] += n[0];
                s[1] += n[1];
                s[2] += n[2];
            }
        }
    }
    clio_frame_model(f);
    top = f->ph;
    for (i = 0; i < m->ntri; i++) {
        const double *p = m->pts + 9 * i;
        const clio_material *mat = m->mats + m->mat[i];
        double pts[9], cols[9], rgb[3], depth, fog, hk = 0.0, area;
        int c3[3], smooth = 0;
        if (!project(m, p, pts) || !project(m, p + 3, pts + 3) || !project(m, p + 6, pts + 6))
            continue;
        area = (pts[3] - pts[0]) * (pts[7] - pts[1]) - (pts[4] - pts[1]) * (pts[6] - pts[0]);
        if (m->cull && area <= 0)                          /* turned away */
            continue;
        if (m->own[i]) {
            rgb[0] = m->colour[3 * i];
            rgb[1] = m->colour[3 * i + 1];
            rgb[2] = m->colour[3 * i + 2];
        } else {
            rgb[0] = mat->r;
            rgb[1] = mat->g;
            rgb[2] = mat->b;
        }
        depth = (pts[2] + pts[5] + pts[8]) / 3;
        fog = mat->glow == 0.0 ? 0.5 + 0.5 * mind(1.0, tz(m, depth) * 1.6) : 1.0;
        if (m->air) {
            fog = 1.0;
            hk = (1.0 - EXP(-depth / m->vis)) * (mat->glow != 0.0 ? 0.35 : 1.0);
        }
        if (m->smooth) {
            smooth = 1;
            for (j = 0; j < 3; j++) {
                const double *q = p + 3 * j;
                double n[3], v[3];
                const double *s = normals + 3 * m->corner[3 * i + j];
                unit3(s[0], s[1], s[2], n);
                unit3(m->cx - q[0], m->cy - q[1], m->eye - q[2], v);
                clio_shade(mat, rgb, n, m->light, v, c3);
                if (m->air) {
                    cols[3 * j] = part(c3[0], R(m->haze), hk);
                    cols[3 * j + 1] = part(c3[1], G(m->haze), hk);
                    cols[3 * j + 2] = part(c3[2], B(m->haze), hk);
                } else {
                    cols[3 * j] = c3[0] * fog;
                    cols[3 * j + 1] = c3[1] * fog;
                    cols[3 * j + 2] = c3[2] * fog;
                }
            }
        } else {
            double n[3], v[3];
            int col[3];
            normal(p, p + 3, p + 6, n);
            if (!m->cull && n[0] * (m->cx - p[0]) + n[1] * (m->cy - p[1]) + n[2] * (m->eye - p[2]) < 0) {
                n[0] = -n[0];                              /* a shard's back: lit as its front */
                n[1] = -n[1];
                n[2] = -n[2];
            }
            unit3(m->cx - (p[0] + p[3] + p[6]) / 3, m->cy - (p[1] + p[4] + p[7]) / 3, m->eye - (p[2] + p[5] + p[8]) / 3,
                  v);
            clio_shade(mat, rgb, n, m->light, v, c3);
            if (m->air) {
                col[0] = part(c3[0], R(m->haze), hk);
                col[1] = part(c3[1], G(m->haze), hk);
                col[2] = part(c3[2], B(m->haze), hk);
            } else {
                col[0] = (int)(c3[0] * fog);
                col[1] = (int)(c3[1] * fog);
                col[2] = (int)(c3[2] * fog);
            }
            if (m->flash)
                col[0] = col[1] = col[2] = 255;
            else if (m->tinted) {
                col[0] = part(col[0], R(m->tint), 0.6);
                col[1] = part(col[1], G(m->tint), 0.6);
                col[2] = part(col[2], B(m->tint), 0.6);
            }
            cols[0] = col[0];
            cols[1] = col[1];
            cols[2] = col[2];
        }
        clio_frame_triangle(f, pts, cols, smooth);
        if (!shown) {
            xmin = xmax = pts[0];
            shown = 1;
        }
        for (j = 0; j < 3; j++) {
            if (pts[3 * j + 1] < top)
                top = pts[3 * j + 1];
            if (pts[3 * j] < xmin)
                xmin = pts[3 * j];
            if (pts[3 * j] > xmax)
                xmax = pts[3 * j];
        }
    }
    free(normals);
    out[0] = shown;
    out[1] = top;
    out[2] = xmin;
    out[3] = xmax;
    return 0;
}
