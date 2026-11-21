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


# ---- the frame (0.4): the calls CLIo makes, recorded, and what its Python draws for the same ---------- #
class Recorder:
    """Stands in for engine.picture.frame.Frame: keeps each call the View makes, with the cur_z of the moment."""
    def __init__(self, view):
        self.view, self.calls = view, []

    def _keep(self, name, *args):
        self.calls.append((name, self.view.cur_z) + args)

    def front(self, *a):
        self._keep("front", *a)

    def side(self, tex, xs, zcs, fogs, *a):
        self._keep("side", tex, list(xs), list(zcs), list(fogs), *a)

    def blit(self, rows, x0, y0, s, sw, sh, tone, dist):
        t = None if tone is None else (tone.kind, tone.fog, tone.haze, tone.k, tone.gone, tone.step, tone.id,
                                       tone.flash, tone.tinted, tone.tint)
        self._keep("blit", rows, x0, y0, s, sw, sh, t, dist)

    def darken(self, *a):
        self._keep("darken", *a)

    def triangles(self, tris):
        self._keep("triangles", list(tris))

    def recolour(self, how, k=0.0, colour=(0, 0, 0)):
        self._keep("recolour", how, k, colour)

    def vignette(self, hurt, where):
        self._keep("vignette", hurt, where)

    def shake(self, s):
        self._keep("shake", s)

    def canopy(self, colour):
        self._keep("canopy", colour)

    def dashboard(self, *a):
        self._keep("dashboard", *a)

    def cage(self):
        self._keep("cage")

    record = True

    def model_draw(self, m, held):
        pts, corner, mat, own, colour, mats = held
        fields = {name: getattr(m, name) for name, _t in m._fields_ if name not in (
            "pts", "corner", "mat", "own", "colour", "mats")}
        fields["light"] = list(m.light)
        self._keep("model", fields, list(pts), list(corner), list(mat), list(own), list(colour),
                   [(x.r, x.g, x.b, x.metal, x.rough, x.glow) for x in mats])
        return [1.0, 0.0, 0.0, 0.0]


class MeshRecorder(Recorder):
    """Takes models the 0.6 way: a kept mesh, the posed corners and where it stands."""
    meshes = True

    def mesh_draw(self, mesh, inst, m, held):
        fields = {name: getattr(m, name) for name, _t in m._fields_ if name not in (
            "pts", "corner", "mat", "own", "colour", "mats")}
        fields["light"] = list(m.light)
        self._keep("mesh", fields, list(held), (inst.wx, inst.wy, inst.fx, inst.fy, inst.lift), mesh.nverts,
                   list(mesh.faces), list(mesh.mat), list(mesh.own), list(mesh.colour),
                   [(x.r, x.g, x.b, x.metal, x.rough, x.glow) for x in mesh.mats])
        return [1.0, 0.0, 0.0, 0.0]


class RecPx(list):
    frame = None


def frame_pixel(x, y):
    return ((x * 29 + y * 7) & 255, (x * 3 + y * 41) & 255, (x * x + y) & 255)


def frame_depth(x, y):
    return 0.5 + ((x * 7 + y * 3) % 11) * 0.4


SPRITE = [[(250, 10, 10), None, (10, 250, 10), (10, 10, 250)], [None, (200, 200, 0), (0, 200, 200), None],
          [(120, 60, 30), (30, 60, 120), None, (255, 255, 255)]]
TRIS = [(((3.2, 1.1, 2.0), (14.7, 4.2, 2.5), (6.1, 12.9, 1.0)), (180, 90, 40)),
        (((5.0, 2.0, 1.5), (16.0, 9.0, 0.5), (2.0, 14.0, 3.0)), [[250.0, 10.0, 10.0], [10.0, 250.0, 10.0], [10.0, 10.0, 250.5]]),
        (((20.5, 0.5, 4.0), (23.0, 15.5, 4.0), (9.0, 8.0, 4.0)), (20, 200, 220)),
        (((1.0, 1.0, 0.2), (1.0, 1.0, 0.2), (5.0, 5.0, 0.2)), (255, 255, 255))]


