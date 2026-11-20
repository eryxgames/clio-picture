"""Writes tests/cases.h: what CLIo's Python draws for a made-up land and sky, as checksums, for test_picture.c.

    python3 tests/make_cases.py PATH/TO/CLIo > tests/cases.h

The land's chunks and the sky's colours come from simple formulas that test_picture.c repeats in C; the
numbers Python works out with exp or a power (the haze at each step, the sky's gradient) are written out."""
import math
import os
import sys

CLIO = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..", "clio")
sys.path.insert(0, os.path.abspath(CLIO))
os.environ["CLIO_PICTURE_LIB"] = "none"                     # the Python's own picture

from engine import firstperson, land as land_mod  # noqa: E402
from engine.frame import mix  # noqa: E402

M64 = (1 << 64) - 1


def fnv(h, data):
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & M64
    return h


def u32(c):
    return (c[0] << 16) | (c[1] << 8) | c[2]


def sum_rgb(h, colours):
    for c in colours:
        h = fnv(h, u32(c).to_bytes(4, "little"))
    return h


def sum_doubles(h, values):
    import struct
    for v in values:
        h = fnv(h, struct.pack("<d", v))
    return h


# ---- the land: chunks from a formula (as test_picture.c's made_chunk) ---------------------------------- #
def cell(x, y):
    h = 0.25 * ((x * 3 + y * 5) % 17)
    kind = -2 if (x * x + y) % 23 == 0 else -1 if (x + 2 * y) % 19 == 0 else (x + y) % 3
    return h, ((x * 37) & 255, (y * 53) & 255, ((x + y) * 11) & 255), kind


class Made(land_mod.Land):
    def build(self, kx, ky):
        hs, cs, ks = [], [], []
        for y in range(ky * 16, ky * 16 + 16):
            for x in range(kx * 16, kx * 16 + 16):
                h, c, k = cell(x, y)
                hs.append(h)
                cs.append(c)
                ks.append(k)
        ch = self.chunks[(kx, ky)] = (hs, cs, ks, [False] * 256, [None] * 256)
        return ch


class View:
    def __init__(self, PW, PH, yh, LW, TALL, detail):
        self.PW, self.PH, self.yh, self.half, self.LW, self.TALL, self.detail = PW, PH, yh, PW / 2.0, LW, TALL, detail


def sky_rows(PW, n):
    return [[((x * 5 + y) & 255, (y * 9) & 255, (x * 3 + 100) & 255) for x in range(PW)] for y in range(n)]


TEX = [[(10, 20, 30), (40, 50, 60), None], [None, (70, 80, 90), (200, 10, 10)]]
STAMPS = [(3, -6, -1), (4, -6, 0), (5, -6, 0), (2, -9, -1), (0, -3, 0), (-1, -3, -1), (0, -2, 0)]   # x, y, texture
CASES = [   # PW, PH, yh, LW, TALL, detail, cam, eye, phase, sky rows
    (64, 40, 16, 40.0, 60.0, "high", (0.0, 0.0, 0, -1, 1, 0), 1.2, 3, 16),
    (51, 30, 12, 31.5, 44.0, "low", (2.5, -7.25, 1, 0, 0, 1), 2.9, 11, 5),
]


def land_cases():
    out = []
    for PW, PH, yh, LW, TALL, detail, cam, eye, phase, nsky in CASES:
        land_mod._SHARED.clear()
        land = Made({"size": [64, 64], "view": 30, "haze": 20, "seed": 1})
        land.haze = (70, 80, 90)
        land.base = 1.5
        land.stamp = {(x, y): (TEX if t == 0 else None) for x, y, t in STAMPS}
        land.stamp_chunks = {(x >> 4, y >> 4) for (x, y) in land.stamp}
        rows, depth = land.cast(View(PW, PH, yh, LW, TALL, detail), cam, eye, phase, sky_rows(PW, nsky))
        h = 0xCBF29CE484222325
        for r in rows:
            h = sum_rgb(h, r)
        for d in depth:
            h = sum_doubles(h, d)
        ds, fog = land.steps()
        out.append((PW, PH, yh, LW, TALL, 2 if detail == "low" else 1, cam, eye + land.base, phase, nsky, ds, fog, h))
    return out


