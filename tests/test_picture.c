/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2024-2026 Daniel Sandner
 *
 * The library against values CLIo's Python gives (engine/land.py), compared to the last bit. */
#include "clio_picture.h"

#include <stdio.h>
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

static int bad = 0;

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
    if (strcmp(clio_picture_version(), CLIO_PICTURE_VERSION) != 0) {
        printf("FAIL version\n");
        bad++;
    }
    printf(bad ? "%d failed\n" : "all as CLIo's Python has them\n", bad);
    return bad ? 1 : 0;
}
