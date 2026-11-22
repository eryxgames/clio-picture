/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Daniel Sandner
 *
 * Surfaces lit by their materials: CLIo's engine/surface.py (Shader.light, Shader.spec, ggx, shade, splashes) with the
 * same operations in the same order, so that the same pixels come out; the G-buffer handed over beside a frame; the
 * picture turned over (View.turned_over). */
#define CLIO_PICTURE_BUILD
#include "clio_picture.h"
#include "internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#  define CLIO_TLS __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
#  define CLIO_TLS __thread
#else
#  define CLIO_TLS _Thread_local
#endif

#define R(c) ((int)(((c) >> 16) & 255u))
#define G(c) ((int)(((c) >> 8) & 255u))
#define B(c) ((int)((c) & 255u))
#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define PI 3.141592653589793

/* ---- the G-buffer beside a frame, for this thread's calls ------------------------------------------------------ */
static CLIO_TLS const clio_frame *more_f = NULL;
static CLIO_TLS clio_more *more_m = NULL;

void clio_frame_more(clio_frame *f, clio_more *m)
{
    more_f = m ? f : NULL;
    more_m = m;
}

const clio_more *clio__more(const clio_frame *f)
{
    return (more_m && more_f == f) ? more_m : NULL;
}

/* ---- light --------------------------------------------------------------------------------------------------- */
static double mind(double a, double b) { return b < a ? b : a; }   /* Python's min(a, b): a unless b is less */
static double maxd(double a, double b) { return b > a ? b : a; }   /* max(a, b): a unless b is more */

double clio_ggx(const double *n, const double *l, const double *v, double rough, double f0)
{
    double nl, nv, hx, hy, hz, hn, nh, vh, a, a2, dd, D, k, Gs, F;
    if (!clio__has_maths())
        return 0.0;
    nl = n[0] * l[0] + n[1] * l[1] + n[2] * l[2];
    nv = n[0] * v[0] + n[1] * v[1] + n[2] * v[2];
    if (nl <= 0.0 || nv <= 0.0)
        return 0.0;
    hx = l[0] + v[0];
    hy = l[1] + v[1];
    hz = l[2] + v[2];
    hn = sqrt(hx * hx + hy * hy + hz * hz);
    if (hn == 0.0)
        hn = 1.0;
    nh = (n[0] * hx + n[1] * hy + n[2] * hz) / hn;
    vh = (v[0] * hx + v[1] * hy + v[2] * hz) / hn;
    a = maxd(0.002, rough * rough);
    a2 = a * a;
    dd = nh * nh * (a2 - 1.0) + 1.0;
    D = a2 / (PI * dd * dd);
    k = clio__pypow(rough + 1.0, 2.0) / 8.0;
    Gs = (nl / (nl * (1.0 - k) + k)) * (nv / (nv * (1.0 - k) + k));
    F = f0 + (1.0 - f0) * clio__pypow(1.0 - maxd(0.0, vh), 5.0);
    return D * Gs * F / (4.0 * nl * nv + 1e-4) * nl;
}

static const double NORMALS[8][3] = {{0, 0, 0}, {0.0, 0.0, 1.0}, {0.0, 0.0, -1.0}, {1.0, 0.0, 0.0}, {-1.0, 0.0, 0.0},
                                     {0.0, 1.0, 0.0}, {0.0, -1.0, 0.0}, {0, 0, 0}};

static int64_t ifloor(double v) { return (int64_t)floor(v); }

static int lamp_sees(const clio_lamp *lp, int64_t cx, int64_t cy)
{
    int64_t i = cx - lp->lit_x0, j = cy - lp->lit_y0;
    if (!lp->lit || i < 0 || j < 0 || i >= lp->lit_w || j >= lp->lit_h)
        return 0;
    return lp->lit[j * lp->lit_w + i] != 0;
}

/* Shader._view: the way to the camera from a point */
static void to_eye(const double *cam, double px, double py, double ph, double *v)
{
    double vx = cam[0] - px, vy = cam[1] - py, vh = cam[2] - ph;
    double vn = sqrt(vx * vx + vy * vy + vh * vh);
    if (vn == 0.0)
        vn = 1.0;
    v[0] = vx / vn;
    v[1] = vy / vn;
    v[2] = vh / vn;
}