# ---- the sky ---------------------------------------------------------------------------------------------- #
SKIES = [
    {"stars": 0.3, "clouds": {"color": "#403050", "amount": 0.6},
     "ridges": [{"height": 0.25, "seed": 3}, {"height": 0.12, "near": 1, "seed": 9, "rough": 0.9}]},
    {"top": "#000010", "horizon": "#c46a44", "clouds": {"amount": 0.4},
     "sun": {"at": 0.18, "height": 0.35, "size": 0.18, "bands": "#d0a070", "glow": 0.5},
     "ridges": [{"height": 0.3, "seed": 5, "near": 0.5}]},
]
SKY_VIEWS = [(90, 20, 1, -13.25, 7), (61, 17, 1, 4.5, 0)]   # PW, yh, f, lateral, phase


def sky_cases():
    out = []
    for i, raw in enumerate(SKIES):
        spec = firstperson.sky_spec(raw)
        PW, yh, f, lateral, phase = SKY_VIEWS[i]
        top, hor = spec["top"], spec["horizon"]
        base = [mix(hor, top, min(1.0, ((yh - y - 0.5) / float(yh)) ** 0.55)) for y in range(yh)]
        tint = [mix(col, hor, 0.3 * (1 - near)) for col, height, seed, near, rough in spec["ridges"]]
        rows = firstperson.sky_picture(spec, PW, yh, f, lateral, phase)
        sun = spec["sun"]
        spots = set()
        if sun is not None:                                   # the pixels the C leaves for the sun
            at, sh, size, scol, glow, bands = sun
            r = size * yh
            reach = (r * 3.5 if glow != 0.0 else r) * 1.000001 + 1e-9
            for y in range(yh):
                e = (yh - y - 0.5) / float(yh)
                for x in range(PW):
                    u = (f + (x + 0.5) / PW - 0.5) / 4.0
                    du = ((u - at + 0.5) % 1.0 - 0.5) * 4 * PW
                    dv = (e - sh) * yh
                    if du * du + dv * dv <= reach * reach:
                        spots.add(y * PW + x)
        h = 0xCBF29CE484222325
        for y in range(yh):
            for x in range(PW):
                if y * PW + x not in spots:
                    h = sum_rgb(h, [rows[y][x]])
        hs = 0xCBF29CE484222325
        for col, height, seed, near, rough in spec["ridges"]:
            ridge = []
            for x in range(PW):
                u = (f + (x + 0.5) / PW - 0.5) / 4.0 + -lateral * near * 0.006
                v = (0.55 * firstperson._wave(u, 9, seed) + 0.3 * firstperson._wave(u, 23, seed + 1)
                     + 0.15 * rough * firstperson._wave(u, 61, seed + 2))
                ridge.append(v * height * yh)
            hs = sum_doubles(hs, ridge)
        out.append((spec, PW, yh, f, lateral, phase, base, tint, h, hs, sorted(spots)))
    return out


# ---- a room: cells from formulas (as test_picture.c's room_cell) ---------------------------------------- #
TILE_A = [[(200, 40, 40), None, (40, 200, 40)], [None, (40, 40, 200), (90, 90, 90)]]
TILE_B = [[(10, 10, 10), (250, 250, 0)], [(0, 250, 250), None]]


class Cell:
    plate = sunk = teleport = lift = conveyor = None
    oil = slick = scorched = toxic = sky = down = False
    floor = ceiling = None


def room_cell(ix, iy):
    if (ix * 5 + iy * 3) % 31 == 0:
        return None
    c = Cell()
    c.x, c.y = ix, iy
    k = (ix * 7 + iy * 11) % 23
    if k == 0:
        c.plate, c.down = "p", bool(ix & 1)
    elif k == 1:
        c.sunk = True
    elif k == 2:
        c.teleport = "t"
    elif k == 3:
        c.lift = "l"
    elif k == 4:
        c.conveyor = (ix + iy) % 4
    elif k == 5:
        c.oil = True
    elif k == 6:
        c.slick = True
    elif k == 7:
        c.scorched = True
    c.toxic = (ix + 2 * iy) % 13 == 0
    c.floor = "tile_a" if (ix - iy) % 9 == 0 else None
    c.ceiling = "tile_b" if (ix + iy) % 7 == 0 else None
    c.sky = (ix * iy) % 17 == 5
    return c