class Vehicle:
    fuel, fuel_max = 4, 30


class CageLevel:
    class player:
        vehicle = type("V", (), {"vehicle": {}})()
    sprites = {}

    @staticmethod
    def weapon_sprite():
        return None


def frame_scene(v, px, land, part):
    """What CLIo's View does to a picture, call by call: in Python (px a list of rows) or recorded (px.frame)."""
    from engine import model as model_mod
    from engine.firstperson import _Tone
    fr = getattr(px, "frame", None)

    def tris(t):
        if fr is not None:
            fr.triangles(t)
        else:
            zbuf = {}
            for pts, cols in t:
                model_mod.raster(px, zbuf, v.PW, v.PH, pts, cols)
    if part == "colours":
        return frame_colours(v, px, fr)
    if part in ("models", "meshes"):
        return frame_models(v, px)
    tex = TILE_A
    v.front_face(px, 0.3, 2.0, tex)
    v.front_face(px, -1.0, 1.4, TILE_B, 0.4)
    v.side_face(px, 1.2, 1.0, tex)
    v.side_face(px, -1.3, 0.6, TILE_B, 0.25, True)
    v.side_face(px, 2.1, 3.0, tex, 0.0, True)
    v.cur_z = 1.3
    v.blit(px, SPRITE, 2, 3, 2.5)
    v.blit(px, SPRITE, -3, 9, 1.7, _Tone(0.6, (70, 80, 90) if land else None, 0.35), 1.1)
    v.blit(px, SPRITE, 10, -2, 3.1, _Tone(0.8, None, 0.0, gone=0.4, step=3.1, mid=7, tint=(255, 0, 0)), 2.2)
    v.blit(px, SPRITE, 15, 6, 1.0, _Tone(0.9, (10, 20, 30), 0.5, flash=True))
    v.shadow(px, 11.3, 12.6, 9.0)
    v.cur_z = 0.9
    tris(TRIS)
    v.cur_z = -1e9
    v.cockpit(px, CageLevel, None)
    v.canopy(px, (70, 76, 60))
    v.dashboard(px, Vehicle(), (70, 76, 60))
    return px


MODEL_SPECS = [
    {"materials": {"hull": {"color": "#6b717c", "metal": 0.8, "rough": 0.35}, "eye": {"color": "#ff3020", "glow": 3},
                   "paint": {"color": "#e07a1f", "rough": 0.5}},
     "parts": [{"shape": "sphere", "size": [0.5, 0.5, 0.34], "at": [0, 0, 0.55], "material": "hull", "detail": 1},
               {"shape": "box", "size": [0.5, 0.06, 0.04], "at": [0, 0, 0.53], "material": "paint"},
               {"shape": "cylinder", "axis": "y", "size": [0.15, 0.42, 0.15], "at": [0.36, -0.05, 0.52],
                "material": "eye", "color": "#20ff40"}]},
    {"smooth": True, "materials": {"glass": {"color": "#4080c0", "metal": 0.2, "rough": 0.15}},
     "parts": [{"shape": "sphere", "size": [0.6, 0.6, 0.6], "at": [0, 0, 0.6], "material": "glass", "detail": 2}]},
]