static double q32(double v, double most, double q) { return (double)(int)(mind(most, v) * q) / q; }

/* Shader.light: the lamps' diffuse light at a place (quarter cells), on a surface of kind k and material mi */
static void light_at(const clio_surface *s, const double *cam, int64_t qx, int64_t qy, int64_t qh, int k, int mi,
                     double *out)
{
    double dr = 0.0, dg = 0.0, db = 0.0;
    if (s->nlamps) {
        double px = (qx + 0.5) / 4.0, py = (qy + 0.5) / 4.0, ph = (qh + 0.5) / 4.0, n[3], metal = s->metal[mi];
        int64_t cx = ifloor(px + 0.5), cy = ifloor(py + 0.5);
        int i;
        if (k == CLIO_SURF_THING)
            to_eye(cam, px, py, ph, n);
        else {
            n[0] = NORMALS[k][0];
            n[1] = NORMALS[k][1];
            n[2] = NORMALS[k][2];
        }
        for (i = 0; i < s->nlamps; i++) {
            const clio_lamp *lp = s->lamps + i;
            double dx, dy, dh, fl, att, d, nl, kk;
            if (!lamp_sees(lp, cx, cy))
                continue;
            dx = lp->x - px;
            dy = lp->y - py;
            dh = lp->h - ph;
            fl = sqrt(dx * dx + dy * dy);
            if (fl >= lp->reach)
                continue;
            att = clio__pypow(1.0 - fl / lp->reach, 2.0) * lp->k;
            d = sqrt(fl * fl + dh * dh);
            if (d == 0.0)
                d = 1e-3;
            nl = (n[0] * dx + n[1] * dy + n[2] * dh) / d;
            kk = att * (0.35 + 0.65 * maxd(0.0, nl)) * (1.0 - 0.55 * metal);
            dr += kk * lp->r;
            dg += kk * lp->g;
            db += kk * lp->b;
        }
    }
    out[0] = q32(dr, 1.6, 32.0);
    out[1] = q32(dg, 1.6, 32.0);
    out[2] = q32(db, 1.6, 32.0);
}

/* Shader.spec: highlights at a place in 1/g of a cell: the lamps' (rgb) and the player's own light's */
static void spec_at(const clio_surface *s, const double *cam, int64_t sx, int64_t sy, int64_t sh, int k, int mi, int g,
                    double *out)
{
    double px = (sx + 0.5) / g, py = (sy + 0.5) / g, ph = (sh + 0.5) / g, v[3], l[3];
    double metal = s->metal[mi], rough = s->rough[mi], f0 = 0.04 + 0.6 * metal, sr = 0.0, sg = 0.0, sb = 0.0;
    double dx, dy, dh, d, own;
    const double *n = NORMALS[k];
    to_eye(cam, px, py, ph, v);
    if (s->nlamps) {
        int64_t cx = ifloor(px + 0.5), cy = ifloor(py + 0.5);
        int i;
        for (i = 0; i < s->nlamps; i++) {
            const clio_lamp *lp = s->lamps + i;
            double fl, att, hl;
            if (!lamp_sees(lp, cx, cy))
                continue;
            dx = lp->x - px;
            dy = lp->y - py;
            dh = lp->h - ph;
            fl = sqrt(dx * dx + dy * dy);
            if (fl >= lp->reach)
                continue;
            att = clio__pypow(1.0 - fl / lp->reach, 2.0) * lp->k;
            d = sqrt(fl * fl + dh * dh);
            if (d == 0.0)
                d = 1e-3;
            l[0] = dx / d;
            l[1] = dy / d;
            l[2] = dh / d;
            hl = clio_ggx(n, l, v, rough, f0) * att * 0.6;
            if (hl > 0.0) {
                sr += hl * lp->r;
                sg += hl * lp->g;
                sb += hl * lp->b;
            }
        }
    }
    dx = s->key[0] - px;
    dy = s->key[1] - py;
    dh = s->key[2] - ph;
    d = sqrt(dx * dx + dy * dy + dh * dh);
    if (d == 0.0)
        d = 1e-3;
    l[0] = dx / d;
    l[1] = dy / d;
    l[2] = dh / d;
    own = clio_ggx(n, l, v, rough, f0) * 0.22;
    out[0] = q32(sr, 2.0, 64.0);
    out[1] = q32(sg, 2.0, 64.0);
    out[2] = q32(sb, 2.0, 64.0);
    out[3] = q32(own, 2.0, 64.0);
}