class Marks:
    def get(self, key, default=None):
        k = (key[0] * 3 + key[1]) % 19
        return "warn" if k == 1 else "burn" if k == 2 else "other" if k == 3 else default


class Fires:
    def __contains__(self, key):
        return (key[0] + key[1] * 5) % 11 == 0


class Room:
    tiles = {"tile_a": [TILE_A], "tile_b": [TILE_B]}
    floor_tile = None
    ceiling_tile = None
    marks = Marks()
    fires = Fires()

    def cell(self, x, y):
        return room_cell(x, y)


def room_sky(PW, yh):
    return [[((x * 11 + y) & 255, (x + y * 13) & 255, 200) for x in range(PW)] for y in range(yh)]


ROOMS = [   # W, VR, style, detail, cam x, cam y, facing, phase
    (40, 12, "pixel", "high", 5.0, 7.0, 0, 3),
    (31, 9, "dots", "low", -3.5, 12.25, 1, 4),
]


def room_cases():
    out = []
    for W, VR, style, detail, cx, cy, f, phase in ROOMS:
        v = firstperson.View(W, VR, style, detail)
        v.set_eye(None)
        sky = room_sky(v.PW, v.yh)
        v.sky_rows = lambda *a, sky=sky: sky
        lv = Room()
        rows = v._backdrop(lv, cx, cy, f, phase)
        later = set()
        fx, fy = firstperson.DIRS[f]
        rx, ry = firstperson.DIRS[(f + 1) % 4]
        step = 2 if v.detail == "low" else 1
        kinds, zs, fogs, spans = v._rows()
        for y in range(v.PH):
            if kinds[y] != 0:
                continue
            for x in range(0, v.PW, step):
                a = (x + 0.5 * step - v.half) / spans[y]
                wx, wy = cx + fx * zs[y] + rx * a, cy + fy * zs[y] + ry * a
                ix, iy = int(math.floor(wx + 0.5)), int(math.floor(wy + 0.5))
                c = room_cell(ix, iy)
                if c is None or not c.teleport or c.plate or c.sunk:
                    continue
                tile = c.floor or (None if c.toxic else lv.floor_tile)
                if tile:
                    tex = lv.tiles[tile][0]
                    th, tw = len(tex), len(tex[0])
                    if tex[min(th - 1, int((wy - iy + 0.5) * th))][min(tw - 1, int((wx - ix + 0.5) * tw))] is not None:
                        continue
                later.add(y * v.PW + x)
                if step == 2 and x + 1 < v.PW:
                    later.add(y * v.PW + x + 1)
        h = 0xCBF29CE484222325
        for y in range(v.PH):
            for x in range(v.PW):
                if y * v.PW + x not in later:
                    h = sum_rgb(h, [rows[y][x]])
        out.append((v, cx, cy, f, phase, step, (kinds, zs, fogs, spans), h, len(later), min(later) if later else -1))
    return out


def arr(kind, name, values, fmt=repr):
    return "static const %s %s[%d] = {%s};" % (kind, name, max(1, len(values)), ", ".join(fmt(v) for v in values) or "0")