def frame_models(v, px):
    """Models as View.draw_model draws them: near and far, turned, broken apart, flashing, tinted, smooth."""
    from engine import model as model_mod
    a, b = (model_mod.Model("m%d" % i, spec) for i, spec in enumerate(MODEL_SPECS))
    cam = (0.0, 0.0, 0, -1, 1, 0)                         # at (0, 0) looking north
    v.cur_z = 2.0
    v.draw_model(px, a, 0.3, -2.2, 0.7, "idle", 0.0, cam)
    v.draw_model(px, a, -0.8, -1.0, 2.1, "idle", 0.0, cam, flash=True)               # near: flattened depth
    v.draw_model(px, b, 0.6, -1.6, 0.0, "idle", 0.0, cam, lift=0.2, size=0.8)
    v.draw_model(px, a, 1.4, -3.5, 4.0, "idle", 0.0, cam, tint=(40, 200, 255))
    v.draw_model(px, a, -1.0, -2.6, 1.0, "idle", 0.0, cam, gone=0.4, seed=5)           # breaking apart
    v.draw_model(px, b, 0.0, -0.05, 0.0, "idle", 0.0, cam)                              # partly behind the eye
    return px


def frame_colours(v, px, fr):
    px = v.recolour(px, 1, 1.35, None, lambda c: firstperson.shade(c, 1.35))
    px = v.recolour(px, 2, 0.0, None, lambda c: firstperson.mix(c, (255 - int(firstperson.lum(c)),) * 3, 0.85))
    px = v.recolour(px, 3, 0.0, None, firstperson._goggled)
    px = v.recolour(px, 4, 0.3, (255, 255, 255), lambda c: firstperson.mix(c, (255, 255, 255), 0.3))
    for where in (None, "left", "right", "back"):
        if fr is not None:
            fr.vignette(0.5, where)
        else:
            v.vignette(px, 0.5, where)
    for s in (2, -1):
        if fr is not None:
            fr.shake(s)
        else:
            black = (0, 0, 0)
            px = [([black] * s + row[:-s]) if s > 0 else (row[-s:] + [black] * -s) for row in px]
    return px


def frame_cases():
    from engine.land import DepthRow
    out = []
    for land, part in ((False, "drawing"), (True, "drawing"), (False, "colours"), (False, "models"), (True, "models"),
                       (False, "meshes"), (True, "meshes")):
        v = firstperson.View(24, 8, "pixel")
        v.set_eye(None)
        if land:
            v.persp, v.air = 1.6, ((70, 80, 90), 30.0)
        PW, PH = v.PW, v.PH
        v.cover, v.cur_z = {}, -1e9
        rows = [[frame_pixel(x, y) for x in range(PW)] for y in range(PH)]
        if land:
            rows = [DepthRow(r) for r in rows]
            for y, r in enumerate(rows):
                r.depth, r.view = [frame_depth(x, y) for x in range(PW)], v
        px = frame_scene(v, rows, land, part)
        h = 0xCBF29CE484222325
        for r in px:
            h = sum_rgb(h, r)
        W, VR = PW // v.sx, PH // v.sy
        h = sum_doubles(h, [v.cover.get((r, c), 99.0) for r in range(VR) for c in range(W)])
        rec = firstperson.View(24, 8, "pixel")
        rec.set_eye(None)
        if land:
            rec.persp, rec.air = 1.6, ((70, 80, 90), 30.0)
        rec.cover, rec.cur_z = {}, -1e9
        fake = RecPx([[frame_pixel(x, y) for x in range(PW)] for y in range(PH)])
        fake.frame = (MeshRecorder if part == "meshes" else Recorder)(rec)
        frame_scene(rec, fake, land, part)
        out.append((PW, PH, rec.sx, rec.sy, land, fake.frame.calls, h))
    return out


def c_tex(rows):
    return "{%s}" % ", ".join("0x%06Xu" % u32(c) if c is not None else "CLIO_CLEAR" for r in rows for c in r)