static int cell_of(const clio_surface *s, int64_t x, int64_t y)
{
    int64_t i = x - s->cells_x0, j = y - s->cells_y0;
    if (!s->cells || i < 0 || j < 0 || i >= s->cells_w || j >= s->cells_h)
        return 0;
    return s->cells[j * s->cells_w + i];
}

static int clamp255(double v)
{
    int i = (int)v;               /* as Python's int(): towards zero */
    if (v >= 255.0)
        return 255;
    return i < 0 ? 0 : i;
}

static int caught(int c, double a, double add, double k)
{
    double m = c * a + add;
    if (!(m < 255.0))
        m = 255.0;
    return (int)(c + (m - c) * k);
}

static uint32_t caught3(uint32_t c, double k)
{
    return RGB(caught(R(c), 1.6, 50.0, k), caught(G(c), 1.6, 56.0, k), caught(B(c), 1.7, 66.0, k));
}

/* surface._h: 0..1, the same for the same numbers */
static double h3(int64_t a, int64_t b, int64_t c)
{
    int64_t k[3];
    uint32_t h = 2166136261u;
    int i;
    k[0] = a;
    k[1] = b;
    k[2] = c;
    for (i = 0; i < 3; i++)
        h = (h ^ (uint32_t)(uint64_t)k[i]) * 16777619u;
    h ^= h >> 13;
    h = h * 1274126177u;
    return (double)(h & 0xFFFFFFu) / (double)0x1000000;
}

/* surface.splashes: raindrops striking the ground open to the sky near the player */
static void splashes(clio_frame *f, const clio_surface *s, const uint8_t *kinds)
{
    int PW = s->pw, PH = s->ph, i, si;
    double ex = s->cx - s->fx * s->z0, ey = s->cy - s->fy * s->z0;
    double now = s->t * 6.0;
    int64_t slot = (int64_t)floor(now);
    int count = (int)(70 * s->amount);
    for (si = 0; si < 2; si++) {
        int64_t sl = slot - si;
        double age = (now - sl) / 2.0;
        if (!(0.0 <= age && age < 1.0))
            continue;
        for (i = 0; i < count; i++) {
            double wx = s->px + (h3(i, sl, 1) - 0.5) * 14.0, wy = s->py + (h3(i, sl, 2) - 0.5) * 14.0;
            double gh, dx, dy, d, z, xf, yf, k, sp;
            int64_t cx = ifloor(wx + 0.5), cy = ifloor(wy + 0.5);
            int c = cell_of(s, cx, cy), x, y, r, xx, j;
            if (!c || !(c & 2) || (c & 4))
                continue;
            gh = s->heights ? s->heights[(cy - s->cells_y0) * s->cells_w + (cx - s->cells_x0)] : 0.0;
            dx = wx - ex;
            dy = wy - ey;
            d = dx * s->fx + dy * s->fy;
            if (d <= s->z0 * 0.6)
                continue;
            xf = floor(s->half + (dx * s->rx + dy * s->ry) * s->lx / d);
            yf = floor(s->yh + (s->eye - gh) * s->ly / d);
            if (!(xf >= 0 && xf < PW && yf >= 0 && yf < PH))
                continue;
            x = (int)xf;
            y = (int)yf;
            z = d - s->z0;
            if (fabs(f->depth[(size_t)y * PW + x] - z) > 0.3 + 0.06 * fabs(z) || kinds[(size_t)y * PW + x] != CLIO_SURF_FLOOR)
                continue;
            k = (1.0 - age) * 0.75;
            if (age < 0.3) {
                f->rgb[(size_t)y * PW + x] = caught3(f->rgb[(size_t)y * PW + x], k);
                if (y > 0 && age < 0.15 && kinds[(size_t)(y - 1) * PW + x] == CLIO_SURF_FLOOR)
                    f->rgb[(size_t)(y - 1) * PW + x] = caught3(f->rgb[(size_t)(y - 1) * PW + x], k * 0.6);
            }
            sp = mind(1.5, 4.0 / d);
            r = 1 + (int)(age * 3.0 * sp);
            for (j = 0; j < 2; j++) {
                xx = j ? x + r : x - r;
                if (xx >= 0 && xx < PW && kinds[(size_t)y * PW + xx] == CLIO_SURF_FLOOR)
                    f->rgb[(size_t)y * PW + xx] = caught3(f->rgb[(size_t)y * PW + xx], k * 0.55);
            }
        }
    }
}