def main():
    w = sys.stdout.write
    w("/* Made by tests/make_cases.py from CLIo's Python: do not edit. */\n\n")
    stamps = STAMPS
    w(arr("int64_t", "STAMP_XY", [v for x, y, t in stamps for v in (x, y)], str) + "\n")
    w(arr("int32_t", "STAMP_TEX", [t for x, y, t in stamps], str) + "\n")
    w("static const int32_t TEX_W[1] = {%d}, TEX_H[1] = {%d}, TEX_AT[1] = {0};\n" % (len(TEX[0]), len(TEX)))
    w(arr("uint32_t", "TEXELS", [u32(c or (60, 60, 64)) for row in TEX for c in row], hex) + "\n\n")
    for i, (PW, PH, yh, LW, TALL, step, cam, eye, phase, nsky, ds, fog, h) in enumerate(land_cases()):
        w(arr("double", "LAND%d_D" % i, ds) + "\n")
        w(arr("double", "LAND%d_FOG" % i, fog) + "\n")
        w("static const clio_ground LAND%d = {%d, %d, %d, %d, %r, %r, %r, %r, %s, %r, %r, %d, 0x%06Xu, %d, "
          "LAND%d_D, LAND%d_FOG, %d, STAMP_XY, STAMP_TEX, TEX_W, TEX_H, TEX_AT, TEXELS};\n" % (
              i, PW, PH, yh, step, PW / 2.0, LW, TALL, land_mod.Z0, ", ".join(repr(float(v)) for v in cam), eye, 1.5,
              phase, u32((70, 80, 90)), len(ds), i, i, len(stamps)))
        w("#define LAND%d_SKY %d\n#define LAND%d_SUM 0x%016XULL\n\n" % (i, nsky, i, h))
    for i, (v, cx, cy, f, phase, step, (kinds, zs, fogs, spans), h, nlater, first) in enumerate(room_cases()):
        fx, fy = firstperson.DIRS[f]
        rx, ry = firstperson.DIRS[(f + 1) % 4]
        w(arr("int8_t", "ROOM%d_KIND" % i, list(kinds), str) + "\n")
        w(arr("double", "ROOM%d_Z" % i, list(zs)) + "\n")
        w(arr("double", "ROOM%d_FOG" % i, list(fogs)) + "\n")
        w(arr("double", "ROOM%d_SPAN" % i, list(spans)) + "\n")
        w(arr("uint8_t", "ROOM%d_SKY" % i, [b for row in room_sky(v.PW, v.yh) for c in row for b in c], str) + "\n")
        w("static const clio_room ROOM%d = {%d, %d, %d, %d, %r, %r, %r, %r, %r, %r, %r, %d, ROOM%d_KIND, ROOM%d_Z, "
          "ROOM%d_FOG, ROOM%d_SPAN, ROOM%d_SKY};\n" % (i, v.PW, v.PH, v.yh, step, v.half, float(cx), float(cy),
                                                     float(fx), float(fy), float(rx), float(ry), phase, i, i, i, i, i))
        w("#define ROOM%d_SUM 0x%016XULL\n#define ROOM%d_LATER %d\n#define ROOM%d_FIRST_LATER %d\n\n" % (
            i, h, i, nlater, i, first))
    for i, (spec, PW, yh, f, lateral, phase, base, tint, h, hs, spots) in enumerate(sky_cases()):
        ridges = spec["ridges"]
        w(arr("uint32_t", "SKY%d_BASE" % i, [u32(c) for c in base], hex) + "\n")
        w(arr("uint32_t", "SKY%d_RIDGE_RGB" % i, [u32(c) for c in tint], hex) + "\n")
        w(arr("double", "SKY%d_RIDGE_H" % i, [r[1] for r in ridges]) + "\n")
        w(arr("double", "SKY%d_RIDGE_NEAR" % i, [r[3] for r in ridges]) + "\n")
        w(arr("double", "SKY%d_RIDGE_ROUGH" % i, [r[4] for r in ridges]) + "\n")
        w(arr("int64_t", "SKY%d_RIDGE_SEED" % i, [r[2] for r in ridges], str) + "\n")
        sun, cl = spec["sun"], spec["clouds"]
        w("static const clio_sky SKY%d = {%d, %d, %r, %r, %d, SKY%d_BASE, %r, %d, 0x%06Xu, %r, %d, SKY%d_RIDGE_RGB, "
          "SKY%d_RIDGE_H, SKY%d_RIDGE_NEAR, SKY%d_RIDGE_ROUGH, SKY%d_RIDGE_SEED, %d, %r, %r, %r, %r};\n" % (
              i, PW, yh, float(f), float(lateral), phase, i, float(spec["stars"]), 1 if cl else 0,
              u32(cl[0]) if cl else 0, float(cl[1]) if cl else 0.0, len(ridges), i, i, i, i, i,
              1 if sun else 0, *((float(sun[0]), float(sun[1]), float(sun[2]), float(sun[4])) if sun else
                                 (0.0, 0.0, 0.0, 0.0))))
        w("#define SKY%d_SUM 0x%016XULL\n#define SKY%d_RIDGES_SUM 0x%016XULL\n#define SKY%d_SPOTS %d\n" % (
            i, h, i, hs, i, len(spots)))
        w("#define SKY%d_FIRST_SPOT %d\n" % (i, spots[0] if spots else -1))
        if i + 1 < len(SKIES):
            w("\n")


if __name__ == "__main__":
    main()
