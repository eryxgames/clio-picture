/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Daniel Sandner
 *
 * What the library's files share among themselves (not exported: the library is built with hidden visibility). */
#ifndef CLIO_PICTURE_INTERNAL_H
#define CLIO_PICTURE_INTERNAL_H

#include "clio_picture.h"

/* room.c: a floor's or ceiling's tile at (ux, uy) of its cell; what is set in a floor (CLIO_CLEAR: nothing, and for
 * teleporters always: their rings are the caller's) */
uint32_t clio__room_texel(const clio_room_cell *c, double ux, double uy);
uint32_t clio__room_fixture(const clio_room_cell *c, double ux, double uy, int64_t phase);

/* model.c: the pow CLIo's Python uses (NULL until clio_maths) */
double clio__pow(double x, double y);
double clio__exp(double x);
double clio__pypow(double x, double y);     /* Python's float ** float (a negative base, 0 ** w) */
int clio__has_maths(void);

/* surface.c: what clio_frame_more handed over beside f on this thread, or NULL */
const clio_more *clio__more(const clio_frame *f);

#endif