def frame_calls_c(i, calls):
    """A C function making the recorded calls on a frame."""
    lines, decl = [], []
    for k, call in enumerate(calls):
        name, cur_z = call[0], call[1]
        a = call[2:]
        lines.append("    f->cur_z = %r;" % float(cur_z))
        if name == "front":
            tex, X0, X1, Y0, Y1, x0, w, y0, h, opened, fog, dist = a
            decl.append("static const uint32_t F%d_T%d[] = %s;" % (i, k, c_tex(tex)))
            lines.append("    clio_frame_front(f, F%d_T%d, %d, %d, %d, %d, %d, %d, %r, %r, %r, %r, %r, %r, %r);" % (
                i, k, len(tex[0]), len(tex), X0, X1, Y0, Y1, float(x0), float(w), float(y0), float(h), float(opened),
                float(fog), float(dist)))
        elif name == "side":
            tex, xs, zcs, fogs, half, span, yh, ch, fs, opened, z = a
            decl.append("static const uint32_t F%d_T%d[] = %s;" % (i, k, c_tex(tex)))
            decl.append(arr("int32_t", "F%d_X%d" % (i, k), xs, str))
            decl.append(arr("double", "F%d_Z%d" % (i, k), zcs))
            decl.append(arr("double", "F%d_G%d" % (i, k), fogs))
            lines.append("    clio_frame_side(f, F%d_T%d, %d, %d, %d, F%d_X%d, F%d_Z%d, F%d_G%d, %r, %r, %r, %r, %r, %r, %r);" % (
                i, k, len(tex[0]), len(tex), len(xs), i, k, i, k, i, k, float(half), float(span), float(yh), float(ch),
                float(fs), float(opened), float(z)))
        elif name == "blit":
            rows, x0, y0, s, sw, sh, t, dist = a
            decl.append("static const uint32_t F%d_T%d[] = %s;" % (i, k, c_tex(rows)))
            tone = "NULL"
            if t is not None:
                decl.append("static const clio_tone F%d_N%d = {%d, %r, 0x%06Xu, %r, %r, %r, %d, %d, %d, 0x%06Xu};" % (
                    i, k, t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7], t[8], t[9]))
                tone = "&F%d_N%d" % (i, k)
            lines.append("    clio_frame_blit(f, F%d_T%d, %d, %d, %d, %d, %r, %d, %d, %s, %d, %r);" % (
                i, k, len(rows[0]), len(rows), x0, y0, float(s), sw, sh, tone, 0 if dist is None else 1,
                0.0 if dist is None else float(dist)))
        elif name == "darken":
            lines.append("    clio_frame_darken(f, %d, %d, %d, %d, %r);" % (a[0], a[1], a[2], a[3], float(a[4])))
        elif name == "triangles":
            lines.append("    clio_frame_model(f);")
            for j, (pts, cols) in enumerate(a[0]):
                flat_pts = [float(v) for p in pts for v in p]
                smooth = isinstance(cols[0], (list, tuple))
                flat_cols = [float(v) for c in cols for v in c] if smooth else [float(v) for v in cols]
                decl.append(arr("double", "F%d_P%d_%d" % (i, k, j), flat_pts))
                decl.append(arr("double", "F%d_C%d_%d" % (i, k, j), flat_cols))
                lines.append("    clio_frame_triangle(f, F%d_P%d_%d, F%d_C%d_%d, %d);" % (i, k, j, i, k, j, 1 if smooth else 0))
        elif name == "recolour":
            how, kk, colour = a
            lines.append("    clio_frame_recolour(f, %d, %r, 0x%06Xu);" % (how, float(kk), u32(colour)))
        elif name == "vignette":
            lines.append("    clio_frame_vignette(f, %r, %d);" % (float(a[0]), {"left": 1, "right": 2, "back": 3}.get(a[1], 0)))
        elif name == "shake":
            lines.append("    clio_frame_shake(f, %d);" % a[0])
        elif name == "canopy":
            lines.append("    clio_frame_canopy(f, 0x%06Xu);" % u32(a[0]))
        elif name == "dashboard":
            colour, h, y0, x0, x1, gx0, gx1, gy, lit, low = a
            lines.append("    clio_frame_dashboard(f, 0x%06Xu, %d, %d, %d, %d, %d, %d, %d, %d, %d);" % (
                u32(colour), h, y0, x0, x1, gx0, gx1, gy, lit, 1 if low else 0))
        elif name == "cage":
            lines.append("    clio_frame_cage(f);")
        elif name == "model":
            m, pts, corner, mat, own, colour, mats = a
            decl.append(arr("double", "F%d_MP%d" % (i, k), pts))
            decl.append(arr("int32_t", "F%d_MC%d" % (i, k), corner, str))
            decl.append(arr("int32_t", "F%d_MM%d" % (i, k), mat, str))
            decl.append(arr("int8_t", "F%d_MO%d" % (i, k), own, str))
            decl.append(arr("double", "F%d_MK%d" % (i, k), colour))
            decl.append("static const clio_material F%d_MT%d[] = {%s};" % (i, k, ", ".join(
                "{%s}" % ", ".join(repr(float(x)) for x in mt) for mt in mats)))
            lines.append("    {")
            lines.append("        clio_model_draw m = {0};")
            lines.append("        double out[4];")
            lines.append("        m.ntri = %d; m.pts = F%d_MP%d; m.corner = F%d_MC%d; m.mat = F%d_MM%d; m.own = F%d_MO%d;" % (
                m["ntri"], i, k, i, k, i, k, i, k))
            lines.append("        m.colour = F%d_MK%d; m.nverts = %d; m.mats = F%d_MT%d; m.nmats = %d; m.cull = %d; m.smooth = %d;" % (
                i, k, m["nverts"], i, k, m["nmats"], m["cull"], m["smooth"]))
            for fld in ("cx", "cy", "fx", "fy", "rx", "ry", "half", "lw", "yh", "fs", "ch", "persp", "flat", "zm", "eye", "vis"):
                lines.append("        m.%s = %r;" % (fld, float(m[fld])))
            lines.append("        m.light[0] = %r; m.light[1] = %r; m.light[2] = %r;" % tuple(float(x) for x in m["light"]))
            lines.append("        m.air = %d; m.haze = 0x%06Xu; m.flash = %d; m.tinted = %d; m.tint = 0x%06Xu;" % (
                m["air"], m["haze"], m["flash"], m["tinted"], m["tint"]))
            lines.append("        if (clio_frame_model_draw(f, &m, out) != 0)")
            lines.append("            frame_model_failed++;")
            lines.append("    }")
        elif name == "mesh":
            m, verts, (wx, wy, fx, fy, lift), nverts, faces, mat, own, colour, mats = a
            decl.append(arr("double", "F%d_MV%d" % (i, k), verts))
            decl.append(arr("int32_t", "F%d_MF%d" % (i, k), faces, str))
            decl.append(arr("int32_t", "F%d_MM%d" % (i, k), mat, str))
            decl.append(arr("int8_t", "F%d_MO%d" % (i, k), own, str))
            decl.append(arr("double", "F%d_MK%d" % (i, k), colour))
            decl.append("static const clio_material F%d_MT%d[] = {%s};" % (i, k, ", ".join(
                "{%s}" % ", ".join(repr(float(x)) for x in mt) for mt in mats)))
            lines.append("    {")
            lines.append("        clio_mesh *mesh = clio_mesh_new(%d, %d, F%d_MF%d, F%d_MM%d, F%d_MO%d, F%d_MK%d, F%d_MT%d, %d);" % (
                nverts, len(mat), i, k, i, k, i, k, i, k, i, k, len(mats)))
            lines.append("        clio_instance in = {F%d_MV%d, %r, %r, %r, %r, %r};" % (
                i, k, float(wx), float(wy), float(fx), float(fy), float(lift)))
            lines.append("        clio_model_draw m = {0};")
            lines.append("        double out[4];")
            lines.append("        m.cull = %d; m.smooth = %d;" % (m["cull"], m["smooth"]))
            for fld in ("cx", "cy", "fx", "fy", "rx", "ry", "half", "lw", "yh", "fs", "ch", "persp", "flat", "zm", "eye", "vis"):
                lines.append("        m.%s = %r;" % (fld, float(m[fld])))
            lines.append("        m.light[0] = %r; m.light[1] = %r; m.light[2] = %r;" % tuple(float(x) for x in m["light"]))
            lines.append("        m.air = %d; m.haze = 0x%06Xu; m.flash = %d; m.tinted = %d; m.tint = 0x%06Xu;" % (
                m["air"], m["haze"], m["flash"], m["tinted"], m["tint"]))
            lines.append("        if (!mesh || clio_frame_mesh_draw(f, mesh, &in, &m, out) != 0)")
            lines.append("            frame_model_failed++;")
            lines.append("        clio_mesh_free(mesh);")
            lines.append("    }")
    return "\n".join(decl) + "\n\nstatic void FRAME%d_CALLS(clio_frame *f)\n{\n%s\n}\n" % (i, "\n".join(lines))