int clio_frame_surface(clio_frame *f, const clio_surface *s)
{
    const clio_more *m = clio__more(f);
    int PW, PH, x, y, *contact, *blurs, *shiny, i;
    double *dirx, *diry, cam[3], d16, d8, ar, ag, ab;
    int64_t tick;
    if (!f || !s || !m || !m->kinds || !m->mats || !m->own || !f->depth || s->pw != f->pw || s->ph != f->ph ||
        s->pw <= 0 || s->ph <= 0 || s->nmat <= 0 || !s->metal || !s->rough || !s->reflect || s->lx == 0.0 ||
        s->ly == 0.0 || s->fade == 0.0 || (s->nlamps && !s->lamps) || s->nlamps < 0)
        return -1;
    if (!clio__has_maths())
        return -2;
    PW = s->pw;
    PH = s->ph;
    dirx = malloc(sizeof(double) * PW);
    diry = malloc(sizeof(double) * PW);
    contact = malloc(sizeof(int) * PW);
    blurs = malloc(sizeof(int) * s->nmat);
    shiny = malloc(sizeof(int) * s->nmat);
    if (!dirx || !diry || !contact || !blurs || !shiny) {
        free(dirx);
        free(diry);
        free(contact);
        free(blurs);
        free(shiny);
        return -3;
    }
    for (x = 0; x < PW; x++) {
        dirx[x] = s->fx + ((x + 0.5 - s->half) / s->lx) * s->rx;
        diry[x] = s->fy + ((x + 0.5 - s->half) / s->lx) * s->ry;
        contact[x] = -1;
    }
    for (i = 0; i < s->nmat; i++) {
        blurs[i] = s->rough[i] < 0.15 ? 0 : s->rough[i] < 0.5 ? 1 : 2;
        shiny[i] = s->metal[i] > 0.05 || s->rough[i] < 0.6;
    }
    cam[0] = s->cx - s->fx * s->z0;
    cam[1] = s->cy - s->fy * s->z0;
    cam[2] = s->eye;
    d16 = s->lx / 24.0;
    d8 = s->lx / 12.0;
    ar = s->amb[0];
    ag = s->amb[1];
    ab = s->amb[2];
    tick = (int64_t)(s->t * 8);
    for (y = 0; y < PH; y++) {
        double sby = (y + 0.5 - s->yh) / s->ly;
        uint32_t *row = f->rgb + (size_t)y * PW;
        const double *dep = f->depth + (size_t)y * PW;
        for (x = 0; x < PW; x++) {
            size_t pi = (size_t)y * PW + x;
            int k = m->kinds[pi], mat = m->mats[pi], mi = mat & 0x7F, glow = (mat & CLIO_GLOW) != 0;
            double z = dep[x], d, wx, wy, h, hq, dl[3], sp[4] = {0.0, 0.0, 0.0, 0.0}, fk, fade, r, g, b, lit_;
            int64_t zq;
            uint32_t c = row[x];
            int have_refl = 0;
            uint32_t src = 0;
            double w = 0.0;
            if (k != CLIO_SURF_FLOOR)
                contact[x] = y;
            if (k == CLIO_SURF_SKY || z >= s->none) {           /* the sky (or what nothing covers) */
                if (!s->has_sky_k && s->cover == 0.0 && s->lift == 0.0)
                    continue;
                r = R(c);
                g = G(c);
                b = B(c);
                if (s->has_sky_k) {
                    r = r * s->sky_k[0];
                    g = g * s->sky_k[1];
                    b = b * s->sky_k[2];
                }
                if (s->cover != 0.0) {
                    r += (s->fog_rgb[0] - r) * s->cover;
                    g += (s->fog_rgb[1] - g) * s->cover;
                    b += (s->fog_rgb[2] - b) * s->cover;
                }
                row[x] = RGB(clamp255(r + s->lift), clamp255(g + s->lift), clamp255(b + s->lift));
                continue;
            }
            zq = z > 0 ? (int64_t)(z * 4) : 0;
            fade = (s->far - zq / 4.0) / s->fade;
            fade = fade < 0.0 ? 0.0 : fade > 1.0 ? 1.0 : fade;
            fk = s->fog != 0.0 ? (double)(int)((1.0 - clio__exp(-(double)zq / 4.0 * s->fog)) * 32) / 32.0 : 0.0;
            if (mi >= s->nmat)
                mi = 0;
            d = z + s->z0;
            wx = cam[0] + d * dirx[x];
            wy = cam[1] + d * diry[x];
            h = s->eye - sby * d;
            if (k == CLIO_SURF_FLOOR || k == CLIO_SURF_CEILING)
                hq = 0;
            else {
                if (k != CLIO_SURF_THING) {                      /* a wall's light is its room's: a hair towards it */
                    wx += NORMALS[k][0] * 0.05;
                    wy += NORMALS[k][1] * 0.05;
                }
                hq = h;
            }
            light_at(s, cam, ifloor(wx * 4), ifloor(wy * 4), ifloor(hq * 4), k, mi, dl);
            if (shiny[mi] && k != CLIO_SURF_THING && !glow) {
                int gg = d < d16 ? 16 : d < d8 ? 8 : 4;
                spec_at(s, cam, ifloor(wx * gg), ifloor(wy * gg), ifloor(hq * gg), k, mi, gg, sp);
            }
            if (k == CLIO_SURF_FLOOR && s->mirrors && contact[x] >= 0) {   /* a floor mirrors what stands on it */
                int64_t ccx = ifloor(wx + 0.5), ccy = ifloor(wy + 0.5);
                int cc = cell_of(s, ccx, ccy);
                double wet = 0.0;
                if (cc)
                    wet = mind(0.9, s->reflect[mi] + s->wet_all + ((cc & 2) ? s->wet_sky : 0.0));
                if (wet > 0.0) {
                    int sy = 2 * contact[x] + 1 - y;
                    if (0 <= sy && sy < y) {
                        int sx = x;
                        double cos_t, fres, ww;
                        if (s->raining && wet > s->reflect[mi]) {     /* rain on the puddles: the image trembles */
                            int64_t jig = ((int64_t)(x >> 1) * 7919 + (int64_t)y * 104729 + tick * 1299709) % 10;
                            if (jig < 0)
                                jig += 10;
                            sx += jig < 2 ? -1 : jig > 7 ? 1 : 0;
                            sx = sx < 0 ? 0 : sx >= PW ? PW - 1 : sx;
                        }
                        cos_t = (s->eye - h) / sqrt(d * d + clio__pypow(s->eye - h, 2.0));
                        fres = 0.04 + 0.96 * clio__pypow(1.0 - cos_t, 5.0);
                        ww = mind(0.4, wet * (0.3 + 0.7 * fres));
                        if (ww >= 1 / 32.0) {
                            int blur = blurs[mi];
                            src = f->rgb[(size_t)sy * PW + sx];
                            if (blur && sy + blur < y) {
                                uint32_t a2 = sy >= blur ? f->rgb[(size_t)(sy - blur) * PW + sx] : src;
                                uint32_t b2 = f->rgb[(size_t)(sy + blur) * PW + sx];
                                src = RGB((R(src) * 2 + R(a2) + R(b2)) >> 2, (G(src) * 2 + G(a2) + G(b2)) >> 2,
                                          (B(src) * 2 + B(a2) + B(b2)) >> 2);
                            }
                            w = (double)(int)(ww * 32) / 32.0;
                            have_refl = 1;
                        }
                    }
                }
            }
            if (glow) {                                         /* it gives its own light */
                r = R(c);
                g = G(c);
                b = B(c);
                lit_ = 1.0;
            } else {
                double a = (k != CLIO_SURF_THING ? m->own[pi] : 256) / 256.0, lr, lg, lb, own, s0, s1, s2;
                lr = a * ar + fade * dl[0];
                lg = a * ag + fade * dl[1];
                lb = a * ab + fade * dl[2];
                r = R(c) * lr;
                g = G(c) * lg;
                b = B(c) * lb;
                lit_ = maxd(maxd(lr, lg), lb);
                own = a * sp[3];
                s0 = fade * sp[0] + own;
                s1 = fade * sp[1] + own;
                s2 = fade * sp[2] + own;
                if (s0 != 0.0 || s1 != 0.0 || s2 != 0.0) {    /* highlights: white off paint, a metal's own colour */
                    double mt = s->metal[mi];
                    r += s0 * (255 * (1 - mt) + mind(255, R(c) * 1.4) * mt);
                    g += s1 * (255 * (1 - mt) + mind(255, G(c) * 1.4) * mt);
                    b += s2 * (255 * (1 - mt) + mind(255, B(c) * 1.4) * mt);
                }
            }
            if (have_refl) {
                r += (R(src) - r) * w;
                g += (G(src) - g) * w;
                b += (B(src) - b) * w;
            }
            r = r + s->lift;
            g = g + s->lift;
            b = b + s->lift;
            if (fk != 0.0) {                                    /* the fog, as bright as the light it is in */
                lit_ = mind(1.0, lit_);
                r += (s->fog_rgb[0] * lit_ - r) * fk;
                g += (s->fog_rgb[1] * lit_ - g) * fk;
                b += (s->fog_rgb[2] * lit_ - b) * fk;
            }
            row[x] = RGB(clamp255(r), clamp255(g), clamp255(b));
        }
    }
    if (s->raining && s->mirrors)
        splashes(f, s, m->kinds);
    free(dirx);
    free(diry);
    free(contact);
    free(blurs);
    free(shiny);
    return 0;
}

