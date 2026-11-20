/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2024-2026 Daniel Sandner
 *
 * CLIo Picture: the inner loops of CLIo's pictures, in C.
 *
 * CLIo (https://github.com/eryxgames/CLIo) draws in pure Python and stays the reference: every
 * function here gives what CLIo's Python gives, to the last bit, so a game looks and plays the
 * same with or without this library. Floats use + - * / and floor only, compiled without fused
 * multiply-add (-ffp-contract=off, /fp:strict), as Python rounds them.
 *
 * 0.1: the land's noise - the hash of a lattice point, value noise, fractal noise (fBm) and a
 * block of the land's heights at once (CLIo's engine/land.py).
 */
#ifndef CLIO_PICTURE_H
#define CLIO_PICTURE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CLIO_PICTURE_STATIC)
#  define CLIO_PICTURE_API
#elif defined(_WIN32)
#  if defined(CLIO_PICTURE_BUILD)
#    define CLIO_PICTURE_API __declspec(dllexport)
#  else
#    define CLIO_PICTURE_API __declspec(dllimport)
#  endif
#else
#  define CLIO_PICTURE_API __attribute__((visibility("default")))
#endif

#define CLIO_PICTURE_VERSION "0.1.0"

/* "0.1.0" */
CLIO_PICTURE_API const char *clio_picture_version(void);

/* A lattice point's value, 0..1 (land._h): a 32-bit hash of the point and the seed. */
CLIO_PICTURE_API double clio_hash(int64_t ix, int64_t iy, int64_t seed);

/* Smooth noise, 0..1, about one bump a unit (land.value). */
CLIO_PICTURE_API double clio_value(double x, double y, int64_t seed);

/* Noise of noises (land.fbm): octaves, each `rough` as strong as the last. */
CLIO_PICTURE_API double clio_fbm(double x, double y, int64_t seed, int octaves, double rough);

/* The land's shape (land.Land): its numbers, as CLIo keeps them. */
typedef struct clio_land {
    int64_t seed;     /* 0..65535 */
    double scale;     /* cells across a hill */
    double warp;      /* 0..1, 0: none */
    double rough;     /* each octave this strong */
    double ridges;    /* 0..1, 0: none */
} clio_land;

/* The raw height, 0..1, of each cell (x, y) for x0 <= x < x0 + w, y0 <= y < y0 + h, row by row,
 * into out[w * h] (land.Land.raw). Returns 0, or -1 when w or h is not positive. */
CLIO_PICTURE_API int clio_land_raw(const clio_land *land, int64_t x0, int64_t y0, int w, int h, double *out);

/* Braille cells from pixels (CLIo's dots and dots-fill styles, engine/firstperson.py View.braille).
 * A cell is 2 pixels across and 4 down; its dots are Unicode braille U+2800 + bits. */
typedef struct clio_dots_style {
    int fill;           /* 0: dots - each pixel lit or not, dithered (Bayer 2 x 4), on no background;
                           1: dots-fill - the brighter pixels in one colour, the rest in another */
    double steps[8];    /* dots: a pixel is lit when its luminance >= steps[its Bayer value 0..7] */
    double reach;       /* dots: the lit colour is brightened so its brightest channel nears this (215) */
    double most;        /* dots: ... but at most this many times (1.9) */
    double flat;        /* fill: a cell whose luminances span less than this is one plain colour (9) */
} clio_dots_style;

/* What each cell came to, 8 bytes: dots (the bits), kind, the dots' colour (r g b), the background's
 * (r g b). Kind: CLIO_BLANK (no dots, no colour), CLIO_PLAIN (no dots, the background's colour),
 * CLIO_DOTS (dots in their colour, no background), CLIO_TWO (dots and a background). */
enum { CLIO_BLANK = 0, CLIO_PLAIN = 1, CLIO_DOTS = 2, CLIO_TWO = 3 };

/* rgb: 2 * cols by 4 * rows pixels, 3 bytes each (r g b), row by row. out: cols * rows cells of 8 bytes,
 * row by row. Luminance is 0.2126 r + 0.7152 g + 0.0722 b; averages are whole numbers, rounded down.
 * Returns 0, or -1 when cols or rows is not positive. */
CLIO_PICTURE_API int clio_dots(const uint8_t *rgb, int cols, int rows, const clio_dots_style *style,
                               uint8_t *out);

#ifdef __cplusplus
}
#endif

#endif