MESHES = [  # a view of CLIo's tests/test_mesh.py features room: W, VR, style, where from, facing, camera, beat
    (40, 12, "pixel", (6, 9), 0, (6.0, 8.6), 3),
    (24, 8, "dots", (9, 7), 3, None, 1),
    (36, 10, "pixel", (11, 7), 0, None, 2),                  # the east wall runs on past the camera: clipped near it
]


def mesh_cases():
    """What CLIo's engine/mesh.py draws of a room with every kind of face and floor, as CLIo Picture is to draw it
    (teleporters left to the caller): the faces as CLIo packs them, the tiles, the sky, and sums of the pixels, the
    distances, the cover and the pixels left."""
    sys.path.insert(0, os.path.join(os.path.abspath(CLIO), "tests"))
    import test_mesh
    from engine import mesh, picture
    out = []
    for W, VR, style, (x, y), f, cam, phase in MESHES:
        lv = test_mesh.features_room()
        test_mesh.put(lv, x, y, f)
        v = firstperson.View(W, VR, style, "high", render="mesh")
        cx, cy = cam if cam is not None else (x, y)
        fx, fy = firstperson.DIRS[f]
        rx, ry = firstperson.DIRS[(f + 1) % 4]
        cam6 = (cx, cy, fx, fy, rx, ry)
        v.persp = mesh.Z0
        v.set_eye_abs(lv.ground(cx, cy) + v.EYE0)
        sky = v.sky_rows(lv, cx, cy, f, phase)
        m = mesh.Mesh(v)
        ex, ey = cx - fx * mesh.Z0, cy - fy * mesh.Z0
        faces = m.faces(lv, cam6, v.EYE, {}, phase, ex, ey)
        later = []
        rgb, zbuf, cover, _labels = m.draw(lv, cam6, v.EYE, phase, {}, sky, later=later)
        ints, geo, xy, tiles = picture.pack_rooms(faces, mesh)
        cov = [cover.get((r, c), 99.0) for r in range(v.VR) for c in range(v.W)]
        h = sum_rgb(0xCBF29CE484222325, [c for row in rgb for c in row])
        h = sum_doubles(h, list(zbuf))
        h = sum_doubles(h, cov)
        h = fnv(h, b"".join(int(n).to_bytes(4, "little", signed=True) for p_ in later for n in p_))
        out.append((v, cam6, phase, sky, list(ints), list(geo), list(xy), tiles, len(later), h))
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
    import struct
    probes = [(0.7, 1 / 2.4), (0.0123, 1 / 2.4), (0.64, 3.7), (0.31, 5.0), (0.9, 2.0)]
    w("/* this machine's pow and exp on a few numbers: the model cases apply where the test's give the same */\n")
    w("static const double PROBE_POW[%d][3] = {%s};\n" % (len(probes), ", ".join(
        "{%r, %r, %r}" % (x, y, x ** y) for x, y in probes)))
    w("static const double PROBE_EXP[2][2] = {{%r, %r}, {%r, %r}};\n\n" % (-0.37, math.exp(-0.37), -2.5, math.exp(-2.5)))
    probes75 = [(0.37, 0.75), (0.9, 0.75), (0.123, 0.75)]
    w("/* this machine's pow for the mesh view's fog (x ** 0.75): its cases apply where the test's gives the same */\n")
    w("static const double PROBE_POW75[%d][3] = {%s};\n\n" % (len(probes75), ", ".join(
        "{%r, %r, %r}" % (x, y, x ** y) for x, y in probes75)))
    pool = []                                                 # every tile of the mesh cases, once
    for i, (v, cam6, phase, sky, ints, geo, xy, tiles, nlater, h) in enumerate(mesh_cases()):
        names = []
        for t in tiles:
            rows = t[0]
            k = next((j for j, r in enumerate(pool) if r is rows or r == rows), None)
            if k is None:
                k = len(pool)
                pool.append(rows)
                w(arr("uint32_t", "MESH_TILE%d" % k, list(t[3]), hex) + "\n")
            names.append(k)
        w("static const uint32_t *const MESH%d_TEXELS[%d] = {%s};\n" % (
            i, max(1, len(names)), ", ".join("MESH_TILE%d" % k for k in names) or "NULL"))
        w(arr("int32_t", "MESH%d_TW" % i, [t[1] for t in tiles] or [0], str) + "\n")
        w(arr("int32_t", "MESH%d_TH" % i, [t[2] for t in tiles] or [0], str) + "\n")
        w(arr("int32_t", "MESH%d_INTS" % i, ints, str) + "\n")
        w(arr("double", "MESH%d_GEO" % i, geo) + "\n")
        w(arr("int64_t", "MESH%d_XY" % i, xy, str) + "\n")
        if sky:
            w(arr("uint8_t", "MESH%d_SKY" % i, [b for row in sky for c in row for b in c], str) + "\n")
        cx, cy, fx, fy, rx, ry = cam6
        from engine import mesh as mesh_mod
        w("static const clio_rooms MESH%d = {%d, %d, %r, %r, %r, %r, %r, %r, %r, %r, %r, %r, %r, %r, %r, %r, %r, %r, "
          "%r, %d, %s, %d, %d, MESH%d_INTS, MESH%d_GEO, MESH%d_XY, %d, MESH%d_TEXELS, MESH%d_TW, MESH%d_TH};\n" % (
              i, v.PW, v.PH, v.half, float(v.yh), v.LW * mesh_mod.Z0, v.TALL * mesh_mod.Z0, float(cx), float(cy),
              float(fx), float(fy), float(rx), float(ry), mesh_mod.Z0, float(v.EYE), float(mesh_mod.FAR),
              mesh_mod.FADE, mesh_mod.NEAR, mesh_mod.SIDE, mesh_mod.NONE, phase,
              ("MESH%d_SKY" % i) if sky else "NULL", len(sky) if sky else 0, len(ints) // 9, i, i, i, len(tiles), i,
              i, i))
        w("#define MESH%d_SX %d\n#define MESH%d_SY %d\n#define MESH%d_LATER %d\n#define MESH%d_SUM 0x%016XULL\n\n" % (
            i, v.sx, i, v.sy, i, nlater, i, h))
    for i, (PW, PH, sx, sy, land, calls, h) in enumerate(frame_cases()):
        w(frame_calls_c(i, calls))
        w("#define FRAME%d_PW %d\n#define FRAME%d_PH %d\n#define FRAME%d_SX %d\n#define FRAME%d_SY %d\n"
          "#define FRAME%d_LAND %d\n#define FRAME%d_SUM 0x%016XULL\n\n" % (i, PW, i, PH, i, sx, i, sy, i, 1 if land else 0, i, h))
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