/* ---- the picture turned over ---------------------------------------------------------------------------------- */
void clio_frame_flip(clio_frame *f)
{
    size_t n, i;
    if (!f || !f->rgb)
        return;
    n = (size_t)f->pw * f->ph;
    for (i = 0; i < n / 2; i++) {                             /* row PH-1-y backwards is row y: the whole reversed */
        uint32_t t = f->rgb[i];
        f->rgb[i] = f->rgb[n - 1 - i];
        f->rgb[n - 1 - i] = t;
    }
    if (f->depth)
        for (i = 0; i < n / 2; i++) {
            double t = f->depth[i];
            f->depth[i] = f->depth[n - 1 - i];
            f->depth[n - 1 - i] = t;
        }
    if (f->cover && f->sx > 0 && f->sy > 0) {
        size_t m = (size_t)(f->pw / f->sx) * (f->ph / f->sy);
        for (i = 0; i < m / 2; i++) {
            double t = f->cover[i];
            f->cover[i] = f->cover[m - 1 - i];
            f->cover[m - 1 - i] = t;
        }
    }
}

/* ---- the picture turned (firstperson.tilt) -------------------------------------------------------------------- */
static int64_t py_round(double v)                            /* Python's round(): halves to the even neighbour */
{
    double f = floor(v), d = v - f;
    int64_t i = (int64_t)f;
    if (d > 0.5)
        return i + 1;
    if (d < 0.5)
        return i;
    return (i % 2 == 0) ? i : i + 1;
}

int clio_tilt(const uint32_t *src, int w, int h, double ca, double sa, uint32_t dark, uint32_t *out)
{
    double cx, cy;
    int x, y;
    if (!src || !out || w <= 0 || h <= 0)
        return -1;
    cx = (w - 1) / 2.0;
    cy = (h - 1) / 2.0;
    for (y = 0; y < h; y++) {
        double dy = y - cy;
        for (x = 0; x < w; x++) {
            double dx = x - cx;
            int64_t sx = py_round(cx + dx * ca + dy * sa), sy = py_round(cy - dx * sa + dy * ca);
            out[(size_t)y * w + x] = (sx >= 0 && sx < w && sy >= 0 && sy < h) ? src[(size_t)sy * w + sx] : dark;
        }
    }
    return 0;
}
