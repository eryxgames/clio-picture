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
 * block of the land's heights at once (CLIo's engine/land.py); braille cells from pixels.
 * 0.2: the open land's ground caster (Land.draw) and its sky (sky_picture).
 * 0.3: the rooms' floor and ceiling (View._backdrop).
 * 0.4: a frame kept here from the floor to the cells: walls, pictures, shadows, models' triangles, colour
 *      changes, hurt, shaking, the cockpit, and the braille read straight from it.
 * 0.5: models projected, shaded and drawn here.
 * 0.6: models kept here as meshes, drawn per instance.
 * 0.7: rooms as faces in a true perspective, with floors and ceilings at heights (CLIo's mesh view).
 * 0.8: surfaces lit by their materials (a G-buffer, lamps, highlights, glow, reflections, wet floors, the rain's
 *      splashes), the mesh view's floors at half width (low detail), the picture turned over, and turned.
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

#define CLIO_PICTURE_VERSION "0.8.0"

/* "0.8.0" */
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

/* ---- 0.2: the open land's ground, as CLIo's engine/land.py Land.draw casts it ---------------------- */

/* One 16 x 16 chunk of the land, cell (x & 15, y & 15) at [(y & 15) * 16 + (x & 15)]: its height (the
 * water's top where it is under), its colour lit by the sun (0xRRGGBB), and its kind (a biome's index,
 * -1 shallow water, -2 deep). */
typedef struct clio_chunk {
    double height[256];
    uint32_t rgb[256];
    int16_t kind[256];
} clio_chunk;

/* Hands the caster the chunk (kx, ky) - built if it was not - or NULL to stop. Called once a chunk a
 * picture; the chunk must stay as it is until clio_land_draw returns. */
typedef const clio_chunk *(*clio_chunk_fn)(void *user, int64_t kx, int64_t ky);

typedef struct clio_ground {
    int pw, ph;                 /* the picture, in pixels */
    int yh;                     /* the horizon's pixel row */
    int step;                   /* 1: a ray a pixel column; 2: a ray every other column (low detail) */
    double half, lw, tall, z0;  /* the middle column, a cell's width and a wall's height at distance 0, the
                                   eye's distance behind the picture */
    double cx, cy, fx, fy, rx, ry;  /* the camera: where, ahead, to the right */
    double eye;                 /* the eye's height (the ground's units) */
    double base;                /* the height of cells a map stands on (stamped) */
    int64_t phase;              /* the beat: the water's glints */
    uint32_t haze;              /* 0xRRGGBB: the far land fades into it */
    int steps;                  /* how many distances the rays step through, near to far */
    const double *d;            /* the distances */
    const double *fog;          /* at each: 1 - exp(-d / haze distance) (worked out by the caller) */
    int stamps;                 /* cells a map stands on: */
    const int64_t *stamp_xy;    /*   x, y of each (2 a stamp) */
    const int32_t *stamp_tex;   /*   its floor's texture, or -1: plain (a checker) */
    const int32_t *tex_w, *tex_h, *tex_at;   /* each texture's size and first texel in texels */
    const uint32_t *texels;     /* 0xRRGGBB */
} clio_ground;

/* The ground and the sky as the player sees them. sky: sky_rows rows of pw pixels, 3 bytes each (r g b),
 * the sky above (rows below it are haze). Out: rgb, pw * ph pixels (0xRRGGBB), and depth, each pixel's
 * distance (1e9 where no ground is). Returns 0; -1 for bad numbers; -2 when chunk() gave NULL; -3 out of
 * memory. */
CLIO_PICTURE_API int clio_land_draw(const clio_ground *g, clio_chunk_fn chunk, void *user,
                                    const uint8_t *sky, int sky_rows, uint32_t *rgb, double *depth);

/* 0.8: clio_land_draw with terraces - nterraces map cells (terrace_xy: x, y of each) standing terrace_h over the base:
 * their floors drawn that high, and the ground blended from them where it meets them (Land.ground) */
CLIO_PICTURE_API int clio_land_terraced(const clio_ground *g, int nterraces, const int64_t *terrace_xy,
                                        const double *terrace_h, clio_chunk_fn chunk, void *user, const uint8_t *sky,
                                        int sky_rows, uint32_t *rgb, double *depth);

/* ---- 0.2: the sky over open cells, as CLIo's engine/firstperson.py sky_picture paints it ------------ */

typedef struct clio_sky {
    int pw, yh;                 /* the picture's width, the horizon's row: yh rows of sky */
    double f;                   /* the way the player faces, 0..3 (a quarter turn is the picture's width) */
    double lateral;             /* how far he has moved sideways (near ridges move a little) */
    int64_t phase;              /* the beat: stars twinkle, clouds drift */
    const uint32_t *base;       /* yh: each row's colour before anything is on it (the gradient) */
    double stars;               /* how many stars, 0: none */
    int clouds;                 /* 0: none */
    uint32_t cloud_rgb;
    double cloud_amount;
    int ridges;                 /* the hills' outlines on the horizon, far to near: */
    const uint32_t *ridge_rgb;
    const double *ridge_height, *ridge_near, *ridge_rough;
    const int64_t *ridge_seed;
    int sun;                    /* 0: none; else the disc and glow are the caller's to draw (they need hypot): */
    double sun_at, sun_height, sun_size, sun_glow;
} clio_sky;

/* The sky, yh rows of pw pixels (0xRRGGBB) into rgb; ridge_rows (ridges * pw, may be NULL) gets each ridge's
 * height in rows at each column. Pixels the sun may reach are left before the sun and before the ridges -
 * their indices (y * pw + x) go into sun_px (room for pw * yh) and their number into *sun_n - so that the
 * caller draws the disc, its glow and then the ridges there. Returns 0, or -1 for bad numbers. */
CLIO_PICTURE_API int clio_sky_draw(const clio_sky *s, uint32_t *rgb, double *ridge_rows, int32_t *sun_px,
                                   int *sun_n);

/* ---- 0.3: the rooms' floor and ceiling, as CLIo's engine/firstperson.py View._backdrop draws them ---- */

#define CLIO_CLEAR 0xFFFFFFFFu  /* a texel that shows what is under it */

/* What is set in a floor (View.fixture), in CLIo's order of precedence */
enum {
    CLIO_FLOOR_PLAIN = 0, CLIO_FLOOR_PLATE = 1, CLIO_FLOOR_PLATE_DOWN = 2, CLIO_FLOOR_SUNK = 3,
    CLIO_FLOOR_TELEPORT = 4,    /* left to the caller (its rings need a hypotenuse) */
    CLIO_FLOOR_LIFT = 5, CLIO_FLOOR_BELT = 6, CLIO_FLOOR_OIL = 7, CLIO_FLOOR_SLICK = 8, CLIO_FLOOR_SCORCHED = 9,
    CLIO_CEILING_SKY = 10       /* a ceiling open to the sky */
};

typedef struct clio_room_cell {
    const uint32_t *tex;        /* the floor's or ceiling's tile (0xRRGGBB or CLIO_CLEAR), row by row; NULL: none */
    int32_t tex_w, tex_h;
    int32_t kind;               /* CLIO_FLOOR_* for a floor, 0 or CLIO_CEILING_SKY for a ceiling */
    int32_t belt;               /* a belt's way: 0 north, 1 east, 2 south, 3 west */
    int32_t toxic, fire;        /* the floor is toxic; it burns */
    int32_t mark;               /* 0 none, 1 fire is coming (hatched), 2 burning now */
    int64_t x, y;               /* the cell's own place (oil and scorch patterns use it) */
} clio_room_cell;

/* Fills *out for the cell (ix, iy): its floor (what = 0) or its ceiling (what = 1). Asked once a cell a
 * picture, in the order CLIo's Python asks; returns 0, or anything else to stop. A tile must stay as it is
 * until clio_room_draw returns. */
typedef int (*clio_room_cell_fn)(void *user, int what, int64_t ix, int64_t iy, clio_room_cell *out);

typedef struct clio_room {
    int pw, ph, yh;             /* the picture in pixels, the horizon's row */
    int step;                   /* 1, or 2: every other column, each drawn twice */
    double half;                /* the middle column */
    double cx, cy, fx, fy, rx, ry;  /* the camera: where, ahead, to the right */
    int64_t phase;
    const int8_t *row_kind;     /* ph: 0 floor, 1 ceiling, 2 too far (haze) */
    const double *row_z;        /* ph: the distance each row sees (log(t) / log(0.64), worked out by the caller) */
    const double *row_fog;      /* ph: how much of a colour is left there (0.18 + 0.82 t^0.75) */
    const double *row_span;     /* ph: a cell's width there, in pixels */
    const uint8_t *sky;         /* the sky's rows (yh rows of pw pixels, r g b), NULL when there is none */
} clio_room;

/* The floor and the ceiling, ph rows of pw pixels (0xRRGGBB) into rgb. Floor pixels of a teleporter with
 * no tile over them are left to the caller: their indices (y * pw + x) go into later (room for pw * ph),
 * their number into *later_n. Returns 0; -1 for bad numbers; -2 when cell() stopped; -3 out of memory. */
CLIO_PICTURE_API int clio_room_draw(const clio_room *r, clio_room_cell_fn cell, void *user, uint32_t *rgb,
                                    int32_t *later, int *later_n);

/* ---- 0.4: a frame kept here from the floor to the cells ---------------------------------------------- */

/* CLIo's picture of a first-person view, owned by the caller: its pixels, and in open land each pixel's
 * distance. Every drawing call below follows the rule of CLIo's Python: a pixel is written only where the
 * land there is further off than what is drawn now (depth > cur_z), when there is a depth. */
typedef struct clio_frame {
    int pw, ph;             /* pixels */
    int sx, sy;             /* pixels to a character cell */
    uint32_t *rgb;          /* pw * ph, 0xRRGGBB */
    double *depth;          /* pw * ph, or NULL: no test (rooms) */
    double cur_z;           /* how far off what is drawn now is */
    double *cover;          /* (ph / sy) * (pw / sx): the nearest wall or thing over each cell, 99 where none */
    double *zbuf;           /* pw * ph: a model's own depth while it is drawn */
    uint32_t *zgen;         /* pw * ph: which model wrote zbuf there */
    uint32_t gen;           /* the model being drawn */
} clio_frame;

/* After clio_land_draw into the frame: the cells the land covers (View.cover), from every other column of
 * each row's pixels at their depth. */
CLIO_PICTURE_API void clio_frame_land_cover(clio_frame *f);

/* A wall's face towards the player (View.front_face): pixels X0..X1-1, Y0..Y1-1 of the tile (tw x th
 * texels, CLIO_CLEAR see-through) laid over x0 + w, y0 + h on the picture, opened that far (a door), each
 * colour times fog; the cells it covers marked at cover_dist. */
CLIO_PICTURE_API void clio_frame_front(clio_frame *f, const uint32_t *tex, int tw, int th, int X0, int X1, int Y0,
                                       int Y1, double x0, double w, double y0, double h, double opened, double fog,
                                       double cover_dist);

/* A wall's side face (View.side_face), column by column: for each of n columns xs[i], its distance zc[i] and
 * fog[i] (worked out by the caller); t = (x + 0.5 - half) / span; v = zc - (z - 0.5) along the tile. */
CLIO_PICTURE_API void clio_frame_side(clio_frame *f, const uint32_t *tex, int tw, int th, int n, const int32_t *xs,
                                      const double *zc, const double *fog, double half, double span, double yh,
                                      double ch, double fs, double opened, double z);

/* How a blitted picture is coloured (View.toner, and a monster's flash, tint and dissolving) */
typedef struct clio_tone {
    int kind;               /* 0: as drawn; 1: times fog; 2: into the haze by k */
    double fog;
    uint32_t haze;
    double k;
    double gone;            /* > 0: dissolving: a texel (i, j) goes where noise(int(i / step), int(j / step), id) < gone */
    double step;
    int64_t id;
    int flash;              /* white */
    int tinted;             /* mixed 0.6 towards tint first */
    uint32_t tint;
} clio_tone;

/* A picture (nw x nh texels, CLIO_CLEAR see-through) scaled by s to sw x sh at (x0, y0) (View.blit); tone may be
 * NULL; with mark, the cells drawn on are marked at dist. */
CLIO_PICTURE_API void clio_frame_blit(clio_frame *f, const uint32_t *pic, int nw, int nh, int x0, int y0, double s,
                                      int sw, int sh, const clio_tone *tone, int mark, double dist);

/* Pixels x_from..x_to, y_from..y_to (inclusive, clipped) darkened: times k (a shadow) */
CLIO_PICTURE_API void clio_frame_darken(clio_frame *f, int x_from, int x_to, int y_from, int y_to, double k);

/* A model starts: its triangles hide each other by their own depth (model.raster's zbuf) */
CLIO_PICTURE_API void clio_frame_model(clio_frame *f);

/* A triangle (model.raster): pts x, y, depth for each corner; cols one colour (smooth 0, cols[0..2]) or one
 * for each corner (smooth 1, cols[0..8]), as numbers 0..255 */
CLIO_PICTURE_API void clio_frame_triangle(clio_frame *f, const double *pts, const double *cols, int smooth);

/* The whole picture through a colour change (CLIo's recolour): 1 times k; 2 inverted (sudo); 3 the TTY
 * goggles; 4 mixed towards colour by k */
CLIO_PICTURE_API void clio_frame_recolour(clio_frame *f, int how, double k, uint32_t colour);

/* Hurt: red from the edges (View.vignette); where: 0 all round, 1 left, 2 right, 3 back */
CLIO_PICTURE_API void clio_frame_vignette(clio_frame *f, double hurt, int where);

/* The picture shaken s pixels sideways, black coming in */
CLIO_PICTURE_API void clio_frame_shake(clio_frame *f, int s);

/* A cockpit's canopy (View.canopy): a frame across the top, two posts slanting in from the bottom corners */
CLIO_PICTURE_API void clio_frame_canopy(clio_frame *f, uint32_t colour);

/* Under the canopy (View.dashboard): the console h rows high from y0, x0..x1, two screens, and the fuel gauge
 * gx0..gx1 at row gy lit up to lit (red when low) - the numbers worked out by the caller */
CLIO_PICTURE_API void clio_frame_dashboard(clio_frame *f, uint32_t colour, int h, int y0, int x0, int x1, int gx0,
                                           int gx1, int gy, int lit, int low);

/* A machine's cage at the edges of the picture (View.cockpit without a canopy) */
CLIO_PICTURE_API void clio_frame_cage(clio_frame *f);

/* ---- 0.5: models, projected, shaded and drawn here (View.draw_model, model.pbr) --------------------------
 * The shading needs pow and the open land's haze exp. The library has none of its own: the caller hands over the
 * ones its Python uses (clio_maths), and CLIo checks clio_shade against model.pbr before it lets the library draw
 * models. Until then the calls below do nothing (clio_frame_model_draw returns -2). */

/* The pow and exp to use */
CLIO_PICTURE_API void clio_maths(double (*pow_fn)(double, double), double (*exp_fn)(double));

typedef struct clio_material {
    double r, g, b;         /* albedo, linear 0..1 */
    double metal, rough, glow;
} clio_material;

typedef struct clio_model_draw {
    int ntri;
    const double *pts;          /* ntri * 9: each corner's world x, y and height */
    const int32_t *corner;      /* ntri * 3: each corner's vertex, for smooth normals (smooth only) */
    const int32_t *mat;         /* ntri: its material */
    const int8_t *own;          /* ntri: 1 where the face has its own colour */
    const double *colour;       /* ntri * 3: that colour (linear 0..1) */
    int nverts;                 /* vertices (smooth) */
    const clio_material *mats;
    int nmats;
    int cull;                   /* 1: faces turned away are not drawn (0 for shards) */
    int smooth;                 /* 1: colours worked out at the corners and blended */
    double cx, cy, fx, fy, rx, ry;  /* the camera */
    double half, lw, yh, fs, ch;    /* the View's numbers */
    double persp;               /* > 0: open land's perspective from that far behind; 0: rooms (0.64 ^ z) */
    double flat, zm;            /* flat > 0: near, projected at the middle's depth zm (View.draw_model) */
    double eye;                 /* View.EYE */
    double light[3];            /* the light, a unit vector */
    int air;                    /* 1: open land: into the haze instead of darker */
    uint32_t haze;
    double vis;
    int flash, tinted;
    uint32_t tint;
} clio_model_draw;

/* A model into the frame (a new depth of its own). out: [shown, top, x min, x max] of what was drawn. Returns 0,
 * -1 for bad numbers, -2 without clio_maths, -3 out of memory. */
CLIO_PICTURE_API int clio_frame_model_draw(clio_frame *f, const clio_model_draw *m, double *out);

/* ---- 0.6: models kept here as meshes, drawn per instance ---------------------------------------------------
 * A model's faces, materials and own colours are handed over once (clio_mesh_new); each time one is drawn only its
 * corners as posed (model space: x right, y forward, z up) and where it stands come over. The library turns them
 * into the world the way View.draw_model does (x' = wx + x * -fy + y * fx, y' = wy + x * fx + y * fy, z' = z + lift,
 * in that order) and draws them with clio_frame_model_draw: the same picture to the last bit. */
typedef struct clio_mesh clio_mesh;

/* faces: ntri * 3 corner indices (0..nverts-1); mat: ntri material indices; own, colour as in clio_model_draw.
 * NULL for bad numbers or no memory. The library keeps copies. */
CLIO_PICTURE_API clio_mesh *clio_mesh_new(int nverts, int ntri, const int32_t *faces, const int32_t *mat,
                                          const int8_t *own, const double *colour, const clio_material *mats,
                                          int nmats);
CLIO_PICTURE_API void clio_mesh_free(clio_mesh *mesh);

typedef struct clio_instance {
    const double *verts;        /* nverts * 3: the corners as posed, in the model's own space */
    double wx, wy;              /* where it stands */
    double fx, fy;              /* the way it faces: cos and sin of its heading */
    double lift;                /* how far above the ground */
} clio_instance;

/* One instance into the frame. m: the camera and View numbers, cull, smooth, flat, zm, light, flash, tint (its
 * triangle fields are not read). Returns as clio_frame_model_draw. */
CLIO_PICTURE_API int clio_frame_mesh_draw(clio_frame *f, const clio_mesh *mesh, const clio_instance *in,
                                          const clio_model_draw *m, double *out);

/* model.pbr: a triangle's colour (0..255) - to check the library against CLIo's Python */
CLIO_PICTURE_API void clio_shade(const clio_material *mat, const double *rgb, const double *n, const double *light,
                                 const double *view, int *out);

/* The rooms' size at distance z (0.64 ^ z) and the open land's haze at d (1 - e^(-d / vis)): to check them too */
CLIO_PICTURE_API double clio_room_scale(double z);
CLIO_PICTURE_API double clio_haze(double d, double vis);

/* ---- 0.7: rooms as faces in a true perspective (CLIo's engine/mesh.py, its mesh view) ------------------------
 * CLIo works out which faces a camera z0 cells behind the player can see - each open cell's floor and ceiling at its
 * own heights, the walls' faces turned to it, the steps and lintels between cells - and hands them over in the order
 * it would draw them. Each face is clipped z0 - near... in front of the camera, projected, and filled row by row: a
 * pixel's ray meets the face's plane, which gives its texel, its distance ahead of the player (kept in the frame's
 * depth: the first face to a distance keeps a pixel) and its light - open land's fog times the face's own (side on:
 * `side`), faded out over the last `fade` cells before `far`, in 256 steps. The fog needs pow: clio_maths first. */
enum { CLIO_FACE_FLOOR = 0, CLIO_FACE_CEILING = 1, CLIO_FACE_XWALL = 2, CLIO_FACE_YWALL = 3 };

typedef struct clio_rooms {
    int pw, ph;                     /* the picture, as the frame's */
    double half, yh;                /* the middle column, the horizon's row */
    double lx, ly;                  /* pixels to a cell across, to a wall's height, z0 cells off */
    double cx, cy, fx, fy, rx, ry;  /* the player, the way he faces, to his right */
    double z0, eye;                 /* the camera this far behind him; the eye's height */
    double far, fade, near, side;   /* engine/mesh.py's FAR, FADE, NEAR, SIDE */
    double none;                    /* the distance of a pixel no face covers (NONE) */
    int64_t phase;
    const uint8_t *sky;             /* the sky's rows (sky_rows rows of pw pixels, r g b), NULL: none */
    int sky_rows;
    int nfaces;
    const int32_t *ints;            /* nfaces * 9: kind (CLIO_FACE_*), side (+1 or -1: an upright face's way), tile (an
                                       index into texels, -1 none), wall (1: counts for the frame's cover), and for a
                                       floor or ceiling its clio_room_cell's kind, belt, toxic, fire and mark */
    const double *geo;              /* nfaces * 7: at, lo, hi, h0, h1, top, opened (engine/mesh.py's Face) */
    const int64_t *xy;              /* nfaces * 4: a floor's or ceiling's cell (ix, iy), then its pattern's place
                                       (clio_room_cell's x, y) */
    int ntex;
    const uint32_t *const *texels;  /* ntex tiles, row by row (CLIO_CLEAR see-through) */
    const int32_t *tw, *th;
} clio_rooms;

/* The rooms into the frame (started with a depth): every pixel and its distance - HAZE and `none` where no face is -
 * and the frame's cover from the walls' pixels. A teleporter's floor with no tile over it is left to the caller: the
 * pixel's index and its face's (2 numbers each) go into later (room for 2 * pw * ph), their number into *later_n; the
 * pixel's distance is set. Returns 0; -1 for bad numbers; -2 without clio_maths; -3 out of memory. */
CLIO_PICTURE_API int clio_frame_rooms(clio_frame *f, const clio_rooms *r, int32_t *later, int *later_n);

/* ---- 0.8: surfaces lit by their materials (CLIo's engine/surface.py) ----------------------------------------
 * A frame can keep, beside its pixels, what each pixel shows (its kind), its material and the light the player's
 * own view gives it (a G-buffer): clio_frame_rooms then draws the surfaces' own colours and fills it, the things
 * drawn after mark it, and clio_frame_surface lights the picture by it. The frame itself is as 0.4 made it: what 0.8
 * adds is handed over beside it (clio_frame_more), for the calls made on this thread, until it is taken back (NULL).
 * Square roots are sqrt's (correctly rounded, as Python's); pow and exp are clio_maths'. */
enum { CLIO_SURF_SKY = 0, CLIO_SURF_FLOOR = 1, CLIO_SURF_CEILING = 2, CLIO_SURF_WALL_PX = 3, CLIO_SURF_WALL_NX = 4,
       CLIO_SURF_WALL_PY = 5, CLIO_SURF_WALL_NY = 6, CLIO_SURF_THING = 7 };
#define CLIO_GLOW 0x80          /* in a pixel's material: its texel gives light of its own */

typedef struct clio_more {
    uint8_t *kinds, *mats, *own;    /* pw * ph each: what each pixel shows (CLIO_SURF_*), its material (| CLIO_GLOW),
                                       the player's view's light on it (0..255) - or NULL: no G-buffer */
    int marking;                    /* 1 while things are drawn (pictures, triangles, shadows): what is drawn now... */
    int thing;                      /* ...stands there - kind THING, its distance kept, drawn where the pixel is as far
                                       or further (>= cur_z) - or (0) marks what is there (a shadow: > cur_z) */
    int glow;                       /* ...gives light of its own (a shot, a flame; a model lit already) */
    int nfaces;                     /* clio_frame_rooms: each face's material (NULL: all 0) */
    const int32_t *face_mat;
    int nmat;
    const int32_t *glow_at;         /* nmat + 1: where each material's glowing colours start in glow_rgb */
    const uint32_t *glow_rgb;       /* 0xRRGGBB */
    int low;                        /* clio_frame_rooms: floors and ceilings at half width (CLIo's low detail) */
} clio_more;

/* Hands m over beside f for the calls this thread makes next (NULL: taken back) */
CLIO_PICTURE_API void clio_frame_more(clio_frame *f, clio_more *m);

typedef struct clio_lamp {
    double x, y, h;                 /* where its light comes from */
    double r, g, b;                 /* its colour, 0..1 */
    double reach, k;                /* cells; its power times how it burns now (flicker, pulse) */
    int64_t lit_x0, lit_y0;         /* the cells it sees: lit_w * lit_h bytes from lit (non-zero: seen) */
    int lit_w, lit_h;
    const uint8_t *lit;
} clio_lamp;

typedef struct clio_surface {
    int pw, ph;                     /* the picture, as the frame's */
    double half, yh, lx, ly;        /* as clio_rooms' */
    double cx, cy, fx, fy, rx, ry, z0, eye;
    double far, fade;               /* engine/mesh.py's FAR, FADE */
    double none;                    /* the distance of a pixel no face covers */
    double t;                       /* the beat and the moment in it */
    double amb[3];                  /* the ambient light */
    int nlamps;
    const clio_lamp *lamps;
    double key[3];                  /* the player's own light */
    int nmat;
    const double *metal, *rough, *reflect;
    double fog, cover, lift, amount, wet_sky, wet_all;
    double fog_rgb[3];              /* the fog's colour, lit */
    int has_sky_k;
    double sky_k[3];                /* the sky darkened (night) */
    int raining, mirrors;           /* rain or a storm; reflections and splashes (not in low detail) */
    double px, py;                  /* the player (where the splashes fall round) */
    int64_t cells_x0, cells_y0;     /* map cells, cells_w * cells_h: 0 none, 1 a cell, 2 under open sky, | 4 solid */
    int cells_w, cells_h;
    const uint8_t *cells;
    const double *heights;          /* their floors' heights */
} clio_surface;

/* The frame's picture lit by its G-buffer (clio_frame_more's): ambient times the view's own light, lamps by how far
 * across and how the surface is turned to them, GGX highlights, glowing texels as they are, floors' dim reflections,
 * fog and lightning, then the rain's splashes - engine/surface.py's shade(). Returns 0; -1 for bad numbers or no
 * G-buffer; -2 without clio_maths. */
CLIO_PICTURE_API int clio_frame_surface(clio_frame *f, const clio_surface *s);

/* GGX's highlight (surface.ggx): n, l, v unit vectors */
CLIO_PICTURE_API double clio_ggx(const double *n, const double *l, const double *v, double rough, double f0);

/* The picture turned half round, upside down and left to right, its cover with it (View.turned_over) */
CLIO_PICTURE_API void clio_frame_flip(clio_frame *f);

/* Pixels (w x h, 0xRRGGBB) turned about their middle (firstperson.tilt): out's pixel (x, y) is src's at
 * (round(cx + dx ca + dy sa), round(cy - dx sa + dy ca)) - Python's round, halves to even - or dark outside. ca, sa:
 * the angle's cosine and sine (the caller's). Returns 0; -1 for bad numbers. */
CLIO_PICTURE_API int clio_tilt(const uint32_t *src, int w, int h, double ca, double sa, uint32_t dark, uint32_t *out);

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

/* clio_dots on a frame's pixels (0xRRGGBB, 2 * cols by 4 * rows) - 0.4 */
CLIO_PICTURE_API int clio_dots32(const uint32_t *rgb, int cols, int rows, const clio_dots_style *style,
                                 uint8_t *out);

#ifdef __cplusplus
}
#endif

#endif
