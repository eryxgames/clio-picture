"""Writes tests/scope_cases.h: what CLIo's Python draws on its vector display (engine/vector.py Scope) for a
small scene, as the calls that draw it and the checksums of what they leave, for test_picture.c.

    python3 tests/make_scope_cases.py PATH/TO/CLIo > tests/scope_cases.h

Three frames of one display with afterglow: a box before a net of lines and a post, seen from three sides -
faces that hide lines, lines that pass behind the eye and leave the display, lines that dim with distance, lines
and dots drawn by hand - then the characters read with and without a glow and a background."""
import math
import os
import struct
import sys

CLIO = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..", "clio")
sys.path.insert(0, os.path.abspath(CLIO))
os.environ["CLIO_PICTURE_LIB"] = "none"                     # the Python's own picture

from engine import vector  # noqa: E402

M64 = (1 << 64) - 1
COLS, ROWS = 23, 9


def fnv(h, data):
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & M64
    return h


def d(v):
    return repr(float(v))


def skipped(cam, wire):
    """The lines Scope.draw leaves out of a solid thing: those whose faces all look away."""
    verts = wire.verts
    front = []
    for f in wire.faces:
        a, b, c = verts[f[0]], verts[f[1]], verts[f[2]]
        front.append(vector._dot(vector._cross(vector._sub(b, a), vector._sub(c, a)), vector._sub(cam.eye, a)) > 0)
    return [bool(s) and not any(front[i] for i in s) for s in wire.sides()]


