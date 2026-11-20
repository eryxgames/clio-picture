/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2024-2026 Daniel Sandner
 *
 * Braille cells from pixels, as CLIo's engine/firstperson.py View.braille makes them. */
#define CLIO_PICTURE_BUILD
#include "clio_picture.h"

#include <stddef.h>

static const uint8_t BIT[4][2] = {{0x01, 0x08}, {0x02, 0x10}, {0x04, 0x20}, {0x40, 0x80}};   /* [row][column] */
static const uint8_t BAYER[4][2] = {{0, 4}, {6, 2}, {1, 5}, {7, 3}};

static double lum(const uint8_t *p)
{
    return 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
}

/* The whole-number average of the pixels picked by `mask` (bit i: the i-th of the cell's 8). */
static void average(const uint8_t *const px[8], unsigned mask, uint8_t *out)
{
    unsigned r = 0, g = 0, b = 0, n = 0, i;
    for (i = 0; i < 8; i++)
        if (mask & (1u << i)) {
            r += px[i][0];
            g += px[i][1];
            b += px[i][2];
            n++;
        }
    out[0] = (uint8_t)(r / n);
    out[1] = (uint8_t)(g / n);
    out[2] = (uint8_t)(b / n);
}

static uint8_t brightened(uint8_t c, double f)
{
    double v = c * f;
    return v >= 255.0 ? 255 : (uint8_t)v;
}

static int dots(const uint8_t *rgb8, const uint32_t *rgb32, int cols, int rows, const clio_dots_style *style,
                uint8_t *out)
{
    int r, c, i;
    size_t stride;
    if (cols <= 0 || rows <= 0 || (!rgb8 && !rgb32) || !style || !out)
        return -1;
    stride = (size_t)cols * 2;                          /* pixels a row */
    for (r = 0; r < rows; r++)
        for (c = 0; c < cols; c++) {
            uint8_t pix[8][3];
            const uint8_t *px[8];
            uint8_t bits[8];
            uint8_t *o = out + ((size_t)r * cols + c) * 8;
            unsigned lit = 0;
            uint8_t cellbits = 0;
            for (i = 0; i < 8; i++) {           /* in CLIo's order: down the rows, across the two columns */
                int dy = i / 2, dx = i % 2;
                size_t p = ((size_t)r * 4 + dy) * stride + ((size_t)c * 2 + dx);
                if (rgb8) {
                    const uint8_t *q = rgb8 + p * 3;
                    pix[i][0] = q[0];
                    pix[i][1] = q[1];
                    pix[i][2] = q[2];
                } else {
                    uint32_t v = rgb32[p];
                    pix[i][0] = (uint8_t)(v >> 16);
                    pix[i][1] = (uint8_t)(v >> 8);
                    pix[i][2] = (uint8_t)v;
                }
                px[i] = pix[i];
                bits[i] = BIT[dy][dx];
            }
            for (i = 0; i < 8; i++)
                o[i] = 0;
            if (style->fill) {
                double ls[8], lo, hi, mid;
                ls[0] = lo = hi = lum(px[0]);
                for (i = 1; i < 8; i++) {
                    ls[i] = lum(px[i]);
                    if (ls[i] < lo)
                        lo = ls[i];
                    if (ls[i] > hi)
                        hi = ls[i];
                }
                if (hi - lo < style->flat) {
                    o[1] = CLIO_PLAIN;
                    average(px, 0xFFu, o + 5);
                    continue;
                }
                mid = (lo + hi) / 2;
                for (i = 0; i < 8; i++)
                    if (ls[i] > mid) {
                        cellbits |= bits[i];
                        lit |= 1u << i;
                    }
                o[0] = cellbits;
                o[1] = CLIO_TWO;
                average(px, lit, o + 2);
                average(px, ~lit & 0xFFu, o + 5);
            } else {
                uint8_t mean[3], top;
                double f;
                for (i = 0; i < 8; i++)
                    if (lum(px[i]) >= style->steps[BAYER[i / 2][i % 2]]) {
                        cellbits |= bits[i];
                        lit |= 1u << i;
                    }
                if (!lit)
                    continue;
                average(px, lit, mean);
                top = mean[0] > mean[1] ? mean[0] : mean[1];
                top = top > mean[2] ? top : mean[2];
                f = style->reach / (top > 1 ? top : 1);
                if (f > style->most)
                    f = style->most;
                o[0] = cellbits;
                o[1] = CLIO_DOTS;
                o[2] = brightened(mean[0], f);
                o[3] = brightened(mean[1], f);
                o[4] = brightened(mean[2], f);
            }
        }
    return 0;
}

int clio_dots(const uint8_t *rgb, int cols, int rows, const clio_dots_style *style, uint8_t *out)
{
    return rgb ? dots(rgb, NULL, cols, rows, style, out) : -1;
}

int clio_dots32(const uint32_t *rgb, int cols, int rows, const clio_dots_style *style, uint8_t *out)
{
    return rgb ? dots(NULL, rgb, cols, rows, style, out) : -1;
}