def main():
    w = sys.stdout.write
    w("/* Made by tests/make_scope_cases.py from CLIo's Python (engine/vector.py): do not edit. */\n")
    w("#define SCOPE_COLS %d\n#define SCOPE_ROWS %d\n\n" % (COLS, ROWS))
    scope = vector.Scope(COLS, ROWS, "amber", afterglow=0.6, cue=(2.0, 9.0), dim=0.25, fast=False)
    things, decl, sums, body = {}, [], [], []

    def thing(name, wire):
        if name not in things:
            things[name] = wire
            flat = [c for v in wire.verts for c in v]
            decl.append("static const double %s_V[%d] = {%s};" % (name, len(flat), ", ".join(d(c) for c in flat)))
            if wire.edges:
                decl.append("static const int32_t %s_E[%d] = {%s};" % (name, 2 * len(wire.edges), ", ".join(
                    "%d, %d" % e for e in wire.edges)))
            if wire.faces:
                corners = [i for f in wire.faces for i in f]
                decl.append("static const int32_t %s_C[%d] = {%s};" % (name, len(corners), ", ".join(map(str, corners))))
                decl.append("static const int32_t %s_N[%d] = {%s};" % (name, len(wire.faces), ", ".join(
                    str(len(f)) for f in wire.faces)))
        return name

    for n, turn in enumerate((0.0, 0.7, 1.9)):
        cam = vector.Camera((4.0 * math.sin(turn), -4.0 * math.cos(turn), 1.6 + turn), (0.1, 0.0, 0.4), fov=62.0,
                            roll=7.0 * turn)
        f = (scope.W / 2.0) / math.tan(math.radians(cam.fov) / 2.0)
        decl.append("static const clio_eye EYE%d = {{%s}, {%s}, {%s}, {%s}, %s, %s};" % (
            n, *(", ".join(d(c) for c in v) for v in (cam.eye, cam.right, cam.up, cam.ahead)), d(cam.near), d(f)))
        solid = turn < 1.0
        made = [("BOX%d" % n, vector.box((1.2, 0.9, 0.8)).placed((0.0, 0.3, 0.4), yaw=20.0), None, 1.0),
                ("NET", vector.grid(5, 5, 1.2), "cyan", 0.6),
                ("POST%d" % n, vector.box((0.5, 0.5, 2.5)).placed((1.1, -0.6 + turn, 1.25), yaw=-35.0, pitch=12.0),
                 (255, 90, 60), 1.0)]
        out = ["    if (frame == %d) {" % n, "        clio_scope_clear(s, %s);" % d(scope.afterglow)]
        scope.clear()
        if solid:
            out.append("        s->depth = depth;")
            for name, wire, _c, _b in made:
                if wire.faces:
                    thing(name, wire)
                    scope.hide(cam, wire)
                    out.append("        bad += clio_scope_hide(s, &EYE%d, %s_V, %d, %s_C, %s_N, %d, %s) != 0;" % (
                        n, name, len(wire.verts), name, name, len(wire.faces), d(1.0 - vector.OFFSET)))
        else:
            out.append("        s->depth = NULL;")
        for name, wire, colour, bright in made:
            thing(name, wire)
            skip = skipped(cam, wire) if solid and wire.faces else None
            scope.draw(cam, wire, colour, bright, solid=solid)
            ink = scope._ink(vector.colour_of(colour, scope.colour))
            if skip is not None:
                out.append("        { static const uint8_t skip[%d] = {%s};" % (len(skip), ", ".join(str(int(x)) for x in skip)))
            out.append("        %sbad += clio_scope_wire(s, &EYE%d, %s_V, %d, %s_E, %d, %s, %s, %d) != 0;%s" % (
                "  " if skip is not None else "", n, name, len(wire.verts), name, len(wire.edges),
                "skip" if skip is not None else "NULL", d(bright), ink, " }" if skip is not None else ""))
        for x0, y0, x1, y1, bright, colour, w0, w1 in ((-7.5, 3.25, 60.0, 31.5, 1.3, None, None, None),
                                                       (5.0, 40.0, 5.0, -4.0, 0.4, (10, 200, 250), 0.3, 0.9),
                                                       (12.0, 12.0, 12.0, 12.0, 0.9, "white", None, None),
                                                       (44.0, 2.0, 1.0, 30.0, 0.7, None, 0.05 + 0.02 * n, 0.4)):
            scope.line2(x0, y0, x1, y1, bright, colour, w0, w1)
            ink = scope._ink(vector.colour_of(colour, scope.colour))
            out.append("        clio_scope_line2(s, %s, %s, %s, %s, %s, %d, %d, %s, %s);" % (
                d(x0), d(y0), d(x1), d(y1), d(bright), ink, 0 if w0 is None else 1, d(w0 or 0.0), d(w1 or 0.0)))
        for x, y, bright, colour, way in ((3, 3, 1.7, None, -1), (45, 35, 0.5, (9, 9, 9), 2), (-1, 4, 1.0, None, 0),
                                          (3, 3, 0.9, "white", 1)):
            scope._put(x, y, bright, scope._ink(vector.colour_of(colour, scope.colour)), way)
            out.append("        clio_scope_dot(s, %d, %d, %s, %d, %d);" % (
                x, y, d(bright), scope._ink(vector.colour_of(colour, scope.colour)), way))
        out.append("    }")
        body += out
        h = 0xCBF29CE484222325
        for v in scope.light:
            h = fnv(h, struct.pack("<d", v))
        for v in scope.depth if scope.depth is not None else ():
            h = fnv(h, struct.pack("<d", v))
        for v in scope.best:
            h = fnv(h, struct.pack("<d", v))
        for v in list(scope.ink) + list(scope.way):
            h = fnv(h, struct.pack("<i", v))
        cells = []
        for glow, paper in ((True, None), (False, None), (True, (4, 6, 8)), (False, (4, 6, 8))):
            scope.glow, scope.paper = glow, paper
            hc = 0xCBF29CE484222325
            for row in scope.rows("dots"):
                for ch, fg, bg in row:
                    bits = ord(ch) - 0x2800 if ch != " " else 0
                    kind = 0 if not bits else (3 if bg is not None else 2)
                    hc = fnv(hc, bytes([bits, kind]) + bytes(fg or (0, 0, 0)) + bytes((bg or (0, 0, 0)) if bits else (0, 0, 0)))
            cells.append(hc)
        scope.glow, scope.paper = True, None
        sums.append((h, cells))
    w("\n".join(decl) + "\n\n")
    w("static const uint32_t SCOPE_COLOURS[%d] = {%s};\n" % (len(scope.colours), ", ".join(
        "0x%06Xu" % ((c[0] << 16) | (c[1] << 8) | c[2]) for c in scope.colours)))
    w("#define SCOPE_PAPER 0x040608u\n#define SCOPE_FRAMES %d\n" % len(sums))
    w("static const uint64_t SCOPE_SUM[%d] = {%s};\n" % (len(sums), ", ".join("0x%016XULL" % h for h, _ in sums)))
    w("static const uint64_t SCOPE_CELLS[%d][4] = {%s};\n\n" % (len(sums), ", ".join(
        "{%s}" % ", ".join("0x%016XULL" % c for c in cells) for _, cells in sums)))
    w("/* the calls that draw frame `frame`; depth: room for the faces' depths; returns how many refused */\n")
    w("static int scope_steps(clio_scope *s, double *depth, int frame)\n{\n    int bad = 0;\n")
    w("\n".join(body) + "\n    return bad;\n}\n")


if __name__ == "__main__":
    main()
